#include "NativeMountService.h"

#include "NativeMountCatalog.h"
#include "NativeMountEligibility.h"
#include "api/ContentResourceApiV1.h"

#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "WorldSession.h"
#include "WorldPacket.h"

#include <algorithm>
#include <chrono>
#include <charconv>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace native_mounts
{
namespace
{
struct GrantState
{
    std::uint8_t state = 0;
    bool introducedByModule = false;
};

struct ReconcileState
{
    std::unordered_map<std::string, std::uint8_t> ownership;
    std::unordered_map<std::uint32_t, GrantState> grants;
};

CharacterEligibility Snapshot(Player const* player, MountVariant const& variant)
{
    CharacterEligibility result;
    result.level = player->GetLevel();
    result.race = player->getRace();
    result.characterClass = player->getClass();
    result.riding = player->GetSkillValue(SKILL_RIDING);
    if (variant.professionSkill)
    {
        result.professionSkill = variant.professionSkill;
        // Eligibility is based on trained skill only. Equipment and aura
        // bonuses must never unlock account-wide profession mounts.
        result.professionRank = player->GetPureSkillValue(variant.professionSkill);
    }
    return result;
}

std::string KeyList(std::vector<MountVariant const*> const& variants)
{
    std::unordered_set<std::string_view> seen;
    std::ostringstream sql;
    bool first = true;
    for (MountVariant const* variant : variants)
    {
        if (!variant || !seen.insert(variant->key).second)
            continue;
        if (!first)
            sql << ',';
        first = false;
        sql << '\'' << variant->key << '\'';
    }
    return sql.str();
}

ReconcileState LoadState(Player const* player, std::vector<MountVariant const*> const& variants)
{
    ReconcileState state;
    std::string const keys = KeyList(variants);
    if (keys.empty())
        return state;

    // One batched read loads both account ownership and character provenance.
    // Revoked ownership rows are included so import cannot erase tombstones.
    QueryResult rows = CharacterDatabase.Query(
        "SELECT 0 AS `kind`,`mount_key`,`state`,0 AS `spell_id`,0 AS `introduced_by_module` "
        "FROM `native_mount_account_ownership` WHERE `account_id`={} AND `mount_key` IN ({}) "
        "UNION ALL SELECT 1,`mount_key`,`state`,`spell_id`,`introduced_by_module` "
        "FROM `native_mount_character_grant` WHERE `character_guid`={} AND `mount_key` IN ({})",
        player->GetSession()->GetAccountId(), keys, player->GetGUID().GetCounter(), keys);
    if (!rows)
        return state;

    do
    {
        Field* fields = rows->Fetch();
        if (fields[0].Get<std::uint8_t>() == 0)
            state.ownership[fields[1].Get<std::string>()] = fields[2].Get<std::uint8_t>();
        else
            state.grants[fields[3].Get<std::uint32_t>()] =
                { fields[2].Get<std::uint8_t>(), fields[4].Get<bool>() };
    } while (rows->NextRow());
    return state;
}

bool IsModuleGrant(GrantState const* grant)
{
    return grant && grant->introducedByModule && (grant->state == 0 || grant->state == 1);
}

void PrimePreviewCreatureCache(Player* player)
{
    if (!player || !player->GetSession())
        return;
    std::unordered_set<std::uint32_t> sent;
    for (MountVariant const& variant : Catalog())
    {
        if (!variant.previewCreatureId || !sent.insert(variant.previewCreatureId).second)
            continue;
        if (!sObjectMgr->GetCreatureTemplate(variant.previewCreatureId))
            continue;
        // SetCreature resolves through the client's creature-query cache. A
        // character need not have encountered or learned this creature, so
        // prime the same authoritative response the normal query handler uses.
        WorldPacket query(CMSG_CREATURE_QUERY, 12);
        query << variant.previewCreatureId << ObjectGuid::Empty;
        player->GetSession()->HandleCreatureQueryOpcode(query);
    }
}

void SendSnapshot(Player* player, std::unordered_set<std::string> const& owned,
    std::uint32_t requestId, std::uint32_t randomMountSpell)
{
    if (!player)
        return;
    player->Whisper("NMOUNTS\t1\tRESET\t" + std::to_string(requestId), LANG_ADDON, player);
    std::string const prefix = "NMOUNTS\t1\tOWN\t" + std::to_string(requestId) + "\t";
    std::string payload = prefix;
    for (MountVariant const& variant : Catalog())
    {
        if (owned.find(std::string(variant.key)) == owned.end())
            continue;
        std::string token = std::to_string(variant.spellId);
        if (payload.size() + token.size() + 1 > 240)
        {
            player->Whisper(payload, LANG_ADDON, player);
            payload = prefix;
        }
        if (payload.size() != prefix.size()) payload += ',';
        payload += token;
    }
    if (payload.size() != prefix.size())
        player->Whisper(payload, LANG_ADDON, player);
    player->Whisper("NMOUNTS\t1\tABILITY\t" + std::to_string(requestId) + "\t" +
        std::to_string(randomMountSpell), LANG_ADDON, player);
    player->Whisper("NMOUNTS\t1\tEND\t" + std::to_string(requestId), LANG_ADDON, player);
}

ContentResourcesV1::Provider const* FindContentProvider(std::string& reason)
{
    ContentResourcesV1::Provider const* provider = nullptr;
    for (auto const& script : ScriptRegistry<WorldScript>::ScriptPointerList)
    {
        auto const* candidate = dynamic_cast<ContentResourcesV1::Provider const*>(script.second);
        if (!candidate)
            continue;
        if (provider)
        {
            reason = "multiple ContentResourceApiV1 providers are registered";
            return nullptr;
        }
        provider = candidate;
    }
    if (!provider)
        reason = "Content Manager resource provider is not registered";
    return provider;
}
}

Service& Service::Instance()
{
    static Service service;
    return service;
}

void Service::Configure(ServiceConfig const& config)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _config = config;
}

bool Service::Initialize()
{
    // Force construction of the immutable generated catalog once at startup.
    std::size_t const catalogSize = Catalog().size();
    QueryResult result = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM `information_schema`.`TABLES` WHERE `TABLE_SCHEMA` = DATABASE() "
        "AND `TABLE_NAME` IN ('native_mount_account_ownership','native_mount_character_grant')");
    bool enabled = false;
    bool databaseAvailable = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _databaseAvailable = result && result->Fetch()[0].Get<std::uint64_t>() == 2;
        enabled = _config.enabled;
        databaseAvailable = _databaseAvailable;
    }
    if (enabled && !databaseAvailable)
        LOG_ERROR("module.native_mounts", "PoC enabled but native mount ownership/grant tables are missing.");
    else if (enabled)
        LOG_INFO("module.native_mounts", "Loaded {} generated mount spell variants at startup.", catalogSize);
    if (enabled && databaseAvailable)
        ResolveRandomMountSpell();
    return databaseAvailable;
}

bool Service::ResolveRandomMountSpell()
{
    std::string reason;
    ContentResourcesV1::Provider const* provider = FindContentProvider(reason);
    std::uint32_t spellId = 0;
    if (!provider || provider->ResolveResource("mod-native-mounts", "random-mount",
            "spell.id", spellId, reason) != ContentResourcesV1::Result::Ready || !spellId ||
        !sSpellMgr->GetSpellInfo(spellId))
    {
        LOG_ERROR("module.native_mounts", "Random Mount managed spell is unavailable: {}",
            reason.empty() ? "resource is not active or SpellInfo requires a worldserver restart" : reason);
        return false;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    _randomMountSpell = spellId;
    LOG_INFO("module.native_mounts", "Resolved Random Mount managed spell {}.", spellId);
    return true;
}

bool Service::IsEnabled() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _config.enabled && _databaseAvailable;
}

std::uint64_t Service::OperationKey(Player const* player, std::uint32_t spellId) const
{
    return (static_cast<std::uint64_t>(player->GetGUID().GetCounter()) << 32U) | spellId;
}

void Service::ReconcileCollections(Player* player, std::uint32_t requestId)
{
    if (!player || !player->GetSession())
        return;
    auto const started = std::chrono::steady_clock::now();
    std::vector<MountVariant const*> variants;
    variants.reserve(Catalog().size());
    for (MountVariant const& variant : Catalog())
        variants.push_back(&variant);
    bool revokeIneligible = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_config.enabled || !_databaseAvailable)
            return;
        revokeIneligible = _config.revokeIneligibleModuleGrants;
    }

    std::uint32_t const guid = player->GetGUID().GetCounter();
    std::uint32_t const account = player->GetSession()->GetAccountId();
    auto const queryStarted = std::chrono::steady_clock::now();
    ReconcileState state = LoadState(player, variants);
    auto const queryElapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - queryStarted).count();
    std::vector<std::string> importedKeys;
    std::vector<MountVariant const*> independentSpells;
    std::uint32_t grants = 0;
    std::uint32_t revokes = 0;

    for (MountVariant const* variant : variants)
    {
        auto grantIt = state.grants.find(variant->spellId);
        GrantState const* grant = grantIt == state.grants.end() ? nullptr : &grantIt->second;
        if (!player->HasSpell(variant->spellId) || IsModuleGrant(grant) ||
            (grant && grant->state == 3 && !grant->introducedByModule))
            continue;
        independentSpells.push_back(variant);

        auto ownershipIt = state.ownership.find(std::string(variant->key));
        if (ownershipIt == state.ownership.end())
        {
            state.ownership[std::string(variant->key)] = 1;
            importedKeys.emplace_back(variant->key);
        }
        state.grants[variant->spellId] = { 3, false };
    }

    if (!importedKeys.empty())
    {
        std::ostringstream sql;
        sql << "INSERT IGNORE INTO `native_mount_account_ownership` "
               "(`account_id`,`mount_key`,`state`,`first_character_guid`,`first_spell_id`,`acquisition_source`) VALUES ";
        bool first = true;
        for (MountVariant const* variant : independentSpells)
        {
            if (std::find(importedKeys.begin(), importedKeys.end(), std::string(variant->key)) == importedKeys.end())
                continue;
            if (!first) sql << ',';
            first = false;
            sql << '(' << account << ",\'" << variant->key << "\',1," << guid << ','
                << variant->spellId << ",1)";
        }
        CharacterDatabase.DirectExecute(sql.str());
    }
    if (!independentSpells.empty())
    {
        std::ostringstream sql;
        sql << "INSERT INTO `native_mount_character_grant` "
               "(`character_guid`,`spell_id`,`mount_key`,`state`,`introduced_by_module`) VALUES ";
        bool first = true;
        for (MountVariant const* variant : independentSpells)
        {
            if (!first) sql << ',';
            first = false;
            sql << '(' << guid << ',' << variant->spellId << ",\'" << variant->key << "\',3,0)";
        }
        sql << " ON DUPLICATE KEY UPDATE `mount_key`=VALUES(`mount_key`),`state`=3,`introduced_by_module`=0";
        CharacterDatabase.Execute(sql.str());
    }

    for (MountVariant const* variant : variants)
    {
        auto ownershipIt = state.ownership.find(std::string(variant->key));
        bool const accountOwns = ownershipIt != state.ownership.end() && ownershipIt->second == 1;
        auto grantIt = state.grants.find(variant->spellId);
        GrantState const* grant = grantIt == state.grants.end() ? nullptr : &grantIt->second;
        bool const externallySuppressed = grant && grant->state == 4;
        bool const eligible = Evaluate(*variant, Snapshot(player, *variant)) == EligibilityFailure::None;
        bool const moduleGrant = IsModuleGrant(grant);

        ReconcileAction const action = DecideReconcileAction(accountOwns, eligible,
            player->HasSpell(variant->spellId), externallySuppressed, moduleGrant, revokeIneligible);
        if (action == ReconcileAction::Grant)
        {
            ++grants;
            // Provenance commits before learnSpell exposes the grant or invokes
            // OnPlayerLearnSpell.
            CharacterDatabase.DirectExecute(
                "INSERT INTO `native_mount_character_grant` "
                "(`character_guid`,`spell_id`,`mount_key`,`state`,`introduced_by_module`) "
                "VALUES ({},{},'{}',0,1) ON DUPLICATE KEY UPDATE `mount_key`=VALUES(`mount_key`),"
                "`state`=0,`introduced_by_module`=1", guid, variant->spellId, variant->key.data());
            std::uint64_t const key = OperationKey(player, variant->spellId);
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _granting.insert(key);
            }
            player->learnSpell(variant->spellId, false);
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _granting.erase(key);
            }
            if (player->HasSpell(variant->spellId))
                CharacterDatabase.Execute(
                    "UPDATE `native_mount_character_grant` SET `state`=1 "
                    "WHERE `character_guid`={} AND `spell_id`={}", guid, variant->spellId);
        }
        else if (action == ReconcileAction::Revoke)
        {
            ++revokes;
            std::uint64_t const key = OperationKey(player, variant->spellId);
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _revoking.insert(key);
            }
            player->removeSpell(variant->spellId, SPEC_MASK_ALL, false);
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _revoking.erase(key);
            }
            CharacterDatabase.Execute(
                "UPDATE `native_mount_character_grant` SET `state`=2 "
                "WHERE `character_guid`={} AND `spell_id`={}", guid, variant->spellId);
        }
    }

    std::unordered_set<std::string> owned;
    for (auto const& [key, value] : state.ownership)
        if (value == 1)
            owned.insert(key);
    PrimePreviewCreatureCache(player);
    std::uint32_t randomMountSpell = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        randomMountSpell = _randomMountSpell;
    }
    if (randomMountSpell && !player->HasSpell(randomMountSpell))
        player->learnSpell(randomMountSpell, false);
    SendSnapshot(player, owned, requestId,
        randomMountSpell && player->HasSpell(randomMountSpell) ? randomMountSpell : 0);
    auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started).count();
    LOG_INFO("module.native_mounts",
        "Collections reconciliation account={} character={} request={} db_reads=1 sync_writes={} grants={} revokes={} imports={} query_us={} world_thread_us={}",
        account, guid, requestId, importedKeys.empty() ? grants : grants + 1,
        grants, revokes, importedKeys.size(), queryElapsed, elapsed);
}

bool Service::IsRandomMountSpell(std::uint32_t spellId) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return spellId && spellId == _randomMountSpell;
}

SpellCastResult Service::PrepareRandomMount(Player* player, bool strict)
{
    if (!player || !player->GetSession() || !IsEnabled())
        return SPELL_FAILED_SPELL_UNAVAILABLE;
    if (!strict)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_preparedRandomMounts.find(player->GetGUID().GetCounter()) != _preparedRandomMounts.end())
            return SPELL_CAST_OK;
    }

    std::vector<MountVariant const*> known;
    known.reserve(Catalog().size());
    for (MountVariant const& variant : Catalog())
        if (player->HasSpell(variant.spellId) &&
            Evaluate(variant, Snapshot(player, variant)) == EligibilityFailure::None)
            known.push_back(&variant);

    if (known.empty())
        return SPELL_FAILED_NOT_KNOWN;

    ReconcileState state = LoadState(player, known);
    std::vector<std::uint32_t> eligible;
    std::unordered_set<std::string_view> logicalKeys;
    eligible.reserve(known.size());
    for (MountVariant const* variant : known)
    {
        auto ownership = state.ownership.find(std::string(variant->key));
        std::uint8_t const ownershipState = ownership == state.ownership.end() ? 0 : ownership->second;
        auto grant = state.grants.find(variant->spellId);
        std::uint8_t const grantState = grant == state.grants.end() ? 0xff : grant->second.state;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(variant->spellId);
        bool dynamicallyEligible = false;
        if (info)
        {
            Spell probe(player, info, TRIGGERED_NONE);
            SpellCastTargets targets;
            targets.SetUnitTarget(player);
            probe.InitExplicitTargets(targets);
            dynamicallyEligible = probe.CheckCast(true) == SPELL_CAST_OK;
        }
        if (IsRandomMountCandidate(true, ownershipState, grantState, true, dynamicallyEligible) &&
            logicalKeys.insert(variant->key).second)
            eligible.push_back(variant->spellId);
    }

    if (eligible.empty())
        return SPELL_FAILED_NOT_HERE;
    std::uint32_t const selected = eligible[urand(0, eligible.size() - 1)];
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _preparedRandomMounts[player->GetGUID().GetCounter()] = selected;
    }
    return SPELL_CAST_OK;
}

void Service::CastPreparedRandomMount(Player* player)
{
    if (!player)
        return;
    std::uint32_t spellId = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto const found = _preparedRandomMounts.find(player->GetGUID().GetCounter());
        if (found == _preparedRandomMounts.end())
            return;
        spellId = found->second;
        _preparedRandomMounts.erase(found);
    }
    // The selected public mount is still cast normally. Core cast checks and
    // the mount's own scripts remain authoritative; no triggered flags bypass
    // location, movement, combat, water, or flight restrictions.
    player->CastSpell(player, spellId, false);
}

void Service::CancelPreparedRandomMount(Player* player)
{
    if (!player)
        return;
    std::lock_guard<std::mutex> lock(_mutex);
    _preparedRandomMounts.erase(player->GetGUID().GetCounter());
}

void Service::HandleLearn(Player* player, std::uint32_t spellId)
{
    MountVariant const* variant = FindBySpell(spellId);
    if (!variant || !player || !player->GetSession() || !IsEnabled())
        return;
    std::uint64_t const operation = OperationKey(player, spellId);
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_granting.find(operation) != _granting.end())
        {
            CharacterDatabase.Execute(
                "UPDATE `native_mount_character_grant` SET `state`=1 "
                "WHERE `character_guid`={} AND `spell_id`={}", player->GetGUID().GetCounter(), spellId);
            return;
        }
    }

    std::uint32_t const account = player->GetSession()->GetAccountId();
    // INSERT IGNORE is both the duplicate check and the write. In particular,
    // it does not overwrite an administrative-revocation tombstone.
    CharacterDatabase.Execute(
        "INSERT IGNORE INTO `native_mount_account_ownership` "
        "(`account_id`,`mount_key`,`state`,`first_character_guid`,`first_spell_id`,`acquisition_source`) "
        "VALUES ({},'{}',1,{},{},2)", account, variant->key.data(),
        player->GetGUID().GetCounter(), spellId);

    CharacterDatabase.Execute(
        "INSERT INTO `native_mount_character_grant` "
        "(`character_guid`,`spell_id`,`mount_key`,`state`,`introduced_by_module`) "
        "VALUES ({},{},'{}',3,0) ON DUPLICATE KEY UPDATE `mount_key`=VALUES(`mount_key`),"
        "`state`=3,`introduced_by_module`=0", player->GetGUID().GetCounter(), spellId, variant->key.data());

}

void Service::HandleForget(Player* player, std::uint32_t spellId)
{
    MountVariant const* variant = FindBySpell(spellId);
    if (!variant || !player || !IsEnabled())
        return;
    std::uint64_t const key = OperationKey(player, spellId);
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_revoking.find(key) != _revoking.end())
            return;
    }

    std::vector<MountVariant const*> variants = { variant };
    ReconcileState state = LoadState(player, variants);
    auto grantIt = state.grants.find(spellId);
    GrantState const* grant = grantIt == state.grants.end() ? nullptr : &grantIt->second;
    bool const lostEligibility = Evaluate(*variant, Snapshot(player, *variant)) != EligibilityFailure::None;
    if (IsModuleGrant(grant) && lostEligibility)
    {
        // Core can remove skill-associated spells as part of a skill change.
        // Classify that removal as an eligibility revoke; eligibility is
        // reevaluated only when Collections is next opened.
        CharacterDatabase.DirectExecute(
            "UPDATE `native_mount_character_grant` SET `state`=2 "
            "WHERE `character_guid`={} AND `spell_id`={}", player->GetGUID().GetCounter(), spellId);
        return;
    }

    CharacterDatabase.DirectExecute(
        "INSERT INTO `native_mount_character_grant` "
        "(`character_guid`,`spell_id`,`mount_key`,`state`,`introduced_by_module`) "
        "VALUES ({},{},'{}',4,0) ON DUPLICATE KEY UPDATE `mount_key`=VALUES(`mount_key`),"
        "`state`=4,`introduced_by_module`=0", player->GetGUID().GetCounter(), spellId, variant->key.data());
}

bool Service::SetAccountOwnership(std::uint32_t accountId, std::string_view mountKey, bool owned)
{
    std::vector<MountVariant const*> variants = FindByKey(mountKey);
    if (!IsEnabled() || variants.empty())
        return false;
    MountVariant const* representative = variants.front();
    CharacterDatabase.DirectExecute(
        "INSERT INTO `native_mount_account_ownership` "
        "(`account_id`,`mount_key`,`state`,`first_character_guid`,`first_spell_id`,`acquisition_source`) "
        "VALUES ({},'{}',{},0,{},3) ON DUPLICATE KEY UPDATE `state`=VALUES(`state`),"
        "`acquisition_source`=3", accountId, representative->key.data(), owned ? 1 : 2,
        representative->spellId);
    return true;
}

void Service::HandleAddonWhisper(Player* player, std::string_view message)
{
    constexpr std::string_view prefix = "NMOUNTS\t1\tSYNC\t";
    if (!player || !player->GetSession() || !IsEnabled() || message.rfind(prefix, 0) != 0)
        return;
    std::string_view const value = message.substr(prefix.size());
    if (value.empty() || value.size() > 10)
        return;
    std::uint32_t requestId = 0;
    auto const parsed = std::from_chars(value.data(), value.data() + value.size(), requestId);
    if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size() || requestId == 0)
        return;
    ReconcileCollections(player, requestId);
}
}

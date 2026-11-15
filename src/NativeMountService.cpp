#include "NativeMountService.h"

#include "NativeMountCatalog.h"
#include "NativeMountEligibility.h"

#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldSession.h"

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
        result.professionRank = player->GetSkillValue(variant.professionSkill);
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
    QueryResult result = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM `information_schema`.`TABLES` WHERE `TABLE_SCHEMA` = DATABASE() "
        "AND `TABLE_NAME` IN ('native_mount_account_ownership','native_mount_character_grant')");
    std::lock_guard<std::mutex> lock(_mutex);
    _databaseAvailable = result && result->Fetch()[0].Get<std::uint64_t>() == 2;
    if (_config.enabled && !_databaseAvailable)
        LOG_ERROR("module.native_mounts", "PoC enabled but native mount ownership/grant tables are missing.");
    return _databaseAvailable;
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

void Service::Register(Player* player)
{
    if (!player || !player->GetSession())
        return;
    std::lock_guard<std::mutex> lock(_mutex);
    _onlineByAccount[player->GetSession()->GetAccountId()].insert(player->GetGUID().GetCounter());
}

void Service::ReconcileLogin(Player* player)
{
    if (!player || !player->GetSession() || !IsEnabled())
        return;
    Register(player);
    std::vector<MountVariant const*> variants;
    variants.reserve(PocCatalog().size());
    for (MountVariant const& variant : PocCatalog())
        variants.push_back(&variant);
    Reconcile(player, variants, true);
}

void Service::Reconcile(Player* player, std::vector<MountVariant const*> const& variants, bool importKnownSpells)
{
    if (!player || !player->GetSession() || variants.empty())
        return;
    bool revokeIneligible = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_config.enabled || !_databaseAvailable)
            return;
        revokeIneligible = _config.revokeIneligibleModuleGrants;
    }

    std::uint32_t const guid = player->GetGUID().GetCounter();
    std::uint32_t const account = player->GetSession()->GetAccountId();
    ReconcileState state = LoadState(player, variants);
    std::vector<std::string> importedKeys;

    if (importKnownSpells)
    {
        for (MountVariant const* variant : variants)
        {
            auto grantIt = state.grants.find(variant->spellId);
            GrantState const* grant = grantIt == state.grants.end() ? nullptr : &grantIt->second;
            if (!player->HasSpell(variant->spellId) || IsModuleGrant(grant))
                continue;

            auto ownershipIt = state.ownership.find(std::string(variant->key));
            if (ownershipIt == state.ownership.end())
            {
                CharacterDatabase.DirectExecute(
                    "INSERT IGNORE INTO `native_mount_account_ownership` "
                    "(`account_id`,`mount_key`,`state`,`first_character_guid`,`first_spell_id`,`acquisition_source`) "
                    "VALUES ({},'{}',1,{},{},1)", account, variant->key.data(), guid, variant->spellId);
                state.ownership[std::string(variant->key)] = 1;
                importedKeys.emplace_back(variant->key);
            }

            CharacterDatabase.Execute(
                "INSERT INTO `native_mount_character_grant` "
                "(`character_guid`,`spell_id`,`mount_key`,`state`,`introduced_by_module`) "
                "VALUES ({},{},'{}',3,0) ON DUPLICATE KEY UPDATE `mount_key`=VALUES(`mount_key`),"
                "`state`=3,`introduced_by_module`=0", guid, variant->spellId, variant->key.data());
            state.grants[variant->spellId] = { 3, false };
        }
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

    // A login import is an ownership change too. Publish it after the current
    // character's pass so already-online same-account characters are updated.
    for (std::string const& mountKey : importedKeys)
        ReconcileAccountKey(account, mountKey);
}

void Service::HandleLevelChange(Player* player, std::uint8_t oldLevel)
{
    if (!player || !IsEnabled())
        return;
    Reconcile(player, FindByLevelTransition(oldLevel, player->GetLevel()), false);
}

void Service::HandleSkillChange(Player* player, std::uint32_t skillId,
    std::uint32_t oldValue, std::uint32_t newValue)
{
    if (!player || oldValue == newValue || !IsEnabled())
        return;
    std::vector<MountVariant const*> variants = skillId == SKILL_RIDING
        ? FindByRidingTransition(oldValue, newValue)
        : FindByProfessionTransition(skillId, oldValue, newValue);
    Reconcile(player, variants, false);
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
    QueryResult ownership = CharacterDatabase.Query(
        "SELECT `state` FROM `native_mount_account_ownership` WHERE `account_id`={} AND `mount_key`='{}'",
        account, variant->key.data());
    bool const newOwnership = !ownership;
    if (newOwnership)
    {
        CharacterDatabase.DirectExecute(
            "INSERT IGNORE INTO `native_mount_account_ownership` "
            "(`account_id`,`mount_key`,`state`,`first_character_guid`,`first_spell_id`,`acquisition_source`) "
            "VALUES ({},'{}',1,{},{},2)", account, variant->key.data(),
            player->GetGUID().GetCounter(), spellId);
    }

    CharacterDatabase.Execute(
        "INSERT INTO `native_mount_character_grant` "
        "(`character_guid`,`spell_id`,`mount_key`,`state`,`introduced_by_module`) "
        "VALUES ({},{},'{}',3,0) ON DUPLICATE KEY UPDATE `mount_key`=VALUES(`mount_key`),"
        "`state`=3,`introduced_by_module`=0", player->GetGUID().GetCounter(), spellId, variant->key.data());

    if (newOwnership)
        ReconcileAccountKey(account, variant->key);
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
        // SetSkill removes skill-associated spells before its post-change hook.
        // Classify that core removal as an eligibility revoke, not an external
        // suppression, so a later skill regain can restore the module grant.
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

void Service::ReconcileAccountKey(std::uint32_t accountId, std::string_view mountKey)
{
    std::vector<std::uint32_t> guids;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _onlineByAccount.find(accountId);
        if (it != _onlineByAccount.end())
            guids.assign(it->second.begin(), it->second.end());
    }
    std::vector<MountVariant const*> variants = FindByKey(mountKey);
    for (std::uint32_t guid : guids)
        if (Player* online = ObjectAccessor::FindPlayerByLowGUID(guid))
            Reconcile(online, variants, false);
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
    ReconcileAccountKey(accountId, mountKey);
    return true;
}

void Service::Release(Player* player)
{
    if (!player || !player->GetSession())
        return;
    std::lock_guard<std::mutex> lock(_mutex);
    std::uint32_t const account = player->GetSession()->GetAccountId();
    auto it = _onlineByAccount.find(account);
    if (it == _onlineByAccount.end())
        return;
    it->second.erase(player->GetGUID().GetCounter());
    if (it->second.empty())
        _onlineByAccount.erase(it);
}
}

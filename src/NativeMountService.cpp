#include "NativeMountService.h"

#include "NativeMountCatalog.h"
#include "NativeMountEligibility.h"

#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldSession.h"

#include <string>
#include <unordered_set>

namespace native_mounts
{
namespace
{
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

bool Service::IsModuleGrant(std::uint32_t guid, std::uint32_t spellId) const
{
    return CharacterDatabase.Query(
        "SELECT 1 FROM `native_mount_character_grant` WHERE `character_guid`={} AND `spell_id`={} "
        "AND `state` IN (0,1) AND `introduced_by_module`=1", guid, spellId) != nullptr;
}

void Service::RecordOwnership(Player* player, char const* mountKey, std::uint32_t spellId, std::uint8_t source)
{
    CharacterDatabase.DirectExecute(
        "INSERT IGNORE INTO `native_mount_account_ownership` "
        "(`account_id`,`mount_key`,`state`,`first_character_guid`,`first_spell_id`,`acquisition_source`) "
        "VALUES ({},'{}',1,{},{},{})", player->GetSession()->GetAccountId(), mountKey,
        player->GetGUID().GetCounter(), spellId, static_cast<std::uint32_t>(source));
}

void Service::Reconcile(Player* player)
{
    if (!player || !player->GetSession())
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

    // Existing spells are independent evidence unless an active/pending ledger
    // row proves this module introduced them.
    for (MountVariant const& variant : PocCatalog())
        if (player->HasSpell(variant.spellId) && !IsModuleGrant(guid, variant.spellId))
            RecordOwnership(player, variant.key.data(), variant.spellId, 1);

    std::unordered_set<std::string> owned;
    if (QueryResult rows = CharacterDatabase.Query(
        "SELECT `mount_key` FROM `native_mount_account_ownership` WHERE `account_id`={} AND `state`=1", account))
    {
        do
            owned.insert(rows->Fetch()[0].Get<std::string>());
        while (rows->NextRow());
    }

    for (MountVariant const& variant : PocCatalog())
    {
        bool const accountOwns = owned.find(std::string(variant.key)) != owned.end();
        bool const externallySuppressed = CharacterDatabase.Query(
            "SELECT 1 FROM `native_mount_character_grant` WHERE `character_guid`={} AND `spell_id`={} AND `state`=4",
            guid, variant.spellId) != nullptr;
        bool const eligible = Evaluate(variant, Snapshot(player, variant)) == EligibilityFailure::None;
        bool const moduleGrant = IsModuleGrant(guid, variant.spellId);
        if (accountOwns && eligible && !externallySuppressed && !player->HasSpell(variant.spellId))
        {
            CharacterDatabase.DirectExecute(
                "INSERT INTO `native_mount_character_grant` "
                "(`character_guid`,`spell_id`,`mount_key`,`state`,`introduced_by_module`) "
                "VALUES ({},{},'{}',0,1) ON DUPLICATE KEY UPDATE `mount_key`=VALUES(`mount_key`),"
                "`state`=0,`introduced_by_module`=1", guid, variant.spellId, variant.key.data());
            std::uint64_t const key = OperationKey(player, variant.spellId);
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _granting.insert(key);
            }
            player->learnSpell(variant.spellId, false);
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _granting.erase(key);
            }
            if (player->HasSpell(variant.spellId))
                CharacterDatabase.DirectExecute(
                    "UPDATE `native_mount_character_grant` SET `state`=1 WHERE `character_guid`={} AND `spell_id`={}",
                    guid, variant.spellId);
        }
        else if ((!accountOwns || !eligible) && player->HasSpell(variant.spellId) && moduleGrant &&
            revokeIneligible)
        {
            std::uint64_t const key = OperationKey(player, variant.spellId);
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _revoking.insert(key);
            }
            player->removeSpell(variant.spellId, SPEC_MASK_ALL, false);
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _revoking.erase(key);
            }
            CharacterDatabase.DirectExecute(
                "UPDATE `native_mount_character_grant` SET `state`=2 WHERE `character_guid`={} AND `spell_id`={}",
                guid, variant.spellId);
        }
    }
}

void Service::Update(Player* player, std::uint32_t diff)
{
    if (!player || !IsEnabled())
        return;
    bool due = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::uint32_t& elapsed = _elapsed[player->GetGUID().GetCounter()];
        elapsed += diff;
        if (elapsed >= _config.reconcileIntervalMs)
        {
            elapsed = 0;
            due = true;
        }
    }
    if (due)
        Reconcile(player);
}

void Service::HandleLearn(Player* player, std::uint32_t spellId)
{
    MountVariant const* variant = FindBySpell(spellId);
    if (!variant || !player || !player->GetSession() || !IsEnabled())
        return;
    std::uint64_t const key = OperationKey(player, spellId);
    bool moduleOperation = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        moduleOperation = _granting.find(key) != _granting.end();
    }
    if (moduleOperation)
    {
        CharacterDatabase.DirectExecute(
            "UPDATE `native_mount_character_grant` SET `state`=1 WHERE `character_guid`={} AND `spell_id`={}",
            player->GetGUID().GetCounter(), spellId);
        return;
    }
    RecordOwnership(player, variant->key.data(), spellId, 2);
    // If an independently caused learn event is ever observed after a module
    // row exists, make the row permanently non-revocable.
    CharacterDatabase.DirectExecute(
        "INSERT INTO `native_mount_character_grant` (`character_guid`,`spell_id`,`mount_key`,`state`,`introduced_by_module`) "
        "VALUES ({},{},'{}',3,0) ON DUPLICATE KEY UPDATE `state`=3,`introduced_by_module`=0",
        player->GetGUID().GetCounter(), spellId, variant->key.data());
}

void Service::HandleForget(Player* player, std::uint32_t spellId)
{
    if (!FindBySpell(spellId) || !player || !IsEnabled())
        return;
    std::uint64_t const key = OperationKey(player, spellId);
    bool moduleOperation = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        moduleOperation = _revoking.find(key) != _revoking.end();
    }
    if (moduleOperation)
        return;
    // An external unlearn does not erase account ownership. The suppression
    // tombstone prevents an automatic grant/unlearn loop; an administrator can
    // clear it deliberately after diagnosing the external removal.
    CharacterDatabase.DirectExecute(
        "INSERT INTO `native_mount_character_grant` (`character_guid`,`spell_id`,`mount_key`,`state`,`introduced_by_module`) "
        "VALUES ({},{},'external-unlearn',4,0) ON DUPLICATE KEY UPDATE `state`=4,`introduced_by_module`=0",
        player->GetGUID().GetCounter(), spellId);
}

void Service::Release(Player* player)
{
    if (!player)
        return;
    std::lock_guard<std::mutex> lock(_mutex);
    _elapsed.erase(player->GetGUID().GetCounter());
}
}

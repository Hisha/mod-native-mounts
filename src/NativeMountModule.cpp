#include "NativeMountService.h"

#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "Player.h"
#include "ScriptMgr.h"

#include <string>
#include <utility>

using namespace Acore::ChatCommands;

namespace
{
native_mounts::ServiceConfig config;

class NativeMountWorldScript final : public WorldScript
{
public:
    NativeMountWorldScript() : WorldScript("NativeMountWorldScript",
        { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP }) { }

    void OnAfterConfigLoad(bool) override
    {
        config.enabled = sConfigMgr->GetOption<bool>("NativeMounts.Poc.Enable", false);
        config.revokeIneligibleModuleGrants = sConfigMgr->GetOption<bool>(
            "NativeMounts.Poc.RevokeIneligibleModuleGrants", true);
        native_mounts::Service::Instance().Configure(config);
    }

    void OnStartup() override
    {
        native_mounts::Service::Instance().Initialize();
    }
};

class NativeMountPlayerScript final : public PlayerScript
{
public:
    NativeMountPlayerScript() : PlayerScript("NativeMountPlayerScript",
        { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_BEFORE_LOGOUT, PLAYERHOOK_ON_LEVEL_CHANGED,
          PLAYERHOOK_ON_LEARN_SPELL, PLAYERHOOK_ON_FORGOT_SPELL, PLAYERHOOK_ON_UPDATE_SKILL,
          PLAYERHOOK_ON_SET_SKILL, PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT }) { }

    using PlayerScript::OnPlayerCanUseChat;

    void OnPlayerLogin(Player* player) override { native_mounts::Service::Instance().ReconcileLogin(player); }
    void OnPlayerBeforeLogout(Player* player) override { native_mounts::Service::Instance().Release(player); }
    void OnPlayerLevelChanged(Player* player, std::uint8_t oldLevel) override
    {
        native_mounts::Service::Instance().HandleLevelChange(player, oldLevel);
    }
    void OnPlayerLearnSpell(Player* player, std::uint32_t spellId) override
    {
        native_mounts::Service::Instance().HandleLearn(player, spellId);
    }
    void OnPlayerForgotSpell(Player* player, std::uint32_t spellId) override
    {
        native_mounts::Service::Instance().HandleForget(player, spellId);
    }
    void OnPlayerUpdateSkill(Player* player, std::uint32_t skillId, std::uint32_t value,
        std::uint32_t, std::uint32_t, std::uint32_t newValue) override
    {
        native_mounts::Service::Instance().HandleSkillChange(player, skillId, value, newValue);
    }
    void OnPlayerSetSkill(Player* player, std::uint32_t skillId, std::uint32_t value,
        std::uint32_t, std::uint32_t, std::uint32_t newValue) override
    {
        native_mounts::Service::Instance().HandleSkillChange(player, skillId, value, newValue);
    }
    bool OnPlayerCanUseChat(Player* player, std::uint32_t type, std::uint32_t language,
        std::string& message, Player* receiver) override
    {
        if (type != CHAT_MSG_WHISPER || language != LANG_ADDON || !player || receiver != player ||
            message.rfind("NMOUNTS\t", 0) != 0)
            return true;
        if (message.rfind("NMOUNTS\t1\tRESET", 0) == 0 || message.rfind("NMOUNTS\t1\tOWN\t", 0) == 0 ||
            message.rfind("NMOUNTS\t1\tEND", 0) == 0)
            return true;
        native_mounts::Service::Instance().HandleAddonWhisper(player, message);
        return false;
    }
};

class NativeMountCommandScript final : public CommandScript
{
public:
    NativeMountCommandScript() : CommandScript("NativeMountCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable ownership = {
            { "grant", Grant, rbac::RBAC_PERM_COMMAND_LEARN, Console::Yes },
            { "revoke", Revoke, rbac::RBAC_PERM_COMMAND_UNLEARN, Console::Yes }
        };
        static ChatCommandTable commands = { { "ownership", ownership } };
        static ChatCommandTable root = { { "native-mounts", commands } };
        return root;
    }

private:
    static bool Change(ChatHandler* handler, std::uint32_t accountId, std::string mountKey, bool owned)
    {
        if (!native_mounts::Service::Instance().SetAccountOwnership(accountId, mountKey, owned))
        {
            handler->PSendSysMessage("Unknown mount key or native mounts is unavailable: {}", mountKey);
            return false;
        }
        handler->PSendSysMessage("Account {} mount {} is now {}.", accountId, mountKey,
            owned ? "owned" : "administratively revoked");
        return true;
    }

    static bool Grant(ChatHandler* handler, std::uint32_t accountId, std::string mountKey)
    {
        return Change(handler, accountId, std::move(mountKey), true);
    }

    static bool Revoke(ChatHandler* handler, std::uint32_t accountId, std::string mountKey)
    {
        return Change(handler, accountId, std::move(mountKey), false);
    }
};
}

void AddNativeMountModuleScripts()
{
    new NativeMountWorldScript();
    new NativeMountPlayerScript();
    new NativeMountCommandScript();
}

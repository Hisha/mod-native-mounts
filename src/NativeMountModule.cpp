#include "NativeMountService.h"

#include "Config.h"
#include "Player.h"
#include "ScriptMgr.h"

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
        config.reconcileIntervalMs = sConfigMgr->GetOption<std::uint32_t>(
            "NativeMounts.Poc.ReconcileIntervalMs", 5000);
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
        { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_UPDATE, PLAYERHOOK_ON_BEFORE_LOGOUT,
          PLAYERHOOK_ON_LEVEL_CHANGED, PLAYERHOOK_ON_LEARN_SPELL, PLAYERHOOK_ON_FORGOT_SPELL }) { }

    void OnPlayerLogin(Player* player) override { native_mounts::Service::Instance().Reconcile(player); }
    void OnPlayerUpdate(Player* player, std::uint32_t diff) override
    {
        native_mounts::Service::Instance().Update(player, diff);
    }
    void OnPlayerBeforeLogout(Player* player) override { native_mounts::Service::Instance().Release(player); }
    void OnPlayerLevelChanged(Player* player, std::uint8_t) override
    {
        native_mounts::Service::Instance().Reconcile(player);
    }
    void OnPlayerLearnSpell(Player* player, std::uint32_t spellId) override
    {
        native_mounts::Service::Instance().HandleLearn(player, spellId);
    }
    void OnPlayerForgotSpell(Player* player, std::uint32_t spellId) override
    {
        native_mounts::Service::Instance().HandleForget(player, spellId);
    }
};
}

void AddNativeMountModuleScripts()
{
    new NativeMountWorldScript();
    new NativeMountPlayerScript();
}

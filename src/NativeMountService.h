#ifndef MOD_NATIVE_MOUNTS_SERVICE_H
#define MOD_NATIVE_MOUNTS_SERVICE_H

#include <cstdint>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Player;

namespace native_mounts { struct MountVariant; }

namespace native_mounts
{
struct ServiceConfig
{
    bool enabled = false;
    bool revokeIneligibleModuleGrants = true;
};

class Service
{
public:
    static Service& Instance();
    void Configure(ServiceConfig const& config);
    bool Initialize();
    bool IsEnabled() const;
    void ReconcileLogin(Player* player);
    void HandleLevelChange(Player* player, std::uint8_t oldLevel);
    void HandleSkillChange(Player* player, std::uint32_t skillId,
        std::uint32_t oldValue, std::uint32_t newValue);
    void HandleLearn(Player* player, std::uint32_t spellId);
    void HandleForget(Player* player, std::uint32_t spellId);
    void HandleAddonWhisper(Player* player, std::string_view message);
    void Release(Player* player);
    bool SetAccountOwnership(std::uint32_t accountId, std::string_view mountKey, bool owned);

private:
    Service() = default;
    std::uint64_t OperationKey(Player const* player, std::uint32_t spellId) const;
    void Reconcile(Player* player, std::vector<MountVariant const*> const& variants, bool importKnownSpells);
    void ReconcileAccountKey(std::uint32_t accountId, std::string_view mountKey);
    void Register(Player* player);
    void SendOwnershipSnapshot(Player* player);
    void SendOwnershipSnapshot(std::uint32_t accountId);

    mutable std::mutex _mutex;
    ServiceConfig _config;
    bool _databaseAvailable = false;
    std::unordered_set<std::uint64_t> _granting;
    std::unordered_set<std::uint64_t> _revoking;
    std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>> _onlineByAccount;
};
}

#endif

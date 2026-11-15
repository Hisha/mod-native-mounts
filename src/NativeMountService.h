#ifndef MOD_NATIVE_MOUNTS_SERVICE_H
#define MOD_NATIVE_MOUNTS_SERVICE_H

#include <cstdint>
#include <mutex>
#include <string_view>
#include <unordered_set>

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
    void HandleLearn(Player* player, std::uint32_t spellId);
    void HandleForget(Player* player, std::uint32_t spellId);
    void HandleAddonWhisper(Player* player, std::string_view message);
    bool SetAccountOwnership(std::uint32_t accountId, std::string_view mountKey, bool owned);

private:
    Service() = default;
    std::uint64_t OperationKey(Player const* player, std::uint32_t spellId) const;
    void ReconcileCollections(Player* player, std::uint32_t requestId);

    mutable std::mutex _mutex;
    ServiceConfig _config;
    bool _databaseAvailable = false;
    std::unordered_set<std::uint64_t> _granting;
    std::unordered_set<std::uint64_t> _revoking;
};
}

#endif

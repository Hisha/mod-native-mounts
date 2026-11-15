#ifndef MOD_NATIVE_MOUNTS_SERVICE_H
#define MOD_NATIVE_MOUNTS_SERVICE_H

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

class Player;

namespace native_mounts
{
struct ServiceConfig
{
    bool enabled = false;
    bool revokeIneligibleModuleGrants = true;
    std::uint32_t reconcileIntervalMs = 5000;
};

class Service
{
public:
    static Service& Instance();
    void Configure(ServiceConfig const& config);
    bool Initialize();
    bool IsEnabled() const;
    void Reconcile(Player* player);
    void Update(Player* player, std::uint32_t diff);
    void HandleLearn(Player* player, std::uint32_t spellId);
    void HandleForget(Player* player, std::uint32_t spellId);
    void Release(Player* player);

private:
    Service() = default;
    std::uint64_t OperationKey(Player const* player, std::uint32_t spellId) const;
    bool IsModuleGrant(std::uint32_t guid, std::uint32_t spellId) const;
    void RecordOwnership(Player* player, char const* mountKey, std::uint32_t spellId, std::uint8_t source);

    mutable std::mutex _mutex;
    ServiceConfig _config;
    bool _databaseAvailable = false;
    std::unordered_set<std::uint64_t> _granting;
    std::unordered_set<std::uint64_t> _revoking;
    std::unordered_map<std::uint32_t, std::uint32_t> _elapsed;
};
}

#endif

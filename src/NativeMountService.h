#ifndef MOD_NATIVE_MOUNTS_SERVICE_H
#define MOD_NATIVE_MOUNTS_SERVICE_H

#include <cstdint>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

class Player;
enum SpellCastResult : std::uint8_t;

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
    bool IsRandomMountSpell(std::uint32_t spellId) const;
    SpellCastResult PrepareRandomMount(Player* player, bool strict);
    void CastPreparedRandomMount(Player* player);
    void CancelPreparedRandomMount(Player* player);

private:
    Service() = default;
    std::uint64_t OperationKey(Player const* player, std::uint32_t spellId) const;
    void ReconcileCollections(Player* player, std::uint32_t requestId);
    bool ResolveRandomMountSpell();

    mutable std::mutex _mutex;
    ServiceConfig _config;
    bool _databaseAvailable = false;
    std::uint32_t _randomMountSpell = 0;
    std::unordered_set<std::uint64_t> _granting;
    std::unordered_set<std::uint64_t> _revoking;
    std::unordered_map<std::uint32_t, std::uint32_t> _preparedRandomMounts;
};
}

#endif

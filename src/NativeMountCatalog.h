#ifndef MOD_NATIVE_MOUNTS_CATALOG_H
#define MOD_NATIVE_MOUNTS_CATALOG_H

#include <cstdint>
#include <string_view>
#include <vector>

namespace native_mounts
{
struct MountVariant
{
    std::string_view key;
    std::uint32_t spellId;
    std::uint8_t minimumLevel;
    std::uint16_t minimumRiding;
    std::uint32_t allowedRaceMask;
    std::uint32_t allowedClassMask;
    std::uint16_t professionSkill;
    std::uint16_t professionRank;
};

std::vector<MountVariant> const& PocCatalog();
MountVariant const* FindBySpell(std::uint32_t spellId);
std::vector<MountVariant const*> FindByKey(std::string_view key);
std::vector<MountVariant const*> FindByLevelTransition(std::uint8_t oldLevel, std::uint8_t newLevel);
std::vector<MountVariant const*> FindByRidingTransition(std::uint16_t oldValue, std::uint16_t newValue);
std::vector<MountVariant const*> FindByProfessionTransition(
    std::uint16_t skillId, std::uint16_t oldValue, std::uint16_t newValue);
}

#endif

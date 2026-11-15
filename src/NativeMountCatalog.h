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
    std::string_view name;
    std::uint32_t iconId;
    std::uint32_t previewCreatureId;
    std::uint8_t minimumLevel;
    std::uint16_t minimumRiding;
    std::uint32_t allowedRaceMask;
    std::uint32_t allowedClassMask;
    std::uint16_t professionSkill;
    std::uint16_t professionRank;
    std::uint32_t sourceItemId;
};

std::vector<MountVariant> const& Catalog();
MountVariant const* FindBySpell(std::uint32_t spellId);
std::vector<MountVariant const*> FindByKey(std::string_view key);
}

#endif

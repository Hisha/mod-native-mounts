#include "NativeMountCatalog.h"

namespace native_mounts
{
std::vector<MountVariant> const& Catalog()
{
    static std::vector<MountVariant> const catalog = {
#include "../generated/NativeMountCatalog.inc"
    };
    return catalog;
}

MountVariant const* FindBySpell(std::uint32_t spellId)
{
    for (MountVariant const& variant : Catalog())
        if (variant.spellId == spellId)
            return &variant;
    return nullptr;
}

std::vector<MountVariant const*> FindByKey(std::string_view key)
{
    std::vector<MountVariant const*> result;
    for (MountVariant const& variant : Catalog())
        if (variant.key == key)
            result.push_back(&variant);
    return result;
}

namespace
{
bool Crossed(std::uint32_t requirement, std::uint32_t oldValue, std::uint32_t newValue)
{
    return requirement && (oldValue < requirement) != (newValue < requirement);
}
}

std::vector<MountVariant const*> FindByLevelTransition(std::uint8_t oldLevel, std::uint8_t newLevel)
{
    std::vector<MountVariant const*> result;
    for (MountVariant const& variant : Catalog())
        if (Crossed(variant.minimumLevel, oldLevel, newLevel))
            result.push_back(&variant);
    return result;
}

std::vector<MountVariant const*> FindByRidingTransition(std::uint16_t oldValue, std::uint16_t newValue)
{
    std::vector<MountVariant const*> result;
    for (MountVariant const& variant : Catalog())
        if (Crossed(variant.minimumRiding, oldValue, newValue))
            result.push_back(&variant);
    return result;
}

std::vector<MountVariant const*> FindByProfessionTransition(
    std::uint16_t skillId, std::uint16_t oldValue, std::uint16_t newValue)
{
    std::vector<MountVariant const*> result;
    for (MountVariant const& variant : Catalog())
        if (variant.professionSkill == skillId && Crossed(variant.professionRank, oldValue, newValue))
            result.push_back(&variant);
    return result;
}
}

#include "NativeMountCatalog.h"

namespace native_mounts
{
std::vector<MountVariant> const& PocCatalog()
{
    // Requirements reproduce the relevant acquisition gates from build-12340
    // item/trainer data. Race/class masks use the standard (id - 1) bit.
    static std::vector<MountVariant> const catalog = {
        { "big-blizzard-bear", 58983, 20, 75, 0x7FFF, 0x3FFFF, 0, 0 },
        { "azure-drake", 59567, 70, 300, 0x7FFF, 0x3FFFF, 0, 0 },
        { "warlock-dreadsteed", 23161, 40, 150, 0x7FFF, 1U << (9U - 1U), 0, 0 },
        { "turbo-flying-machine", 44151, 70, 300, 0x7FFF, 0x3FFFF, 202, 375 },
        { "tawny-wind-rider", 32243, 60, 225, 690, 0x3FFFF, 0, 0 },
    };
    return catalog;
}

MountVariant const* FindBySpell(std::uint32_t spellId)
{
    for (MountVariant const& variant : PocCatalog())
        if (variant.spellId == spellId)
            return &variant;
    return nullptr;
}

std::vector<MountVariant const*> FindByKey(std::string_view key)
{
    std::vector<MountVariant const*> result;
    for (MountVariant const& variant : PocCatalog())
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
    for (MountVariant const& variant : PocCatalog())
        if (Crossed(variant.minimumLevel, oldLevel, newLevel))
            result.push_back(&variant);
    return result;
}

std::vector<MountVariant const*> FindByRidingTransition(std::uint16_t oldValue, std::uint16_t newValue)
{
    std::vector<MountVariant const*> result;
    for (MountVariant const& variant : PocCatalog())
        if (Crossed(variant.minimumRiding, oldValue, newValue))
            result.push_back(&variant);
    return result;
}

std::vector<MountVariant const*> FindByProfessionTransition(
    std::uint16_t skillId, std::uint16_t oldValue, std::uint16_t newValue)
{
    std::vector<MountVariant const*> result;
    for (MountVariant const& variant : PocCatalog())
        if (variant.professionSkill == skillId && Crossed(variant.professionRank, oldValue, newValue))
            result.push_back(&variant);
    return result;
}
}

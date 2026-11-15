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

}

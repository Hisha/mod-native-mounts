#ifndef MOD_NATIVE_MOUNTS_ELIGIBILITY_H
#define MOD_NATIVE_MOUNTS_ELIGIBILITY_H

#include "NativeMountCatalog.h"

#include <cstdint>

namespace native_mounts
{
struct CharacterEligibility
{
    std::uint8_t level = 1;
    std::uint8_t race = 0;
    std::uint8_t characterClass = 0;
    std::uint16_t riding = 0;
    std::uint16_t professionSkill = 0;
    std::uint16_t professionRank = 0;
};

enum class EligibilityFailure : std::uint8_t
{
    None,
    Level,
    Riding,
    Race,
    Class,
    Profession
};

EligibilityFailure Evaluate(MountVariant const& variant, CharacterEligibility const& character);

enum class ReconcileAction : std::uint8_t
{
    None,
    Grant,
    Revoke
};

ReconcileAction DecideReconcileAction(bool accountOwns, bool eligible, bool hasSpell,
    bool externallySuppressed, bool moduleGrant, bool revokeIneligibleModuleGrants);
}

#endif

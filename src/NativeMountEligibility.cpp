#include "NativeMountEligibility.h"

namespace native_mounts
{
namespace
{
bool MaskContains(std::uint32_t mask, std::uint8_t id)
{
    return id > 0 && id <= 32 && (mask & (1U << (id - 1U))) != 0;
}
}

EligibilityFailure Evaluate(MountVariant const& variant, CharacterEligibility const& character)
{
    if (character.level < variant.minimumLevel)
        return EligibilityFailure::Level;
    if (character.riding < variant.minimumRiding)
        return EligibilityFailure::Riding;
    if (!MaskContains(variant.allowedRaceMask, character.race))
        return EligibilityFailure::Race;
    if (!MaskContains(variant.allowedClassMask, character.characterClass))
        return EligibilityFailure::Class;
    if (variant.professionSkill &&
        (character.professionSkill != variant.professionSkill || character.professionRank < variant.professionRank))
        return EligibilityFailure::Profession;
    return EligibilityFailure::None;
}

ReconcileAction DecideReconcileAction(bool accountOwns, bool eligible, bool hasSpell,
    bool externallySuppressed, bool moduleGrant, bool revokeIneligibleModuleGrants)
{
    if (accountOwns && eligible && !externallySuppressed && !hasSpell)
        return ReconcileAction::Grant;
    if ((!accountOwns || !eligible) && hasSpell && moduleGrant && revokeIneligibleModuleGrants)
        return ReconcileAction::Revoke;
    return ReconcileAction::None;
}
}

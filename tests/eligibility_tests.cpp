#include "NativeMountCatalog.h"
#include "NativeMountEligibility.h"

#include <cstdlib>
#include <iostream>

using namespace native_mounts;

namespace
{
void Expect(bool value, char const* message)
{
    if (!value)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

CharacterEligibility EligibleBase()
{
    CharacterEligibility state;
    state.level = 80;
    state.race = 1; // human
    state.characterClass = 2; // paladin
    state.riding = 300;
    return state;
}
}

int main()
{
    Expect(Catalog().size() > 200, "generated catalog contains the complete stock collection");

    CharacterEligibility state = EligibleBase();
    Expect(Evaluate(*FindBySpell(58983), state) == EligibilityFailure::None,
        "ordinary ground mount is eligible");
    state.riding = 74;
    Expect(Evaluate(*FindBySpell(58983), state) == EligibilityFailure::Riding,
        "ground mount retains riding gate");

    state = EligibleBase();
    state.level = 69;
    Expect(Evaluate(*FindBySpell(59567), state) == EligibilityFailure::Level,
        "flying mount retains level gate");

    state = EligibleBase();
    Expect(Evaluate(*FindBySpell(23161), state) == EligibilityFailure::Class,
        "paladin cannot receive Dreadsteed");
    state.characterClass = 9;
    Expect(Evaluate(*FindBySpell(23161), state) == EligibilityFailure::None,
        "eligible warlock can receive Dreadsteed");

    state = EligibleBase();
    state.professionSkill = 202;
    state.professionRank = 374;
    Expect(Evaluate(*FindBySpell(44151), state) == EligibilityFailure::Profession,
        "Turbo Flying Machine requires Engineering 375");
    state.professionRank = 375;
    Expect(Evaluate(*FindBySpell(44151), state) == EligibilityFailure::None,
        "Engineer at 375 is eligible for Turbo Flying Machine");

    state = EligibleBase();
    Expect(Evaluate(*FindBySpell(32243), state) == EligibilityFailure::Race,
        "Alliance race cannot receive Tawny Wind Rider");
    state.race = 2; // orc; bit is present in source item's 690 mask
    Expect(Evaluate(*FindBySpell(32243), state) == EligibilityFailure::None,
        "Horde race can receive Tawny Wind Rider");

    Expect(FindByLevelTransition(69, 70).size() >= 2,
        "level threshold reconciliation includes the level-70 regression mounts");
    Expect(FindByLevelTransition(70, 69).size() >= 2,
        "level decreases cross the same targeted thresholds");
    Expect(FindByLevelTransition(71, 72).empty(),
        "unrelated level changes produce no reconciliation work");
    Expect(FindByRidingTransition(299, 300).size() >= 2,
        "riding 300 includes the regression mounts gated at that rank");
    Expect(FindByRidingTransition(300, 299).size() >= 2,
        "riding decreases select affected mounts");
    Expect(!FindByProfessionTransition(202, 374, 375).empty(),
        "Engineering 375 includes Turbo Flying Machine");
    Expect(!FindByProfessionTransition(202, 375, 0).empty(),
        "unlearning Engineering includes Turbo Flying Machine");
    Expect(FindByProfessionTransition(197, 374, 375).empty(),
        "unrelated professions produce no reconciliation work");

    Expect(DecideReconcileAction(true, true, false, false, false, true) == ReconcileAction::Grant,
        "login/acquisition grants an owned eligible missing spell");
    Expect(DecideReconcileAction(true, true, false, true, false, true) == ReconcileAction::None,
        "external-unlearn suppression prevents regrant");
    Expect(DecideReconcileAction(false, true, true, false, true, true) == ReconcileAction::Revoke,
        "administrative ownership revoke removes a proven module grant");
    Expect(DecideReconcileAction(true, false, true, false, true, true) == ReconcileAction::Revoke,
        "eligibility loss removes a proven module grant");
    Expect(DecideReconcileAction(false, false, true, false, false, true) == ReconcileAction::None,
        "independently learned spells survive ownership and eligibility loss");

    std::cout << "native mounts eligibility tests passed\n";
}

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
    Expect(PocCatalog().size() == 5, "PoC catalog remains intentionally bounded");

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

    std::cout << "native mounts eligibility tests passed\n";
}

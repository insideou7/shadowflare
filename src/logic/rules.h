#pragma once

namespace sf {

// Where the engine departs from the original game on purpose, mostly to fix
// the original's bugs. Original mode turns every one of them off and plays
// as ShadowFlare.exe does, bugs included. Our fixes are on by default.
// (Data the engine used to misread is not listed: that is corrected in both
// modes.)
struct Rules {
    // The original charges Magic Shield and Counter Burst, each time they
    // soak up a blow, the MP cost of whichever spell is selected, at the
    // shield's level (0x443cb0), rather than the shield's own cost.
    bool shieldsCostTheirOwnMp = true;
    // The partner's status window prints its defence on the "M Evasion
    // Rate" line (0x4319de, 0x450ace).
    bool partnerStatusShowsMagicEvasion = true;
    // The companion's Explosion (the hero's spell 20) reads its stun from
    // the hero's level in spell 21, Elemental Strike (0x461c40 asks
    // 0x451e60 for 0x15), rather than Explosion's own level.
    bool explosionUsesItsOwnLevel = true;
    // A drop list rolled more than once forgets which qualities the last
    // roll allowed: the original never clears them (0x417550), so a roll
    // allowing magic items lets every later roll of that monster give one.
    bool dropQualityPerRoll = true;

    bool originalMode() const {
        return !shieldsCostTheirOwnMp && !partnerStatusShowsMagicEvasion && !explosionUsesItsOwnLevel &&
               !dropQualityPerRoll;
    }
    static Rules ours() { return {}; }
    static Rules original() {
        Rules r;
        r.shieldsCostTheirOwnMp = false;
        r.partnerStatusShowsMagicEvasion = false;
        r.explosionUsesItsOwnLevel = false;
        r.dropQualityPerRoll = false;
        return r;
    }
};

}  // namespace sf

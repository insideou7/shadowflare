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
    // SETQUESTFLAG clears a mission only once in a game session (0x433f29
    // keeps a second array at 0x48cebc that is never saved). The first
    // clear is spent even on a mission not yet taken, so a mission whose
    // clearing script runs before it is given stays taken for good once
    // it is given, until the game is loaded again. Ours clears a taken
    // mission every time.
    bool missionsClearOnlyWhenTaken = true;
    // A hurt monster shows a small health bar over its head. The original
    // shows a monster's health only in its name under the pointer
    // (0x40ee70).
    bool healthBarsOverMonsters = true;
    // The scroll wheel zooms the world out, to half size (Settings::zoom).
    // The original always shows it at 640 x 480.
    bool zoomOut = true;
    // A chest, lever, door or item under the pointer gets a light outline,
    // so what can be used stands out. The original only draws it a little
    // brighter (0x40ee70), which both modes do.
    bool outlineUnderPointer = true;
    // Right clicking an item in the bag while a shop is open sells it. In
    // the original a right click there only uses a consumable, as it does
    // anywhere (0x44a6f0); to sell, the item is picked up and put on the
    // shop.
    bool rightClickSells = true;
    // Gold put in the Warehouse Transfer Box that exactly fills the
    // warehouse's gold stacks leaves a Gold item of 0 behind, which the
    // original puts down in the warehouse (0x446320). Ours drops it.
    bool noEmptyGoldInWarehouse = true;
    // Holding Alt, or after L turns them on, every item on the ground shows
    // its name over it, and a click on a name goes for that item. The
    // original names only the item under the pointer (0x40ee70); it reads
    // Alt with its other keys but does nothing with it.
    bool groundItemNames = true;

    bool originalMode() const {
        return !shieldsCostTheirOwnMp && !partnerStatusShowsMagicEvasion && !explosionUsesItsOwnLevel &&
               !dropQualityPerRoll && !missionsClearOnlyWhenTaken && !healthBarsOverMonsters && !zoomOut &&
               !outlineUnderPointer && !rightClickSells && !noEmptyGoldInWarehouse &&
               !groundItemNames;
    }
    static Rules ours() { return {}; }
    static Rules original() {
        Rules r;
        r.shieldsCostTheirOwnMp = false;
        r.partnerStatusShowsMagicEvasion = false;
        r.explosionUsesItsOwnLevel = false;
        r.dropQualityPerRoll = false;
        r.missionsClearOnlyWhenTaken = false;
        r.healthBarsOverMonsters = false;
        r.zoomOut = false;
        r.outlineUnderPointer = false;
        r.rightClickSells = false;
        r.noEmptyGoldInWarehouse = false;
        r.groundItemNames = false;
        return r;
    }
};

}  // namespace sf

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

    bool originalMode() const { return !shieldsCostTheirOwnMp; }
    static Rules ours() { return {}; }
    static Rules original() {
        Rules r;
        r.shieldsCostTheirOwnMp = false;
        return r;
    }
};

}  // namespace sf

#pragma once

#include <array>

#include "logic/ai_select.h"

namespace sf {

// The steam on the title screen, as ShadowFlare.exe plays it: ten jets
// (System/Title/Pattern/Smoke00-09, one chart each, drawn facing 8) blow
// out of the pipes on the title picture. The title's setup (0x420d88)
// starts jet i at tick rand() % 90; its loop (0x421836) shows frame
// tick - start while that is a frame of the jet's chart, and on the last
// frame sets the next start to tick + rand() % 100 + frames + 30. Ticks are
// the original's, 30 a second.
class TitleSmoke {
public:
    static constexpr int kJets = 10;
    struct Point {
        int x, y;
    };
    // Where each jet is drawn, in screen pixels (0x420e7f): the mouth of
    // its pipe. The even ones blow to the right, the odd ones to the left.
    static constexpr std::array<Point, kJets> kPositions{{
        {222, 282}, {535, 208}, {122, 472}, {562, 60}, {62, 54},
        {547, 350}, {53, 377}, {566, 294}, {113, 473}, {420, 463},
    }};

    explicit TitleSmoke(Random& random);

    // One of the original's ticks. `frames` is how many frames each jet's
    // chart has (0 when it has none).
    void step(const std::array<int, kJets>& frames, Random& random);

    // The frame jet i shows now, or -1 while it rests.
    int frame(int jet, int frames) const;

    int tick() const { return tick_; }

private:
    int tick_ = 0;
    std::array<int, kJets> start_{};
};

}  // namespace sf

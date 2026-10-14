#include "logic/title_smoke.h"

namespace sf {

TitleSmoke::TitleSmoke(Random& random) {
    for (int& start : start_) start = random.next(90);
}

void TitleSmoke::step(const std::array<int, kJets>& frames, Random& random) {
    for (int i = 0; i < kJets; ++i) {
        // The loop draws first, then restarts a jet on its last frame.
        if (frames[i] > 0 && tick_ == start_[i] + frames[i] - 1) start_[i] = random.next(100) + tick_ + frames[i] + 30;
    }
    ++tick_;
}

int TitleSmoke::frame(int jet, int frames) const {
    const int f = tick_ - start_[jet];
    return f >= 0 && f < frames ? f : -1;
}

}  // namespace sf

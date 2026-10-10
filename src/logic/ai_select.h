#pragma once

#include <cstdint>

#include "formats/ai_control.h"

namespace sf {

// Deterministic random numbers, so runs can be replayed.
class Random {
public:
    explicit Random(std::uint32_t seed = 12345) : state_(seed) {}
    int next(int bound);  // 0 <= result < bound; 0 when bound <= 0

private:
    std::uint32_t state_;
};

// What the selection needs to know about the monster right now.
struct AiSituation {
    double gapToTarget = 0;  // between collision boxes, world units; < 0 = no target
    int healthPercent = 100;
};

// Picks the entry a monster acts on for an event, as ShadowFlare.exe does
// (0x45c9f0): entries whose conditions hold are collected, a higher
// priority (param[0]) throws away what was collected before it, and one
// is drawn at random weighted by param[2]. Returns null when none applies.
const AiControl::Entry* selectAiEntry(const AiControl::List& list, int event, const AiSituation& situation, Random& random);

// Whether an entry's distance condition holds (true when it has none).
bool targetInRange(const AiControl::Entry& entry, double gap);

}  // namespace sf

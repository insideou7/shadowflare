#include "logic/ai_select.h"

#include <vector>

namespace sf {

int Random::next(int bound) {
    state_ = state_ * 1103515245u + 12345u;
    return bound > 0 ? static_cast<int>((state_ >> 16) % static_cast<std::uint32_t>(bound)) : 0;
}

bool targetInRange(const AiControl::Entry& entry, double gap) {
    const auto& cond = entry.cond;
    if (cond[3] != 1) return true;
    if (gap < 0) return false;
    return (cond[4] == -1 || gap >= cond[4]) && (cond[5] == -1 || gap <= cond[5]);
}

const AiControl::Entry* selectAiEntry(const AiControl::List& list, int event, const AiSituation& situation, Random& random) {
    if (event < 0 || static_cast<std::size_t>(event) >= list.events.size()) return nullptr;
    std::vector<const AiControl::Entry*> candidates;
    int best = -1;
    for (const auto& e : list.events[static_cast<std::size_t>(event)]) {
        bool ok = targetInRange(e, situation.gapToTarget);
        if (e.cond[0] == 1) {
            if (e.cond[1] != -1 && e.cond[1] > situation.healthPercent) ok = false;
            if (e.cond[2] != -1 && e.cond[2] < situation.healthPercent) ok = false;
        }
        if (!ok) continue;
        // As in the original, a higher priority throws away what was
        // collected so far, but later lower ones are still added.
        if (best < e.param[0]) {
            candidates.clear();
            best = e.param[0];
        }
        candidates.push_back(&e);
    }
    int total = 0;
    for (const auto* e : candidates) total += e->param[2];
    if (total <= 0) return nullptr;
    int pick = random.next(total);
    for (const auto* e : candidates) {
        pick -= e->param[2];
        if (pick < 0) return e;
    }
    return nullptr;
}

}  // namespace sf

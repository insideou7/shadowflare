#include "logic/combat.h"

#include <algorithm>
#include <array>

namespace sf {

WarriorMove warriorMove(int weaponSubtype) {
    switch (weaponSubtype) {
        case 0: return WarriorMove::Sword;
        case 1: return WarriorMove::Spin;
        case 3: return WarriorMove::GreatSword;
        default: return WarriorMove::None;
    }
}

int comboChart(WarriorMove move, int blow) {
    static constexpr std::array<int, 3> kSword{5, 7, 8}, kGreatSword{15, 17, 18};
    const auto i = static_cast<std::size_t>(std::clamp(blow, 1, 3) - 1);
    return move == WarriorMove::GreatSword ? kGreatSword[i] : kSword[i];
}

int comboShout(int blow, bool heroine) { return (heroine ? 0x63 : 0x60) + std::clamp(blow, 1, 3) - 1; }

int attackSpeedStep(int table4, bool powerMode) { return powerMode ? 9 : std::clamp(table4 + 1, 0, 9); }

double comboSpeed(int step) {
    static constexpr double kSteps[10] = {0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7};
    return kSteps[std::clamp(step, 0, 9)];
}

double spinSpeed(int step) {
    static constexpr double kSteps[10] = {0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.4, 1.5};
    return kSteps[std::clamp(step, 0, 9)];
}

int comboLunge(int n) { return n >= 0 && n * 10 <= 60 ? n * 10 : 0; }

SpinFrame spinFrame(int t, int direction, int windUp, int turn, int recover) {
    // Where in its turn chart 22 starts for each facing (the table 0x437910
    // builds on the stack), so the turn picks up from the wind-up.
    static constexpr int kTurnStart[8] = {8, 0, 1, 2, 3, 4, 5, 6};
    SpinFrame f;
    turn = std::max(1, turn);
    if (t < windUp) {
        f.chart = 21;
        f.frame = std::max(0, t);
    } else if (t < windUp + 3 * turn) {
        f.chart = 22;
        f.frame = (kTurnStart[std::clamp(direction, 0, 7)] - windUp + t) % turn;
    } else {
        f.chart = 23;
        f.frame = std::min(t - 3 * turn - windUp, recover - 1);
    }
    // The blows and the march run from the turn's first frame to one past
    // its last.
    f.turning = t >= windUp && t <= windUp + 3 * turn;
    f.done = t >= windUp + 3 * turn + recover - 1;
    return f;
}

bool inSwing(int heroDirection, int toMonster, int spread) {
    const int d = ((toMonster - heroDirection) % 8 + 8) % 8;
    return std::min(d, 8 - d) <= spread;
}

}  // namespace sf

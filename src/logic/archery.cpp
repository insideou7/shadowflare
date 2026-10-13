#include "logic/archery.h"

#include <algorithm>
#include <array>

#include "logic/traps.h"

namespace sf {

int hunterLevels(const std::vector<HeroClass>& history) {
    return static_cast<int>(std::ranges::count(history, HeroClass::Hunter));
}

int bowAttackPercent(HeroClass current, const std::vector<HeroClass>& history) {
    if (current == HeroClass::Hunter) return 100;
    return std::min(hunterLevels(history) * 50 / 30 + 40, 90);
}

BowVolley bowVolley(int kind, int spread, double angle) {
    // The fans 0x437fe0 builds on the stack, in radians: 3 shots 15 degrees
    // apart, 5 shots 10 apart, 7 shots 8 apart (24 degrees at the edge is
    // 0.4189, a shade under 3 x 8).
    static constexpr std::array<double, 7> kFans[3] = {
        {0, -0.2617993877991494, 0.2617993877991494, -0.5235987755982988, 0.5235987755982988, -0.7853981633974483,
         0.7853981633974483},
        {0, -0.17453292519943295, 0.17453292519943295, -0.3490658503988659, 0.3490658503988659, -0.5235987755982988,
         0.5235987755982988},
        {0, -0.13962634015954636, 0.13962634015954636, -0.2792526803190927, 0.2792526803190927, -0.41887902047863906,
         0.41887902047863906},
    };
    constexpr double kTwoWay = 0.13962634015954636;  // 8 degrees

    BowVolley v;
    // Stones are shot kind 1, arrows kind 0 (0x429ec0); the shot type picks
    // the arrow's layers once it is made (0x42a300).
    v.option = kind == static_cast<int>(ShotKind::Stone) ? 1 : 0;
    v.flame = kind == static_cast<int>(ShotKind::FireArrow);
    v.frost = kind == static_cast<int>(ShotKind::IceArrow);
    const int centre = directionOfAngle(angle);
    // A stone has no direction; an arrow given 8 faces where it flies.
    auto add = [&](double a, int facing) {
        const int own = directionOfAngle(a);
        v.arrows.push_back({a, v.option == 1 ? 8 : facing == 8 ? own : facing});
    };
    switch (spread) {
        case 1:
            // Two shots. The loop's counter doubles as the shot's "turn to
            // face itself" word: 2 for the first, which keeps the centre's
            // direction, and 1 for the second, which faces its own.
            add(angle - kTwoWay, centre);
            add(angle + kTwoWay, 8);
            break;
        case 2: add(angle, 8); break;
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8: {
            const int count = (spread - 1) / 2 * 2 + 1;  // 3, 3, 5, 5, 7, 7
            const auto& fan = kFans[(spread - 3) / 2];
            for (int i = 0; i < count; ++i) add(angle + fan[static_cast<std::size_t>(i)], spread % 2 == 1 ? centre : 8);
            break;
        }
        default: add(angle, 8); break;  // 0, and anything else, one shot
    }
    return v;
}

int bowHitOption(int kind, int roll) {
    if (kind == static_cast<int>(ShotKind::Stone)) {
        static constexpr int kStone[4] = {11000000, 11000001, 11000002, 11000009};
        return kStone[std::clamp(roll, 0, 3)];
    }
    return 11000020 + std::clamp(roll, 0, 2);
}

FallingArrow fallingArrow(int tick, int speed) {
    tick = std::clamp(tick, 0, kFallTicks);
    // Quarters cut towards zero, as the exe divides (0x42b9e0).
    const int off = (kFallTicks - tick) * speed / 4;
    return {off + 1, -off + 1, std::max(0, speed * kFallTicks - speed * tick)};
}

}  // namespace sf

#include "logic/traps.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

namespace sf {

namespace {

// The exe's own constants (0x475178, 0x475160).
constexpr double kDegreesPerRadian = 57.29579143313326;
constexpr double kFullTurn = 6.283184;

}  // namespace

int calcAngleDegrees(int dx, int dy) { return static_cast<int>(std::atan2(-static_cast<double>(dy), dx) * kDegreesPerRadian); }

int directionOfAngle(double radians) {
    // 0x413e40 brings the angle round into one turn first.
    while (radians < 0) radians += kFullTurn;
    while (radians > kFullTurn) radians -= kFullTurn;
    int tenths = static_cast<int>(radians * kDegreesPerRadian * 10.0);
    if (tenths < 0) tenths += 3600;
    tenths %= 3600;
    if (tenths < 226) return 1;
    if (tenths < 676) return 2;
    if (tenths < 1126) return 3;
    if (tenths < 1576) return 4;
    if (tenths < 2026) return 5;
    if (tenths < 2476) return 6;
    if (tenths < 2926) return 7;
    return tenths > 3375 ? 1 : 0;
}

int rectGap(int ax, int ay, const JudgeRect& a, int bx, int by, const JudgeRect& b) {
    const int l1 = ax + a.left, t1 = ay + a.top, r1 = ax + a.right, b1 = ay + a.bottom;
    const int l2 = bx + b.left, t2 = by + b.top, r2 = bx + b.right, b2 = by + b.bottom;
    // Apart one way only: the nearest pair of edges, less one.
    auto edges = [](int lo1, int hi1, int lo2, int hi2) {
        return std::min({std::abs(lo1 - lo2), std::abs(hi1 - lo2), std::abs(lo1 - hi2), std::abs(hi1 - hi2)}) - 1;
    };
    if (r2 < l1 || r1 < l2) {
        if (t1 <= b2 && t2 <= b1) return edges(l1, r1, l2, r2);
        const double dx = l1 < l2 ? r1 - l2 : l1 - r2;
        const double dy = t1 < t2 ? b1 - t2 : t1 - b2;
        return std::max(static_cast<int>(std::hypot(dx, dy)) - 1, 0);
    }
    if (b2 < t1 || b1 < t2) return edges(t1, b1, t2, b2);
    return 0;
}

std::optional<TrapShot> trapShot(int kind, int direction, double angle) {
    TrapShot s;
    if (direction < 0) direction = 8;
    switch (kind) {
        case 0:
        case 1:
        case 3:
        case 4:
        case 5:
        case 7:
            // Arrows and the like: a 61-unit square that flies until it hits
            // the nearest one in it or a wall. Kind 1 (the stone) has no
            // direction and kind 3 always faces where it flies; 4, 5 and 7
            // are kind 0's arrow.
            s.option = kind == 1 || kind == 3 ? kind : 0;
            s.direction = kind == 1 ? 8 : kind == 3 || direction == 8 ? directionOfAngle(angle) : direction;
            s.box = {-30, -30, 30, 30};
            s.diesOnHit = true;
            s.diesOnWall = true;
            s.hitSound = 7;
            return s;
        case 2:
            // A fire jet: 360 units long out of the trap along one of the
            // four diagonal directions, hurting everyone in it from tick 5
            // to tick 30. Other directions leave the box unset in the exe.
            s.option = 2;
            s.direction = direction;
            switch (direction) {
                case 1: s.box = {0, -60, 360, 60}; break;
                case 3: s.box = {-60, -360, 60, 0}; break;
                case 5: s.box = {-360, -60, 0, 60}; break;
                case 7: s.box = {-60, 0, 60, 360}; break;
                default: break;
            }
            s.hitFrom = 5;
            s.hitTo = 30;
            s.lastsItsAnimation = true;
            s.hitsAll = true;
            return s;
        case 6:
            // A big shot (OPTION 4) with a 320-unit box, facing its way.
            s.option = 4;
            s.direction = direction == 8 ? directionOfAngle(angle) : direction;
            s.box = {-160, -160, 159, 159};
            s.diesOnHit = true;
            s.diesOnWall = true;
            s.hitSound = 20;
            return s;
        default: return std::nullopt;
    }
}

unsigned shotHiddenLayers(int kind) {
    constexpr unsigned kFlame = 1u << 2, kFrost = 1u << 3;
    switch (kind) {
        case 0:
        case 7: return kFlame | kFrost;
        case 4: return kFrost;
        case 5: return kFlame;
        default: return 0;
    }
}

int monsterShotSpark(int kind, int roll) {
    const bool arrow = kind == 0 || kind == 4 || kind == 5 || kind == 7;
    return arrow ? 21007 + std::clamp(roll, 0, 3) % 3 : 21000 + std::clamp(roll, 0, 3);
}

int monsterShotEffect(int kind) {
    if (kind == 4) return 20000;
    if (kind == 5) return 21013;
    return -1;
}

int scriptEffectOption(int controller) {
    static const std::map<int, int> kOptions{
        {20000, 10000011}, {20001, 10000001}, {20005, 11000003}, {20006, 11000004}, {20007, 11000005}, {20008, 11000006},
        {20009, 11000007}, {20010, 11000008}, {20011, 11000010}, {20014, 11000013}, {20015, 11000014}, {20016, 11000015},
        {20017, 11000016}, {20018, 10000020}, {21000, 11000000}, {21001, 11000001}, {21002, 11000002}, {21003, 11000009},
        {21004, 11000017}, {21005, 11000018}, {21006, 11000019}, {21007, 11000020}, {21008, 11000021}, {21009, 11000022},
        {21013, 11000026}, {21014, 11000027}, {21019, 11000050}, {21020, 11000060}, {21021, 11000080}, {21022, 11000081},
        {21023, 11000090}, {21024, 11000200}, {21025, 11000100}, {21028, 11000230}, {21029, 11000241}, {21030, 11000251},
        {22000, 12000000},
    };
    const auto found = kOptions.find(controller);
    return found == kOptions.end() ? 0 : found->second;
}

int trapHitOption(int kind, int roll) {
    if (kind == 0) return scriptEffectOption(21007 + std::clamp(roll, 0, 3) % 3);
    return scriptEffectOption(21000 + std::clamp(roll, 0, 3));
}

bool shotMeetsWall(const GroundLayer& ground, int x, int y, const JudgeRect& box) {
    if (ground.magX <= 0 || ground.magY <= 0) return false;
    const int left = x + box.left, top = y + box.top, right = x + box.right, bottom = y + box.bottom;
    auto cell = [](int v, int mag) { return v >= 0 ? v / mag : -((-v + mag - 1) / mag); };
    const int x0 = std::max(cell(left, ground.magX) - ground.judgeX, 0);
    const int x1 = std::min(cell(right, ground.magX) - ground.judgeX, ground.judgeWidth - 1);
    const int y0 = std::max(cell(top, ground.magY) - ground.judgeY, 0);
    const int y1 = std::min(cell(bottom, ground.magY) - ground.judgeY, ground.judgeHeight - 1);
    for (int jy = y0; jy <= y1; ++jy) {
        for (int jx = x0; jx <= x1; ++jx) {
            const auto judge = ground.judge[static_cast<std::size_t>(jy) * ground.judgeWidth + jx];
            if ((judge & 1) && !(judge & 2)) return true;
        }
    }
    return false;
}

std::optional<int> stunTicks(int chance, int length, bool steady, int chanceResist, int lengthResist, int roll) {
    if (roll >= std::max(0, chance - chanceResist)) return std::nullopt;
    const int ticks = std::max(1, length - lengthResist);
    return steady ? ticks : std::min(ticks, 15);
}

int stunPush(int tick, int total) { return total > 0 ? (total - tick) * 120 / total : 0; }

}  // namespace sf

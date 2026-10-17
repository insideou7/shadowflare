// Transport: the gate the spell opens between a field or dungeon and its
// town, as ShadowFlare.exe makes it (the cast at 0x43a260, the gate's
// record at 0x420020, its drawing at 0x4201b0 and walking into it at
// 0x420970). The gate stands 500 away from the hero and leads to the
// town's entry 100; from there it leads back to where it stands, and
// closes once its owner has come back through it. It is not saved.
#include <cmath>
#include <cstdlib>
#include <format>

#include "core/log.h"
#include "game/game.h"

namespace sf {

namespace {

constexpr int kGateOption = 10000020;  // Character/OPTION: its pieces and animation
constexpr int kTownEntry = 100;        // where it leads in town (entry point id 400)
constexpr double kGateDistance = 500;
// The way to the gate must be clear, for each direction the gate can go
// (left, top, right, bottom, from the hero): up, down, left, right.
constexpr int kCorridor[4][4] = {{-80, -580, 79, 79}, {-80, -80, 79, 579}, {-580, -80, 79, 79}, {-80, -80, 579, 79}};
constexpr double kOffset[4][2] = {{0, -kGateDistance}, {0, kGateDistance}, {-kGateDistance, 0}, {kGateDistance, 0}};
// And where it stands (0x420020).
constexpr int kStandRect[4] = {-80, -80, 79, 79};
// Walking into the 100 x 100 square round either end goes through.
constexpr double kGateHalf = 50;
// Ours: the original tests the hero's own judge rect against it, which
// the engine does not read; this is half the side of the hero's square.
constexpr double kHeroHalf = 30;
constexpr int kPieceDelay = 7, kPieceDrop = 50, kPieceBrighten = 200;
constexpr int kNameColor[3] = {0x90, 0x90, 0xff};

}  // namespace

int Game::scenarioNumber() const {
    const auto slash = world_.scenarioDir.find_last_of('/');
    if (world_.scenarioDir.empty() || slash == std::string::npos) return -1;
    char* end = nullptr;
    const long n = std::strtol(world_.scenarioDir.c_str() + slash + 1, &end, 10);
    return end && *end == '\0' ? static_cast<int>(n) : -1;
}

// Transport can only be cast where the area allows it (scenario + 0x70,
// checked at 0x441c00): fields and dungeons, not towns.
bool Game::transportAllowed() const { return !world_.scenarioDir.empty() && world_.control.transport; }

// At the start of the cast (0x43a260): the gate goes 500 away in the
// direction of the cursor, up, down, left or right, whichever is most
// that way; if the way there is blocked, the first of the four that is
// clear. None in the town itself, nor where it would stand on a wall.
void Game::openTransportGate() {
    if (!player_ || !world_.map) return;
    const int here = scenarioNumber();
    if (here < 0 || here == world_.control.town) return;
    const double dx = castTarget_.first - player_->x, dy = castTarget_.second - player_->y;
    const int toward = std::abs(dy) < std::abs(dx) ? (dx >= 0 ? 3 : 2) : (dy >= 0 ? 1 : 0);
    auto clear = [&](double x, double y, const int (&r)[4]) {
        // Ours: the original asks its collision map for the whole
        // rectangle; this samples the ground every 20 units.
        for (double ry = r[1]; ry <= r[3]; ry += 20) {
            for (double rx = r[0]; rx <= r[2]; rx += 20) {
                if (!world_.map->ground().walkable(x + rx, y + ry)) return false;
            }
        }
        return true;
    };
    int way = -1;
    if (clear(player_->x, player_->y, kCorridor[toward])) {
        way = toward;
    } else {
        for (int d = 0; d < 4 && way < 0; ++d) {
            if (d != toward && clear(player_->x, player_->y, kCorridor[d])) way = d;
        }
    }
    if (way < 0) return;
    TransportGate gate;
    gate.field = here;
    gate.x = player_->x + kOffset[way][0];
    gate.y = player_->y + kOffset[way][1];
    gate.town = world_.control.town;
    if (!clear(gate.x, gate.y, kStandRect)) return;
    gate.wait = {1 + world_.random.next(90), 1 + world_.random.next(90)};
    transportGate_ = gate;
    playSound(0x4f, gate.x, gate.y);  // as it is made (0x4201b0)
    log::info("Transport gate at {:.0f},{:.0f} to scenario {}", gate.x, gate.y, gate.town);
    trainSpell(kTransport);
}

std::optional<std::pair<double, double>> Game::transportGateHere() const {
    if (!transportGate_) return std::nullopt;
    const int here = scenarioNumber();
    if (here == transportGate_->field) return std::pair{transportGate_->x, transportGate_->y};
    if (here == transportGate_->town) {
        for (const auto& e : world_.control.entryPoints) {
            if (e.id == kTownEntry * 4) return std::pair<double, double>{e.x, e.y};
        }
    }
    return std::nullopt;
}

// The pieces drop and brighten, one more every 7 ticks; once the last is
// down the gate plays its animation now and then, and takes whoever walks
// into it to the other end (0x420970).
void Game::tickTransportGate() {
    if (!transportGate_ || !player_) return;
    TransportGate& g = *transportGate_;
    for (transportTicks_ += kOriginalTicksPerTick; transportTicks_ >= 1.0; transportTicks_ -= 1.0) {
        const int here = scenarioNumber();
        for (int end = 0; end < 2; ++end) {
            if (here != (end == 0 ? g.field : g.town) || !g.assembled()) continue;
            auto& wait = g.wait[static_cast<std::size_t>(end)];
            auto& frame = g.frame[static_cast<std::size_t>(end)];
            if (wait > 0) {
                --wait;
                continue;
            }
            // Each time its animation starts again.
            if (frame == 0) {
                if (const auto at = transportGateHere()) playSound(0x33, at->first, at->second);
            }
            if (++frame >= effectLength(kGateOption)) {
                frame = 0;
                wait = 30 + world_.random.next(60);
            }
        }
        for (std::size_t i = 0; i < 4; ++i) {
            if (g.age < kPieceDelay * static_cast<int>(i)) continue;
            g.height[i] = std::max(0, g.height[i] - kPieceDrop);
            g.light[i] = std::min(1000, g.light[i] + kPieceBrighten);
        }
        ++g.age;
    }
    if (world_.pendingScenario || !g.assembled()) return;
    const int here = scenarioNumber();
    auto onIt = [&](double x, double y) {
        return std::abs(player_->x - x) < kGateHalf + kHeroHalf && std::abs(player_->y - y) < kGateHalf + kHeroHalf;
    };
    if (here == g.field) {
        if (onIt(g.x, g.y)) {
            if (!g.standing[0]) {
                log::info("Through the Transport gate to scenario {}", g.town);
                world_.pendingScenario.emplace(g.town, kTownEntry);
                g.standing[1] = true;  // arriving on the other end does not send them back
            }
            g.standing[0] = true;
        } else {
            g.standing[0] = false;
        }
    } else if (here == g.town) {
        const auto end = transportGateHere();
        if (end && onIt(end->first, end->second)) {
            if (!g.standing[1]) {
                log::info("Back through the Transport gate to scenario {}", g.field);
                world_.pendingScenario.emplace(g.field, -1);
                pendingPosition_.emplace(g.x, g.y);
                // Its owner coming back closes it.
                transportGate_.reset();
                return;
            }
            g.standing[1] = true;
        } else {
            g.standing[1] = false;
        }
    }
}

// The gate's four pieces from its Pattern bank, each as high and as bright
// as it has come, then its animation (chart 0, undirected) once it stands.
std::vector<Effect> Game::transportGateParts() const {
    std::vector<Effect> parts;
    const auto at = transportGateHere();
    if (!at) return parts;
    const TransportGate& g = *transportGate_;
    const bool field = scenarioNumber() == g.field;
    for (std::size_t i = 0; i < 4; ++i) {
        // The town end draws all four at once (0x4201b0); by the time the
        // hero is there they are down.
        if (field && g.age < kPieceDelay * static_cast<int>(i)) continue;
        Effect e;
        e.option = kGateOption;
        e.x = at->first;
        e.y = at->second;
        e.pattern = static_cast<int>(i);
        e.height = g.height[i];
        e.light = g.light[i];
        parts.push_back(e);
    }
    const int end = field ? 0 : 1;
    if (g.assembled() && g.wait[static_cast<std::size_t>(end)] == 0) {
        Effect e;
        e.option = kGateOption;
        e.x = at->first;
        e.y = at->second;
        e.age = g.frame[static_cast<std::size_t>(end)];
        parts.push_back(e);
    }
    return parts;
}

// With the mouse on a standing gate, its owner's name over it, in a dark box.
void Game::renderTransportGateName() {
    const auto at = transportGateHere();
    if (!at || !transportGate_->assembled() || heroName_.empty()) return;
    int px, py;
    worldToScreen(static_cast<int>(at->first), static_cast<int>(at->second), px, py);
    const SDL_FPoint gate = onScreen(px, py);
    const int sx = static_cast<int>(gate.x), sy = static_cast<int>(gate.y);
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    float lx, ly;
    SDL_RenderWindowToLogical(renderer_, mx, my, &lx, &ly);
    const double reach = 32 * zoom();
    if (std::abs(lx - static_cast<float>(sx)) > reach || std::abs(ly - static_cast<float>(sy)) > reach) return;
    const int w = uiTextWidth(heroName_);
    const int top = sy - static_cast<int>(34 * zoom());
    const SDL_Rect box{sx - w / 2 - 4, top, w + 6, 15};
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 204);
    SDL_RenderFillRect(renderer_, &box);
    uiText(sx - w / 2 + 1, top + 3, heroName_, {0, 0, 0, 255});
    uiText(sx - w / 2, top + 2, heroName_,
           {static_cast<Uint8>(kNameColor[0]), static_cast<Uint8>(kNameColor[1]), static_cast<Uint8>(kNameColor[2]), 255});
}

}  // namespace sf

// The scenario script commands the world carries out by itself.
#include "world/world_script.h"

#include <array>
#include <cmath>

#include "core/log.h"
#include "world/world.h"

namespace sf {

namespace {

int directionTowards(double dx, double dy) {
    // World x runs down-right on screen and y down-left; 0 = south, then
    // counter-clockwise.
    const double sx = dx - dy, sy = (dx + dy) * 2.0 / 3.0;
    const double angle = std::atan2(sx, sy);  // 0 = down the screen, positive = right
    int d = static_cast<int>(std::lround(angle / (3.14159265358979 / 4)));
    return (d + 8) % 8;
}

}  // namespace

// x: and y: of a character the scenario does not have are -1, as in
// the exe (0x4346b0). Some scripts name trap vents their map lacks,
// such as 10057004 in Vaporous Forest; those shots go off at (-1, -1)
// and hurt no one.
std::int32_t WorldScript::characterX(std::int32_t id) const {
    if (const auto at = world_.characterAt(id)) return static_cast<std::int32_t>(at->first);
    return -1;
}

std::int32_t WorldScript::characterY(std::int32_t id) const {
    if (const auto at = world_.characterAt(id)) return static_cast<std::int32_t>(at->second);
    return -1;
}

void WorldScript::stopCharacter(std::int32_t id, bool stop) {
    auto* a = world_.actorById(id);
    if (!a) return;
    a->stopped = stop;
    // Either ends a scripted action: they stand again (0x45d480).
    if (a->scripted && !a->hostile()) {
        a->scripted.reset();
        a->character.play(chart::kIdle);
    }
}

// C_ACTIONREQ on a person (0x45d480 sets its state, 0x45d3d0 plays it):
// state 1 stands, 2 walks, 3 talks and 4 on play chart state - 1. Monsters
// and objects take the command through their own code, which no scenario
// uses.
void WorldScript::requestAction(const ActionRequest& r) {
    Actor* a = world_.actorById(r.character);
    if (!a || a->hostile()) return;
    if (r.action <= 1) {
        a->scripted.reset();
        a->character.play(chart::kIdle);
        return;
    }
    const int chart = r.action - 1;
    if (!a->character.hasChart(chart)) {
        log::info("{} has no chart {} for action {}", r.character, chart, r.action);  // our art may not draw it
        return;
    }
    a->scripted = Actor::ScriptedAction{chart, r.loop, r.from, r.to};
    log::info("{} plays action {} (chart {}, {} frames){}", r.character, r.action, chart, a->character.frameCount(), r.loop ? ", looping" : "");
    a->character.restart(chart);
}

void WorldScript::faceCharacter(std::int32_t id, std::int32_t towards) {
    auto* a = world_.actorById(id);
    const Character* hero = world_.hero();
    if (!a || !hero) return;
    // 0 turns them to the player.
    double tx = hero->x, ty = hero->y;
    if (const auto* t = towards ? world_.actorById(towards) : nullptr) {
        tx = t->character.x;
        ty = t->character.y;
    }
    a->character.direction = directionTowards(tx - a->character.x, ty - a->character.y);
}

void WorldScript::setActive(std::int32_t id, bool active) {
    if (auto* a = world_.actorById(id)) a->active = active;
}

void WorldScript::setObjectStates(std::int32_t id, int shown, int usable, int blocks) {
    if (auto* o = world_.sceneObject(id)) o->fixed = std::array{shown, usable, blocks};
}

void WorldScript::setUnlockSwitch(bool on) { world_.unlockSwitch = on; }

int WorldScript::random(int n) { return world_.random.next(n); }

int WorldScript::entryPoint() const { return world_.entryPoint; }

std::optional<bool> WorldScript::enemyAlive(std::int32_t id) const {
    if (const auto* a = world_.actorById(id)) return a->hostile() ? std::optional(a->alive()) : std::nullopt;
    // A monster the engine could not spawn still counts, as alive.
    for (const auto& p : world_.control.placements) {
        if (p.group == ScenarioControl::Group::Enemy && ScenarioControl::idBase(p.group) + p.id == id) return true;
    }
    return std::nullopt;
}

void WorldScript::skippedCommand(std::int32_t opcode) {
    // Once per scenario and command, so a playthrough log lists what
    // each area still lacks without a line every frame.
    if (world_.skippedCommands.insert(opcode).second) {
        log::warn("Script command {} ({}) in {} is not carried out yet", ScenarioScript::commandName(opcode), opcode, world_.scenarioDir);
    }
}

bool WorldScript::nearestPlayer(std::int32_t character, int min, int max, int& player, int& x, int& y) const {
    // Single play: the hero is the only player (0x430b30).
    const auto p = world_.placementById(character);
    const auto at = world_.positionOf(character);
    const Character* hero = world_.hero();
    if (!p || !at || !hero || !world_.heroAlive()) return false;
    const JudgeRect rect{p->rect[0], p->rect[1], p->rect[2], p->rect[3]};
    const int gap = rectGap(static_cast<int>(hero->x), static_cast<int>(hero->y), kHeroJudge, static_cast<int>(at->first),
                            static_cast<int>(at->second), rect);
    if ((min != -1 && gap < min) || (max != -1 && gap > max)) return false;
    player = 0;
    x = static_cast<int>(hero->x);
    y = static_cast<int>(hero->y);
    return true;
}

int WorldScript::playerDistance(std::int32_t character) const {
    const auto p = world_.placementById(character);
    const auto at = world_.positionOf(character);
    const Character* hero = world_.hero();
    if (!p || !at || !hero) return -1;
    const JudgeRect rect{p->rect[0], p->rect[1], p->rect[2], p->rect[3]};
    return rectGap(static_cast<int>(hero->x), static_cast<int>(hero->y), kHeroJudge, static_cast<int>(at->first),
                   static_cast<int>(at->second), rect);
}

// RESSURECT (0x45a140): a dead enemy is back where the script says, at
// full health, facing `direction`, and counts as alive again.
void WorldScript::resurrect(std::int32_t enemy, int x, int y, int direction) {
    Actor* a = world_.actorById(enemy);
    if (!a || !a->hostile() || a->alive()) return;
    a->hp = a->maxHp;
    a->state = Actor::State::Idle;
    a->action = -1;
    a->actionTicks = 0;
    a->event = AiControl::kDecide;
    a->stunTicks = 0;
    a->struck = false;
    a->character.x = x;
    a->character.y = y;
    if (direction >= 0 && direction < 8) a->character.direction = direction;
    a->character.restart(chart::kIdle);
    log::info("Monster {} is back", enemy);
}

// The object's opacity at + 0xf4, which 0x45ddd0 draws it with.
void WorldScript::setObjectOpacity(std::int32_t object, int opacity) {
    for (auto& p : world_.control.placements) {
        if (p.group == ScenarioControl::Group::Object && ScenarioControl::idBase(p.group) + p.id == object) p.look.opacity = opacity;
    }
}

std::optional<int> WorldScript::objectOpacity(std::int32_t object) const {
    const auto p = world_.placementById(object);
    if (!p || p->group != ScenarioControl::Group::Object) return std::nullopt;
    return p->look.opacity;
}

void WorldScript::playSound(int voice, bool everywhere, int x, int y) {
    const Character* hero = world_.hero();
    if (!hero) return;
    if (x == -1) {
        world_.sound(voice, hero->x, hero->y, everywhere);
    } else {
        world_.sound(voice, x, y, everywhere);
    }
}

}  // namespace sf

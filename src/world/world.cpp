// The world's lookups and the rules that need only what is in it: object
// states, who stands where and moving on the map.
#include "world/world.h"

#include <utility>

#include "logic/movement.h"

namespace sf {

namespace {

bool inside(const ScenarioControl::Placement& p, double x, double y) {
    return x >= p.x + p.rect[0] && x <= p.x + p.rect[2] && y >= p.y + p.rect[1] && y <= p.y + p.rect[3];
}

}  // namespace

void Actor::endStun() {
    stunTicks = 0;
    if (stunKind == StunKind::Stone) character.grey = character.held = false;
    stunKind = StunKind::Plain;
}

const Actor* World::actorById(std::int32_t id) const {
    for (const auto& a : actors) {
        if (a.gameId() == id) return &a;
    }
    return nullptr;
}

Actor* World::actorById(std::int32_t id) { return const_cast<Actor*>(std::as_const(*this).actorById(id)); }

std::optional<std::pair<double, double>> World::positionOf(std::int32_t id) const {
    if (const auto* a = actorById(id)) return std::pair(a->character.x, a->character.y);
    for (const auto& p : control.placements) {
        if (ScenarioControl::idBase(p.group) + p.id == id) return std::pair(double(p.x), double(p.y));
    }
    return std::nullopt;
}

std::optional<std::pair<double, double>> World::characterAt(std::int32_t id) const {
    if (id >= 0 && id < 4) {
        if (const Character* h = hero(); id == 0 && h) return std::pair(h->x, h->y);
        return std::nullopt;
    }
    return positionOf(id);
}

void World::runScript(std::int32_t sentence, std::int32_t character, WorldScript& host) {
    if (!script || pendingScenario) return;
    const std::int32_t outer = host.character();
    host.setCharacter(character);
    ScriptRunner runner(*script, flags, &host, character);
    runner.setRules(rules);
    if (const auto change = runner.run(sentence)) {
        pendingScenario.emplace(change->scenario, change->entry);
    }
    host.setCharacter(outer);
}

void World::runEvent(std::int32_t event, std::int32_t character, WorldScript& host) {
    if (!script) return;
    for (const auto& t : script->triggers) {
        if (t.event != event || (character != -1 && t.character != character)) continue;
        runScript(t.sentence, t.character, host);
    }
}

void World::tickObjects() {
    for (auto& o : sceneObjects) {
        if (o.animation && objectState(o, kShown)) o.animation->tick();
    }
    if (unlockSwitch) unlockTicks += 1.5;
}

const ScenarioControl::Placement* World::placementById(std::int32_t id) const {
    for (const auto& p : control.placements) {
        if (ScenarioControl::idBase(p.group) + p.id == id) return &p;
    }
    return nullptr;
}

bool World::objectState(const SceneObject& o, ObjectState which) const {
    if (o.fixed) return (*o.fixed)[which] != 0;
    const auto& p = placementOf(o);
    const auto found = flags.find({ScenarioScript::kNet, kStateFlagBases[which] + ScenarioControl::idBase(p.group) + p.id});
    // A missing flag reads as -1, which counts as set (0x4541c0).
    return found == flags.end() || found->second != 0;
}

const SceneObject* World::sceneObject(std::int32_t id) const {
    for (const auto& o : sceneObjects) {
        const auto& p = placementOf(o);
        if (ScenarioControl::idBase(p.group) + p.id == id) return &o;
    }
    return nullptr;
}

SceneObject* World::sceneObject(std::int32_t id) { return const_cast<SceneObject*>(std::as_const(*this).sceneObject(id)); }

bool World::objectInTheWay(double fromX, double fromY, double toX, double toY) const {
    for (const auto& o : sceneObjects) {
        const auto& p = placementOf(o);
        if (p.rect[0] == p.rect[2] || p.rect[1] == p.rect[3]) continue;
        if (!objectState(o, kBlocks)) continue;
        if (inside(p, toX, toY) && !inside(p, fromX, fromY)) return true;
    }
    return false;
}

bool World::boxMeetsObject(double x, double y, const JudgeRect& box) const {
    for (const auto& o : sceneObjects) {
        const auto& p = placementOf(o);
        if (p.rect[0] == p.rect[2] || p.rect[1] == p.rect[3]) continue;
        if (!objectState(o, kBlocks)) continue;
        if (x + box.left <= p.x + p.rect[2] && p.x + p.rect[0] <= x + box.right && y + box.top <= p.y + p.rect[3] &&
            p.y + p.rect[1] <= y + box.bottom) {
            return true;
        }
    }
    return false;
}

bool World::tryMove(Character& who, double dx, double dy) const {
    const double x0 = who.x, y0 = who.y;
    if (!sf::tryMove(map->ground(), who.x, who.y, dx, dy)) return false;
    if (!objectInTheWay(x0, y0, who.x, who.y)) return true;
    // A closed door or a chest in the way: slide along it as along a wall.
    for (const auto& [sx, sy] : {std::pair{dx, 0.0}, std::pair{0.0, dy}}) {
        if (sx == 0 && sy == 0) continue;
        double x = x0, y = y0;
        if (sf::tryMove(map->ground(), x, y, sx, sy) && !objectInTheWay(x0, y0, x, y)) {
            who.x = x;
            who.y = y;
            return true;
        }
    }
    who.x = x0;
    who.y = y0;
    return false;
}

void World::sound(int voice, double x, double y, bool everywhere) {
    if (host_ && host_->hero()) events.sound(voice, x, y, everywhere);
}

}  // namespace sf

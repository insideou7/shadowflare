// The world's lookups and the rules that need only what is in it: object
// states, who stands where and moving on the map.
#include "world/world.h"

#include <format>
#include <utility>

#include "core/log.h"

#include "logic/movement.h"

namespace sf {

namespace {

// "MISS!!": OPTION 11000011's picture, starting this high over whoever
// was missed (0x417a60).
constexpr int kMissOption = 11000011;
constexpr double kMissHeight = 400.0;
// What an effect is drawn for when the content has no animation for it.
constexpr int kMissingLength = 16;

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

void World::checkTriggers(double x, double y, bool fire, WorldScript& host) {
    for (auto& t : triggers) {
        const bool inside = x >= t.left && x <= t.right && y >= t.top && y <= t.bottom;
        if (fire && inside && !t.inside && script && !pendingScenario) {
            runScript(t.sentence, -1, host);
        }
        t.inside = inside;
    }
}

void World::tickGroundItems() {
    for (auto& g : groundItems) {
        if (g.sprite) g.sprite->tick();
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

const CharacterModel* World::effectModel(int option) {
    if (option < 0) return nullptr;  // -1: an unseen area that only hurts; 0 is the trap arrow
    auto it = effectModels.find(option);
    if (it == effectModels.end()) {
        auto model = CharacterModel::load(*data, std::format("Character/OPTION/{:08d}/Animation", option));
        if (!model) log::warn("No animation for effect {}", option);
        it = effectModels.emplace(option, model ? std::make_unique<CharacterModel>(std::move(*model)) : nullptr).first;
    }
    return it->second.get();
}

// The most frames any direction of a chart has (GetMaxFrameCount).
int World::effectLength(int option, int chart) {
    const CharacterModel* model = effectModel(option);
    if (!model || chart < 0 || static_cast<std::size_t>(chart) >= model->animations.charts.size()) return kMissingLength;
    int frames = 0;
    for (const auto& dir : model->animations.charts[static_cast<std::size_t>(chart)].directions) {
        frames = std::max(frames, static_cast<int>(dir.frameCount));
    }
    return frames > 0 ? frames : kMissingLength;
}

// New effects wait in pendingEffects until the tick ends, so the ones
// being stepped never move in memory. The reference is only good until the
// next spawn.
Effect& World::spawnEffect(int option, double x, double y, int chart) {
    Effect e;
    e.id = nextEffectId++;
    e.option = option;
    e.chart = chart;
    e.x = x;
    e.y = y;
    e.life = effectLength(option, chart);
    pendingEffects.push_back(std::move(e));
    return pendingEffects.back();
}

// "MISS!!" pops up over whoever was missed: a still picture, made here
// rather than by spawnEffect, hopping where it was put (stepHop).
void World::showMiss(double x, double y) {
    log::info("Missed at ({:.0f}, {:.0f})", x, y);
    Effect e;
    e.id = nextEffectId++;
    e.option = kMissOption;
    e.pattern = 0;
    e.x = x;
    e.y = y;
    e.life = -1;
    e.height = kMissHeight;
    e.hop = Effect::Hop{};
    e.hop->floor = kMissHeight;
    pendingEffects.push_back(std::move(e));
}

// One of the original's ticks of the hop (0x42c900): up by a tenth of its
// speed, which falls by 100 a tick. Each time it comes down to where it
// started it bounces, at 300 and then 200; after the third landing it
// fades by a tenth a tick and is gone.
void World::stepHop(Effect& e) {
    Effect::Hop& h = *e.hop;
    e.height += h.speed / 10;
    if (e.height < h.floor) {
        e.height = h.floor;
        if (h.landings == 0) h.speed = 300;
        if (h.landings == 1) h.speed = 200;
        if (h.landings != 3) ++h.landings;
    }
    h.speed -= 100;
    if (h.landings == 3) {
        e.light = std::max(0, h.fade);
        h.fade -= 100;
        if (h.fade == 0) e.dead = true;
    }
    ++e.age;
}

Effect* World::findEffect(int id) {
    for (auto* list : {&effects, &pendingEffects}) {
        for (auto& e : *list) {
            if (e.id == id && !e.dead) return &e;
        }
    }
    return nullptr;
}

int World::directionFrames(int option, int direction) {
    const CharacterModel* model = effectModel(option);
    if (!model || model->animations.charts.empty()) return effectLength(option);
    const auto& dirs = model->animations.charts[0].directions;
    for (int d : {direction, 8, 0}) {
        if (d >= 0 && static_cast<std::size_t>(d) < dirs.size() && dirs[static_cast<std::size_t>(d)].frameCount > 0) {
            return dirs[static_cast<std::size_t>(d)].frameCount;
        }
    }
    return effectLength(option);
}

}  // namespace sf

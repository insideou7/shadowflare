// The world library (src/world) links without SDL: this test links only
// sf_world, loads a map and a character from the open content, plays an
// animation and runs a World with no window, so anything that pulls SDL
// into the world breaks the build.
#include <cmath>
#include <cstdio>
#include <optional>
#include <cstdlib>

#include "platform/filesystem.h"
#include "world/character.h"
#include "world/map.h"
#include "world/world.h"
#include "world/world_events.h"
#include "world/world_script.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// The hero's side, as Game is for the real world.
struct TestHost : sf::WorldHost {
    std::optional<sf::Character> heroCharacter;
    int strikes = 0;
    const sf::Character* hero() const override { return heroCharacter ? &*heroCharacter : nullptr; }
    bool heroAlive() const override { return true; }
    void monsterStrikes(sf::Actor&, int) override { ++strikes; }
    void monsterCasts(sf::Actor&, int) override {}
    void monsterThrows(sf::Actor&, int) override {}
};

struct TestScript : sf::WorldScript {
    using WorldScript::WorldScript;
};

// A spot with open ground for a few judge cells to the east.
bool openSpot(const sf::GroundLayer& g, double& x, double& y) {
    for (int jy = 1; jy + 1 < g.judgeHeight; ++jy) {
        for (int jx = 0; jx + 6 < g.judgeWidth; ++jx) {
            bool open = true;
            for (int dy = -1; dy <= 1 && open; ++dy) {
                for (int dx = 0; dx < 6 && open; ++dx) {
                    open = g.walkable((g.judgeX + jx + dx + 0.5) * g.magX, (g.judgeY + jy + dy + 0.5) * g.magY);
                }
            }
            if (open) {
                x = (g.judgeX + jx + 0.5) * g.magX;
                y = (g.judgeY + jy + 0.5) * g.magY;
                return true;
            }
        }
    }
    return false;
}

void testWorld(const sf::FileSystem& content, const sf::CharacterModel& model) {
    sf::World world;
    world.data = &content;
    world.map = sf::Map::load(content, "f00_01");
    if (!world.map) return;
    double x = 0, y = 0;
    check(openSpot(world.map->ground(), x, y), "the map has open ground");

    // Sounds are only heard once there is a hero.
    TestHost host;
    world.setHost(&host);
    world.sound(5, x, y);
    check(world.events.empty(), "no hero, no sounds");
    host.heroCharacter.emplace(&model);
    host.heroCharacter->x = x;
    host.heroCharacter->y = y;
    world.sound(5, x, y);
    check(!world.events.takeSounds().empty(), "a hero hears sounds");

    // An object: its NET flags say whether it stands in the way, until
    // ABSOLUTEOBJECTFLAG fixes its states.
    sf::ScenarioControl::Placement door;
    door.group = sf::ScenarioControl::Group::Object;
    door.id = 7;
    door.x = static_cast<std::int32_t>(x) + 300;
    door.y = static_cast<std::int32_t>(y);
    door.rect[0] = -40, door.rect[1] = -400, door.rect[2] = 40, door.rect[3] = 400;
    world.control.placements.push_back(door);
    world.sceneObjects.push_back(sf::SceneObject{});
    const std::int32_t id = sf::ScenarioControl::idBase(door.group) + door.id;
    check(world.sceneObject(id) != nullptr, "the object is found by id");
    check(world.objectState(world.sceneObjects[0], sf::World::kBlocks), "a missing flag reads as set");
    sf::Character walker(&model);
    walker.x = x;
    walker.y = y;
    check(!world.tryMove(walker, 300, 0) && walker.x == x, "a closed door stops a step into it");
    world.flags[{sf::ScenarioScript::kNet, sf::kStateFlagBases[sf::World::kBlocks] + id}] = 0;
    check(world.tryMove(walker, 300, 0) && walker.x == x + 300, "an open one does not");
    TestScript script(world, -1);
    script.setObjectStates(id, 1, 1, 1);
    walker.x = x;
    check(!world.tryMove(walker, 300, 0), "fixed states win over the flags");

    // A monster without an AI list chases a hero it can see.
    sf::ScenarioControl::Placement p;
    p.group = sf::ScenarioControl::Group::Enemy;
    p.id = 1;
    p.kind = 1;
    p.x = static_cast<std::int32_t>(x) + 1000;
    p.y = static_cast<std::int32_t>(y) + 100;
    sf::Actor monster(p, sf::Character(&model));
    monster.character.x = p.x;
    monster.character.y = p.y;
    monster.hp = monster.maxHp = 10;
    world.actors.push_back(std::move(monster));
    world.sceneObjects.clear();
    const auto gap = [&] {
        const auto& c = world.actors[0].character;
        return std::hypot(c.x - x, c.y - y);
    };
    const double before = gap();
    for (int i = 0; i < 10; ++i) world.tickActors();
    check(gap() < before && world.actors[0].state == sf::Actor::State::Chase, "the monster comes for the hero");
    check(script.enemyAlive(world.actors[0].gameId()) == true, "scripts see it alive");
    check(script.characterX(0) == static_cast<std::int32_t>(x), "scripts see the hero as character 0");
    script.setActive(world.actors[0].gameId(), false);
    const double stopped = gap();
    world.tickActors();
    check(gap() == stopped, "SETINACTIVE holds it");

    // Effects: an unseen one lasts the stand-in length and is found by id.
    const int effect = world.spawnEffect(-1, x, y).id;
    check(world.findEffect(effect) != nullptr && world.findEffect(effect)->life == 16, "an effect with no animation lasts 16 ticks");
}

}  // namespace

int main() {
    const sf::DirectoryFileSystem content(SF_CONTENT_DIR);
    const auto map = sf::Map::load(content, "f00_01");
    check(map.has_value(), "the open pack's first map loads");
    if (map) check(map->ground().width > 0 && map->ground().height > 0, "the map has ground");

    const auto model = sf::CharacterModel::load(content, "Player/Male/Animation00");
    check(model.has_value(), "the hero's animations load");
    if (model) {
        sf::Character hero(&*model);
        hero.setAction(sf::Action::Walk);
        check(hero.chart() == sf::chart::kWalk && hero.frameCount() > 1, "the hero walks");
        const int first = hero.frame();
        for (int i = 0; i < 4; ++i) hero.tick();
        check(hero.frame() != first, "the walk moves on frame by frame");
        check(hero.currentDirection() != nullptr, "the walk has frames facing south");
        hero.faceTowards(1, 0);
        check(hero.direction != 0, "the hero turns");
        testWorld(content, *model);
    }
    sf::WorldEvents events;
    check(events.empty(), "no events to begin with");
    events.sound(3, 100, 200);
    events.sound(-1, 0, 0);  // no sound: nothing queued
    events.message("Game saved");
    events.sound(0x39);
    events.message("Autosaved");
    const auto sounds = events.takeSounds();
    check(sounds.size() == 2 && sounds[0] == sf::WorldEvents::Sound{3, 100, 200, true, false} &&
              sounds[1] == sf::WorldEvents::Sound{0x39, 0, 0, false, true},
          "sounds come out in order, placed or heard everywhere");
    const auto messages = events.takeMessages();
    check(messages.size() == 2 && messages[0] == "Game saved" && messages[1] == "Autosaved", "messages come out in order");
    check(events.empty(), "taking empties the queue");

    if (failures) {
        std::printf("%d world checks failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("world checks passed\n");
    return EXIT_SUCCESS;
}

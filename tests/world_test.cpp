// The world library (src/world) links without SDL: this test links only
// sf_world, loads a map and a character from the open content and plays
// an animation, so anything that pulls SDL into the world breaks the build.
#include <cstdio>
#include <cstdlib>

#include "platform/filesystem.h"
#include "world/character.h"
#include "world/map.h"
#include "world/world_events.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
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

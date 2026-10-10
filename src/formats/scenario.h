#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf {

// Scenario/<id>/Scenario.Mct: which map a scenario takes place on and the
// objects, people, enemies and items placed on it. Read by ShadowFlare.exe
// (the loader at 0x427b50).
struct ScenarioControl {
    enum class Group { Object, Person, Enemy, Item };

    struct Placement {
        Group group = Group::Object;
        std::int32_t id = 0;
        // Character folder number: Character/OBJECT, PEOPLE or ENEMY/<kind>,
        // or PARTNER/<kind - kPartnerBase> for people. -1 = no sprite
        // (spawners, trigger areas).
        std::int32_t kind = -1;
        std::string name;  // Shift-JIS in the Japanese release, ASCII here
        std::int32_t x = 0, y = 0;  // world units
        std::int32_t direction = 0;  // 0 = south, counter-clockwise; 8 = none
        std::int32_t rect[4] = {};   // left, top, right, bottom around (x, y)
    };

    static constexpr std::int32_t kPartnerBase = 1000000;

    std::string mapPath;  // e.g. "Map\\c02_00.map"
    std::string title;    // area name shown to the player
    std::vector<Placement> placements;

    static ScenarioControl parse(std::span<const std::uint8_t> data);
};

}  // namespace sf

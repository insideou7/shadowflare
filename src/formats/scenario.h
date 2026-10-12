#pragma once

#include <array>
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

        // Enemies only, from the 0x13c-byte enemy block. The loader
        // (0x428bc2-0x428f02) copies its words, out of order, into the
        // monster from + 0xc4 on; the combat code reads them there (word 9
        // at + 0xe8 is the defence 0x459690 hands the damage code, word 23
        // at + 0xf8 the attack of 0x45a749, and so on).
        struct EnemyStats {
            // 0 fire, 1 water, 2 earth, 3 thunder, 4 holy, 5 dark, 6 gel,
            // 7 metal (word 6); -1 = none, for open content monsters.
            std::int32_t element = -1;
            std::int32_t hp = 0;            // word 8
            std::int32_t defense = 0;       // word 9
            std::int32_t evasion = 0;       // word 10; against blows (0x45eb38)
            std::int32_t magicDefense = 0;  // word 11
            std::int32_t experience = 0;    // word 13
            // Word 14 (monster + 0x180): the row of Table.Tbd table 30, its
            // drop list, that 0x45a000 rolls when it dies. Open content
            // calls it the monster's level.
            std::int32_t dropList = 0;
            // Its three attacks (Control.aid actions 2-4): power (words
            // 23-25) and hit (words 29-31).
            std::array<std::int32_t, 3> attacks{}, hits{50, 50, 50};
            // Its three spells (actions 5-7): which (words 32-34, -1 = none),
            // their power (words 35-37), the level whose Table.Tbd columns
            // they use (words 38-40) and what they add to the spell's hit
            // (words 41-43), as the cast code at 0x45b043 reads them.
            std::array<std::int32_t, 3> spells{-1, -1, -1}, spellPower{}, spellLevel{1, 1, 1}, spellHit{};
            std::int32_t magicEvasion = 0;  // word 12; what spells must beat (0x45eb2b)
            // Word 7 (monster + 0xdc), meaning unknown; Counter Burst
            // reflects half as much onto monsters where it is 100.
            std::int32_t word7 = 0;
            std::int32_t goldChance = 0;  // percent; word 49, read by the exe at 0x45a030
            std::int32_t goldMin = 0, goldMax = 0;
            std::int32_t speed = 1000;    // per mille, scales the AI's movement speeds
            // What it takes off a stun's chance (percent) and length
            // (ticks), and whether its stuns may run past 15 ticks: words
            // 61-63, read by the damage code at 0x459690 from monster
            // + 0x1cc-0x1d4. Inferred: 999 marks bosses that never stun.
            std::int32_t stunChanceResist = 0, stunLengthResist = 0;
            bool stunUncapped = false;
            std::int32_t area[4] = {};    // where it wanders: left, top, right, bottom around (x, y)
            std::string ai;               // name of its list in Control.aid (Shift-JIS)

            // The first attack, which the simple chase uses.
            std::int32_t attack() const { return attacks[0]; }
            std::int32_t hit() const { return hits[0]; }
        } stats;

        // Objects: the companion's jump or dash presses it (word 5, the
        // object's + 0xe8 at 0x460420; inferred from which objects have it).
        bool companionSwitch = false;

        // Items lying on the map: which one, as an Item.Ibn (class, code).
        std::int32_t itemClass = -1, itemCode = 0;
    };

    // Where the party appears when the scenario is entered. Each entry
    // number used by CHANGESCENARIO has four points, one per party member,
    // with ids entry * 4 to entry * 4 + 3.
    struct EntryPoint {
        std::int32_t id = 0;
        std::int32_t x = 0, y = 0;  // world units
        std::int32_t direction = 0;
    };

    static constexpr std::int32_t kPartnerBase = 1000000;

    // Offsets that turn a placement id into the in-game id scripts use.
    static constexpr std::int32_t idBase(Group group) {
        constexpr std::int32_t bases[] = {10000000, 12000000, 20000000, 30000000};
        return bases[static_cast<int>(group)];
    }

    std::string mapPath;  // e.g. "Map\\c02_00.map"
    std::string title;    // area name shown to the player
    std::vector<Placement> placements;
    std::vector<EntryPoint> entryPoints;

    static ScenarioControl parse(std::span<const std::uint8_t> data);
};

}  // namespace sf

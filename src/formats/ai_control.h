#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf {

// System/Game/Parameter/Control.aid: monster behaviour (RKC_RPG_AICONTROL.dll,
// RKC_RPG_AICONTROL::ReadFile). Each named list, such as "goblin normal
// attack", says which action a monster may take after each event, under
// which conditions, and how likely each is. Monsters name their list in the
// scenario file. How the game uses it was read from ShadowFlare.exe around
// 0x45c9f0; see docs/formats.md.
struct AiControl {
    // What just happened to the monster. Not every number is identified.
    enum Event : int {
        kDecide = 0,         // the last action finished
        kWanderDone = 1,
        kRetreatDone = 9,
        kApproachDone = 10,
        kWaiting = 11,       // while action 0 runs
        kWandering = 12,     // while action 1 runs
        kRetreating = 14,    // while action 9 runs
        kApproaching = 15,   // while action 10 runs
        kAnimationDone = 16, // an attack or spell finished
        kHit = 17,           // took damage
    };

    // What the monster does.
    enum Action : int {
        kWait = 0,      // stand still for param[1] ticks
        kWander = 1,    // stroll inside the home area for param[1] ticks
        kAttack = 2,    // 2 to 4: melee attacks
        kMagic = 5,     // 5 to 7: spells
        kStop = 8,
        kRetreat = 9,   // move away from the target for param[1] ticks
        kApproach = 10, // move towards the target for param[1] ticks
        kGoHome = 11,
    };

    struct Entry {
        std::int32_t action = 0;
        // param[0] priority, [1] duration in ticks, [2] weight for the
        // random pick, [3] movement speed (scaled by the monster's speed),
        // [4] and [5] wander pause and its random extra.
        std::int32_t param[9] = {};
        // cond[0] = 1: monster's health must be within cond[1]..cond[2]
        // percent (-1 = no bound). cond[3] = 1: a target must be within
        // cond[4]..cond[5] world units, measured between collision boxes.
        std::int32_t cond[6] = {};
    };

    struct List {
        std::string name;  // Shift-JIS
        std::int32_t walkSpeed = 0;
        std::vector<std::vector<Entry>> events;  // indexed by Event
    };

    std::vector<List> lists;

    // Index of the list with this name, or -1.
    int find(const std::string& name) const;

    static AiControl parse(std::span<const std::uint8_t> data);
};

}  // namespace sf

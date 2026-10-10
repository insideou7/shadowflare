#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace sf {

// Scenario/<id>/Scenario.Scs: a scenario's event script, compiled from text
// by RKC_RPG_SCRIPT.dll (layout from RKC_RPG_SCRIPT::ReadBinary). Triggers
// tie an event on a character or area to a sentence, a list of commands.
struct ScenarioScript {
    // Operand kinds, from RKC_RPG_SCRIPT::AnalyzeOPERAND (the text keyword
    // each stands for in brackets).
    enum OperandType : std::int32_t {
        kOperator = 0,  // in IF: 0 '=', 1 '!', 2 '>', 3 '<'; also sentence numbers
        kConstant = 1,
        kMessage = 2,   // MSG
        kTemp = 4,      // TEMP: flags reset with the scenario
        kNet = 5,       // NET
        kSave = 11,     // SAVE: flags kept in the save game
        kQuest = 12,    // QUEST
    };

    // Opcodes identified so far from how they are used in the game's scripts.
    enum Opcode : std::int32_t {
        kIf = 0,               // IF a op b sentence: runs that sentence when a op b holds
        kSetFlag = 1,          // SETFLAG flag value
        kChangeScenario = 17,  // CHANGESCENARIO scenario entry
    };

    // Trigger events (the "status" field).
    enum Event : std::int32_t {
        kOnJudge = 3,  // the player walks into the character's rectangle
    };

    struct Operand {
        std::int32_t type = 0;
        std::int32_t value = 0;
    };
    struct Command {
        std::int32_t opcode = 0;
        std::vector<Operand> operands;
    };
    struct Trigger {
        std::int32_t network = 0;
        std::int32_t event = 0;
        // In-game id: placement id plus 10,000,000 for objects, 12,000,000
        // for people and so on (see ScenarioControl); -1 = none.
        std::int32_t character = -1;
        std::int32_t sentence = 0;
    };

    std::vector<std::pair<std::int32_t, std::int32_t>> tempFlags;  // id, initial value
    std::map<std::int32_t, std::string> messages;                  // dialogue text by id
    std::vector<Trigger> triggers;
    std::vector<std::vector<Command>> sentences;

    static ScenarioScript parse(std::span<const std::uint8_t> data);
};

}  // namespace sf

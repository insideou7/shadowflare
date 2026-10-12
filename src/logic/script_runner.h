#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "formats/scenario_script.h"

namespace sf {

// Story flags shared by scripts: (operand type, id) -> value.
using ScriptFlags = std::map<std::pair<std::int32_t, std::int32_t>, std::int32_t>;

// What a script asks the game to do. The interpreter is pure rules; the
// game implements these. Ids are in-game character numbers (placement id
// plus ScenarioControl::idBase).
class ScriptHost {
public:
    virtual ~ScriptHost() = default;

    // TALK: shows `text` over `speaker`. When the box closes the game stores
    // the chosen ~option~ (0 = first, -1 = none) in `answer`, unless
    // `answer` is a constant, and runs the speaker's talk-ended triggers.
    struct Talk {
        std::string text;  // already formatted with SETMSGDEFINE's values
        ScenarioScript::Operand answer;
        int defaultChoice = -1;
        std::int32_t speaker = -1;
    };
    virtual void talk(const Talk&) {}
    virtual void initTrade(int /*shop*/, int /*list*/) {}
    virtual void trade(int /*shop*/) {}
    virtual void healLife() {}
    virtual void healMental() {}
    // CREATEITEM: puts an item on the ground near (x, y); for gold
    // (class 4 code 0) `amount` is the number of coins.
    virtual void createItem(int /*cls*/, int /*code*/, int /*x*/, int /*y*/, int /*amount*/) {}
    virtual std::int32_t characterX(std::int32_t /*id*/) const { return 0; }
    virtual std::int32_t characterY(std::int32_t /*id*/) const { return 0; }
    virtual void stopCharacter(std::int32_t /*id*/, bool /*stop*/) {}
    virtual void faceCharacter(std::int32_t /*id*/, std::int32_t /*towards*/) {}
    virtual void setActive(std::int32_t /*id*/, bool /*active*/) {}
    virtual int playerLevel() const { return 1; }
    virtual int playerClass() const { return 0; }  // 0 none, then as setClass
    virtual std::int64_t gold() const { return 0; }
    virtual void payGold(std::int64_t /*amount*/) {}
    virtual int random(int /*n*/) { return 0; }
    virtual void showQuestName(int /*quest*/) {}
    virtual int entryPoint() const { return 0; }  // the entry the party came in by
    virtual void setClass(int /*cls*/) {}         // 1 warrior, 2 magician, 3 hunter
};

// The scenario script interpreter (ShadowFlare.exe 0x430f80; command names
// from RKC_RPG_SCRIPT.dll). Commands run one after another; IF runs another
// sentence and then carries on. Commands that need something the engine
// does not have yet (sound, effects, partners, network play) are skipped.
// No SDL, so it can be unit tested.
class ScriptRunner {
public:
    struct ScenarioChange {
        int scenario = 0;
        int entry = 0;
    };

    // `character` is the in-game id of the character whose trigger fired
    // (-1 for none); TALK with speaker -1 uses it.
    ScriptRunner(const ScenarioScript& script, ScriptFlags& flags, ScriptHost* host = nullptr, std::int32_t character = -1)
        : script_(script), flags_(flags), host_(host), character_(character) {}

    // Runs a sentence; returns the scenario change it asked for, if any
    // (the rest of the sentence is skipped once one is requested).
    std::optional<ScenarioChange> run(std::int32_t sentence);

    std::int32_t value(const ScenarioScript::Operand& op) const;
    void store(const ScenarioScript::Operand& op, std::int32_t value);

    // A message with printf-style %d / %s filled from SETMSGDEFINE's values.
    std::string format(const std::string& text) const;

private:
    void run(std::int32_t sentence, int depth);

    const ScenarioScript& script_;
    ScriptFlags& flags_;
    ScriptHost* host_;
    std::int32_t character_;
    std::optional<ScenarioChange> change_;
};

// SETMSGDEFINE's twenty values live with the flags under this operand type
// so they survive from one sentence to the next, as in the original.
inline constexpr std::int32_t kMessageArgType = -100;

}  // namespace sf

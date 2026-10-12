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
    // CREATEITEMTABLE list x y: drops what a drop list gives there, as a
    // dying monster's (0x417550).
    virtual void createItemTable(int /*list*/, int /*x*/, int /*y*/) {}
    virtual std::int32_t characterX(std::int32_t /*id*/) const { return 0; }
    virtual std::int32_t characterY(std::int32_t /*id*/) const { return 0; }
    virtual void stopCharacter(std::int32_t /*id*/, bool /*stop*/) {}
    virtual void faceCharacter(std::int32_t /*id*/, std::int32_t /*towards*/) {}
    virtual void setActive(std::int32_t /*id*/, bool /*active*/) {}
    virtual int playerLevel() const { return 1; }
    // GETCLASS: the class on offer, which is the hero's own unless one is
    // waiting to be taken (0 mercenary, 1 warrior, 2 hunter, 3 wizard).
    virtual int offeredClass() const { return 0; }
    // GETPLAYERSEX: 1 for the hero, 0 for the heroine (player +0x28, 0x433b1f).
    virtual int playerSex() const { return 1; }
    virtual std::int64_t gold() const { return 0; }
    virtual void payGold(std::int64_t /*amount*/) {}
    virtual int random(int /*n*/) { return 0; }
    virtual void showQuestName(int /*quest*/) {}
    virtual int entryPoint() const { return 0; }  // the entry the party came in by
    // ENABLECLASSCHANGE: offers class 1 warrior, 2 hunter or 3 wizard; the
    // player takes it in the status window.
    virtual void offerClass(int /*cls*/) {}
    // STORAGE: opens the warehouse.
    virtual void openStorage() {}
    // IDENTIFY, CHECKUNOPINIONITEM, REPAIR and GETREPAIRPRICE: see
    // Inventory for the part numbers.
    virtual void identifyItems() {}
    virtual bool hasUnidentifiedItems() const { return false; }
    virtual void repair(int /*part*/) {}
    virtual int repairPrice(int /*part*/) const { return 0; }
    // MESSAGEBOX and MESSAGEBOXVALUE: a line of text floating at (dx, dy)
    // screen pixels from a character, such as an area's name over its warp
    // gate, for `duration` (1000 = a second, inferred).
    struct Label {
        std::int32_t character = -1;
        int dx = 0, dy = 0;
        std::string text;
        std::uint8_t r = 224, g = 224, b = 224;
        int duration = 1000;
    };
    virtual void showLabel(const Label&) {}
    // The hero's companion. GETPARTNERKIND and CHANGEPARTNER (a free swap);
    // GETPARTNERLIFE gives false when there is no companion, which leaves the
    // script's variables alone, and -1 / -1 while it is down.
    virtual int partnerKind() const { return 0; }
    virtual void changePartner(int /*kind*/) {}
    virtual bool partnerLife(int& /*hp*/, int& /*max*/) const { return false; }
    // TALKPARTNERSTATUS: the status sheet of a kind of companion.
    virtual std::string partnerStatus(int /*kind*/) const { return {}; }
};

// The scenario script interpreter (ShadowFlare.exe 0x430f80; command names
// from RKC_RPG_SCRIPT.dll). Commands run one after another; IF runs another
// sentence and then carries on. Commands that need something the engine
// does not have yet (sound, effects, network play) are skipped.
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

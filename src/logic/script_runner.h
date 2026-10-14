#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "formats/scenario_script.h"
#include "logic/rules.h"

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
    // ABSOLUTEOBJECTFLAG id shown usable blocks: from now on the object
    // keeps these three states and no longer follows its NET activity
    // flags (0x433a78 sets object + 0xfc to 1 and + 0x100 to + 0x108).
    virtual void setObjectStates(std::int32_t /*id*/, int /*shown*/, int /*usable*/, int /*blocks*/) {}
    // CCHECK id: 1 while the player holds the left button on object `id`
    // and stands within reach of it, as a Brain Globe needs (0x4346b0).
    virtual bool workingObject(std::int32_t /*id*/) const { return false; }
    // SETUNLOCKSW on: Player/Common/UnlockSW plays round the hero for the
    // frame (player + 0x159c, drawn by 0x434ef0, cleared every frame).
    virtual void setUnlockSwitch(bool /*on*/) {}
    virtual int playerLevel() const { return 1; }
    // GETCLASS: the class on offer, which is the hero's own unless one is
    // waiting to be taken (0 mercenary, 1 warrior, 2 hunter, 3 wizard).
    virtual int offeredClass() const { return 0; }
    // GETPLAYERSEX: 1 for the hero, 0 for the heroine (player +0x28, 0x433b1f).
    virtual int playerSex() const { return 1; }
    virtual std::int64_t gold() const { return 0; }
    virtual void payGold(std::int64_t /*amount*/) {}
    virtual int random(int /*n*/) { return 0; }
    // DRAWQUESTNAME: the mission's title along the bottom of the screen
    // for a while (ShadowFlare.exe 0x433868, drawn by 0x4050f0).
    virtual void showQuestName(int /*quest*/) {}
    // SETCOMPASSES msg: the area's name beside the compass at the top left
    // as the player arrives (0x43389b, drawn by 0x412220).
    virtual void showAreaName(const std::string& /*name*/) {}
    virtual int entryPoint() const { return 0; }  // the entry the party came in by
    // ENABLECLASSCHANGE: offers class 1 warrior, 2 hunter or 3 wizard; the
    // player takes it in the status window.
    virtual void offerClass(int /*cls*/) {}
    // STORAGE: opens the warehouse, or with a number other than 0 the
    // Giant Warehouse and its ten pages (0x434a60 case 0x29).
    virtual void openStorage(bool /*giant*/) {}
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
    // CHECKENEMYLIVE and CHECKENEMYDEAD: whether enemy `id` is alive,
    // which every enemy placed in the scenario is until killed, active or
    // not; empty when the scenario has no such enemy.
    virtual std::optional<bool> enemyAlive(std::int32_t /*id*/) const { return std::nullopt; }
    // CHECKITEMEXIST and DELETEITEM: an item anywhere the player keeps
    // them (special items, the bag, worn), and taking one of them away.
    virtual bool hasItem(int /*cls*/, int /*code*/) const { return false; }
    virtual void deleteItem(int /*cls*/, int /*code*/) {}
    // GETPLAYERLIFE and GETPLAYERMENTAL: the hero's HP or MP and its most.
    virtual void playerLife(int& /*hp*/, int& /*max*/) const {}
    virtual void playerMental(int& /*mp*/, int& /*max*/) const {}
    // SETITEMDIRECT class code: hands the player the item, unless they
    // already have one (the original keeps it with the special items).
    virtual void giveItem(int /*cls*/, int /*code*/) {}
    // WARPGATE n: gate n opens the GATE window, the list of the gates found
    // (WARP flags), unless it or a shop is open already. DELETEWARPGATE n
    // closes it again, but only if gate n opened it (0x4334da, 0x433544).
    virtual void openWarpGate(int /*gate*/) {}
    virtual void closeWarpGate(int /*gate*/) {}
    // GETEXPERIENCE: `percent` of the experience the hero's level needs.
    virtual void giveExperiencePercent(int /*percent*/) {}
    // Traps and map effects (docs/traps.md). CREATEOPTIONANGLE fires a
    // trap's arrow or fire jet; the runner has already moved its start
    // `distance` out along its angle (0x4140a0).
    struct TrapFire {
        int kind = 0;        // 0 arrow, 2 fire jet, 1 and 3 others (see trapShot)
        int x = 0, y = 0;
        double angle = 0;    // radians, world y up
        int speed = 0;       // world units an original tick
        int power = 0;
        bool fixedDamage = false;  // the power is the damage, defense or not
        int height = 0;
        int direction = -1;  // -1: from the angle
        // Hit words 0x29, 0x2b, 0x28 and 0x48: the stun's chance (percent,
        // -1 for table 25's) and length (original ticks, -1 likewise),
        // whether it is steady (stuns past 15 ticks and does not push),
        // and whether the hit may splash (11000024 or 25, one time in five).
        struct Stun {
            int chance = 0, length = 0;
            bool steady = false, splash = false;
        } stun;
    };
    virtual void fireTrap(const TrapFire&) {}
    // CREATEEFFECT number x y height direction: an effect where it stands;
    // CREATEEFFECTCHAR number character: one that stays on a character
    // (0-3 the players).
    virtual void createEffect(int /*number*/, int /*x*/, int /*y*/, int /*height*/, int /*direction*/) {}
    virtual void createEffectOn(int /*number*/, std::int32_t /*character*/) {}
    // GETNEARPLAYERPOS: the living player nearest `character` whose judge
    // rectangle is min..max from its own (-1 for no limit); false for none.
    virtual bool nearestPlayer(std::int32_t /*character*/, int /*min*/, int /*max*/, int& /*player*/, int& /*x*/, int& /*y*/) const { return false; }
    // CALCMYPLAYERDIST: the gap between the hero and `character`, -1 for no
    // such character.
    virtual int playerDistance(std::int32_t /*character*/) const { return -1; }
    // RESSURECT enemy x y direction: a dead enemy back at full health there.
    virtual void resurrect(std::int32_t /*enemy*/, int /*x*/, int /*y*/, int /*direction*/) {}
    // SETCHARTRANS and GETCHARTRANS: a scenario object's opacity (+0xf4,
    // 0-1000); objects only, empty for anything else.
    virtual void setObjectOpacity(std::int32_t /*object*/, int /*opacity*/) {}
    virtual std::optional<int> objectOpacity(std::int32_t /*object*/) const { return std::nullopt; }
    // STARTVISUAL n: a story picture over the screen (0 the loading one).
    virtual void startVisual(int /*picture*/) {}
    // SETTINGRAIN r g b count: `count` more raindrops of that colour.
    virtual void rain(int /*r*/, int /*g*/, int /*b*/, int /*count*/) {}
    // MEMORIZEMAGIC spell and CHECKMEMORIZEDMAGIC spell var: a spell the
    // hero is taught, and whether they know it.
    virtual void memorizeSpell(int /*spell*/) {}
    virtual bool knowsSpell(int /*spell*/) const { return false; }
    // PLAYSOUND voice everywhere x y (0x430f80 case 16): a sound effect of
    // Voice00.Voc at a spot, x -1 meaning where the player stands. Unless
    // `everywhere`, only heard near the player.
    virtual void playSound(int /*voice*/, bool /*everywhere*/, int /*x*/, int /*y*/) {}
    // A command the engine does not carry out yet ran, so a
    // playthrough can list what a scenario still lacks.
    virtual void skippedCommand(std::int32_t /*opcode*/) {}
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

    // Original mode's SETQUESTFLAG (see Rules::missionsClearOnlyWhenTaken).
    void setRules(const Rules& rules) { rules_ = rules; }

    // Whether the engine carries the command out.
    static bool implemented(std::int32_t opcode);

private:
    void run(std::int32_t sentence, int depth);

    const ScenarioScript& script_;
    ScriptFlags& flags_;
    ScriptHost* host_;
    std::int32_t character_;
    std::optional<ScenarioChange> change_;
    Rules rules_;
};

// SETMSGDEFINE's twenty values live with the flags under this operand type
// so they survive from one sentence to the next, as in the original.
inline constexpr std::int32_t kMessageArgType = -100;
// The original's other mission array (0x48cebc): set once a script has
// cleared a mission, taken or not. It is not saved, so it lives under a
// negative type with the flags, which saving skips.
inline constexpr std::int32_t kQuestClearedType = -101;
// The NET flags a placement's activity is kept in (see
// ScenarioControl::Placement::states): 100,000,000 + id (active) and so on.
inline constexpr std::array<std::int32_t, 3> kStateFlagBases{100000000, 300000000, 200000000};

}  // namespace sf

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
        kEnemyAlive = 3,  // ELIV n: whether enemy 14,000,000 + n is alive
        kTemp = 4,      // TEMP: flags reset with the scenario
        kNet = 5,       // NET
        kSave = 11,     // SAVE: flags kept in the save game
        kQuest = 12,    // QUEST
        // Read-only values (ShadowFlare.exe 0x4346b0): 3 ELIV, 6 CPOSX and
        // 7 CPOSY (a character's position), 8 PLAYMODE, 9 CCHECK, 10 WARP,
        // 13 EXSTRG.
        kCharacterX = 6,
        kCharacterY = 7,
        kPlayMode = 8,
        kObjectCheck = 9,  // CCHECK id: whether the player is working object `id`
    };

    // Command numbers, with the names RKC_RPG_SCRIPT.dll compiles them from
    // (its keyword table), and the operands they take.
    enum Opcode : std::int32_t {
        kIf = 0,                // IF a op b sentence: runs that sentence when a op b holds
        kSetFlag = 1,           // SETFLAG flag value
        kTalk = 2,              // TALK message answer mode default speaker height
        kTalkPartnerStatus = 3,
        kIdentify = 4,          // IDENTIFY: identifies everything the player carries
        kTrade = 5,             // TRADE shop: opens a shop
        kInitTrade = 6,         // INITTRADE shop list: stocks a shop from Table.Tbd table 32 row `list`
        kHealLife = 7,          // HEALLIFE
        kHealMental = 8,        // HEALMENTAL
        kRepair = 9,            // REPAIR part: 0 weapon, 1 helmet, 2 body, 3 shield, 4 boots, -1 the bag
        kCreateItem = 10,       // CREATEITEM class code x y min max
        kAdd = 11, kSub = 12, kMul = 13, kDiv = 14, kRem = 15,  // ADD var value...
        kPlaySound = 16,        // PLAYSOUND sound ? x y
        kChangeScenario = 17,   // CHANGESCENARIO scenario entry
        kStop = 18,             // C_STOP character: stops it wandering (to talk)
        kMovable = 19,          // C_MOVABLE character: lets it wander again
        kActionRequest = 20,    // C_ACTIONREQ
        kSetAngleTo = 21,       // C_SETANGLETO character target (0 = the player)
        kSetActive = 22,        // SETACTIVE character
        kSetInactive = 23,      // SETINACTIVE character
        kCreateItemTable = 24,
        kResurrect = 25,
        kMessageBoxValue = 26,
        kMessageBox = 27,
        kRoutine = 28,          // ROUTIN n: runs the event 6 triggers for n, then carries on
        kRoutineNet = 29,       // ROUTINNET n: the same through the network host (network play only)
        kCreateOptionAngle = 30,
        kCheckEnemyLive = 31, kCheckEnemyDead = 32,
        kGetNearPlayerPos = 33,
        kCalcMyPlayerDist = 34,
        kCalcAngle = 35,
        kCreateEffect = 36,
        kWarpGate = 37, kDeleteWarpGate = 38,
        kGetRandom = 39,        // GETRANDAM min max var
        kCreateEffectChar = 40,
        kStorage = 41,
        kGetPlayerLife = 42, kGetPlayerMental = 43,
        kGetPartnerKind = 44, kChangePartner = 45,
        kSetCharTrans = 46, kGetCharTrans = 47,
        kDrawQuestName = 48,    // DRAWQUESTNAME quest
        kSetCompasses = 49,
        kGetEntryPoint = 50,    // GETENTRYPOINT var
        kSetMessageDefine = 51, // SETMSGDEFINE v0..v19: values for %d in the next messages
        kGetRepairPrice = 52,   // GETREPAIRPRICE slot var
        kGetTotalGold = 53,     // GETTOTALGOLD var
        kPayGold = 54,          // PAYGOLD amount
        kCheckUnopinionItem = 55,  // CHECKUNOPINIONITEM var: 1 if anything is unidentified
        kAbsoluteObjectFlag = 56,  // ABSOLUTEOBJECTFLAG object shown usable blocks
        kGetPlayerSex = 57,
        kCheckItemExist = 58, kDeleteItem = 59,
        kSetUnlockSwitch = 60,  // SETUNLOCKSW on: the unlocking glow round the hero this frame
        kGetPlayerLevel = 61,   // GETPLAYERLEVEL var
        kSetQuestFlag = 62,     // SETQUESTFLAG quest state ?: 1 taken, 2 cleared
        kGetPartnerLife = 63,
        kStartVisual = 64,
        kSettingRain = 65,
        kGetMyPlayerNo = 66,
        kMemorizeMagic = 67, kGetExperience = 68, kCheckMemorizedMagic = 69,
        kEnableClassChange = 70, kGetClass = 71,
        kChangeColor = 72, kStartCardGame = 73, kGetCardGameResult = 74,
        kSetItemDirect = 75,
    };

    // Trigger events (the "status" field).
    // (ShadowFlare.exe 0x4304b0 to 0x430a40.)
    enum Event : std::int32_t {
        kOnTalk = 0,       // the player speaks to the character
        kOnTalkEnd = 1,    // the character's message box closed
        kOnJudge = 3,      // the player walks into the character's rectangle
        kOnEnemyDead = 4,  // an enemy died (inferred from where the exe raises it)
        kEveryTick = 5,
        kOnRoutine = 6,    // ROUTIN `character` runs these, as a subroutine (0x4309f0)
        kOnStart = 7,      // the scenario was entered
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
    // NET flags and their starting values. Out of network play they too
    // start again at every scenario load (0x4300e0).
    std::vector<std::pair<std::int32_t, std::int32_t>> netFlags;
    std::map<std::int32_t, std::string> messages;                  // dialogue text by id
    std::vector<Trigger> triggers;
    std::vector<std::vector<Command>> sentences;

    static ScenarioScript parse(std::span<const std::uint8_t> data);

    // A command's name, such as "TALK" for 2 (empty for unknown numbers),
    // and the number for a name.
    static std::string_view commandName(std::int32_t opcode);
    static std::optional<std::int32_t> commandNumber(std::string_view name);
};

}  // namespace sf

#include "formats/scenario_script.h"

#include <array>

#include "formats/binary_reader.h"

namespace sf {

namespace {

std::int32_t count(BinaryReader& in, std::size_t itemSize) {
    const std::int32_t n = in.i32();
    if (n < 0 || static_cast<std::size_t>(n) * itemSize > in.remaining()) throw FormatError("bad count in script file");
    return n;
}

// Command names by number, as RKC_RPG_SCRIPT.dll's keyword table spells
// them (GETRANDAM and RESSURECT included).
constexpr std::array<std::string_view, 76> kCommandNames = {
    "IF", "SETFLAG", "TALK", "TALKPARTNERSTATUS", "IDENTIFY", "TRADE",
    "INITTRADE", "HEALLIFE", "HEALMENTAL", "REPAIR", "CREATEITEM", "ADD",
    "SUB", "MUL", "DIV", "REM", "PLAYSOUND", "CHANGESCENARIO",
    "C_STOP", "C_MOVABLE", "C_ACTIONREQ", "C_SETANGLETO", "SETACTIVE", "SETINACTIVE",
    "CREATEITEMTABLE", "RESSURECT", "MESSAGEBOXVALUE", "MESSAGEBOX", "ROUTIN", "ROUTINNET",
    "CREATEOPTIONANGLE", "CHECKENEMYLIVE", "CHECKENEMYDEAD", "GETNEARPLAYERPOS", "CALCMYPLAYERDIST", "CALCANGLE",
    "CREATEEFFECT", "WARPGATE", "DELETEWARPGATE", "GETRANDAM", "CREATEEFFECTCHAR", "STORAGE",
    "GETPLAYERLIFE", "GETPLAYERMENTAL", "GETPARTNERKIND", "CHANGEPARTNER", "SETCHARTRANS", "GETCHARTRANS",
    "DRAWQUESTNAME", "SETCOMPASSES", "GETENTRYPOINT", "SETMSGDEFINE", "GETREPAIRPRICE", "GETTOTALGOLD",
    "PAYGOLD", "CHECKUNOPINIONITEM", "ABSOLUTEOBJECTFLAG", "GETPLAYERSEX", "CHECKITEMEXIST", "DELETEITEM",
    "SETUNLOCKSW", "GETPLAYERLEVEL", "SETQUESTFLAG", "GETPARTNERLIFE", "STARTVISUAL", "SETTINGRAIN",
    "GETMYPLAYERNO", "MEMORIZEMAGIC", "GETEXPERIENCE", "CHECKMEMORIZEDMAGIC", "ENABLECLASSCHANGE", "GETCLASS",
    "CHANGECOLOR", "STARTCARDGAME", "GETCARDGAMERESULT", "SETITEMDIRECT",
};

}  // namespace

std::string_view ScenarioScript::commandName(std::int32_t opcode) {
    return opcode >= 0 && opcode < static_cast<std::int32_t>(kCommandNames.size()) ? kCommandNames[static_cast<std::size_t>(opcode)]
                                                                                     : std::string_view();
}

std::optional<std::int32_t> ScenarioScript::commandNumber(std::string_view name) {
    for (std::size_t i = 0; i < kCommandNames.size(); ++i) {
        if (kCommandNames[i] == name) return static_cast<std::int32_t>(i);
    }
    return std::nullopt;
}

ScenarioScript ScenarioScript::parse(std::span<const std::uint8_t> data) {
    BinaryReader in(data);
    if (!in.chars(16).starts_with("ScenaScriptV000")) throw FormatError("not a scenario script");
    ScenarioScript s;
    for (std::int32_t i = 0, n = count(in, 8); i < n; ++i) {
        const std::int32_t id = in.i32();
        s.tempFlags.emplace_back(id, in.i32());
    }
    for (std::int32_t i = 0, n = count(in, 8); i < n; ++i) {
        const std::int32_t id = in.i32();
        s.netFlags.emplace_back(id, in.i32());
    }
    for (std::int32_t i = 0, n = count(in, 8); i < n; ++i) {
        const std::int32_t id = in.i32();
        std::string text(in.chars(static_cast<std::size_t>(count(in, 1))));
        for (char& c : text) c = static_cast<char>(~c);  // stored bitwise inverted
        s.messages[id] = std::move(text);
    }
    for (std::int32_t i = 0, n = count(in, 16); i < n; ++i) {
        Trigger t;
        t.network = in.i32();
        t.event = in.i32();
        t.character = in.i32();
        t.sentence = in.i32();
        s.triggers.push_back(t);
    }
    s.sentences.resize(static_cast<std::size_t>(count(in, 4)));
    for (auto& sentence : s.sentences) {
        sentence.resize(static_cast<std::size_t>(count(in, 8)));
        for (auto& command : sentence) {
            command.opcode = in.i32();
            command.operands.resize(static_cast<std::size_t>(count(in, 8)));
            for (auto& op : command.operands) {
                op.type = in.i32();
                op.value = in.i32();
            }
        }
    }
    return s;
}

}  // namespace sf

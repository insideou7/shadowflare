// Unit tests for the game rules that need neither SDL nor game data.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "logic/ai_select.h"
#include "logic/archery.h"
#include "logic/explored_map.h"
#include "logic/lighting.h"
#include "logic/item_labels.h"
#include "formats/table_data.h"
#include "logic/magic.h"
#include "logic/mines.h"
#include "logic/movement.h"
#include "logic/partner.h"
#include "logic/rules.h"
#include "logic/progression.h"
#include "logic/script_runner.h"
#include "logic/settings.h"
#include "logic/shop.h"
#include "audio/mixer.h"
#include "logic/traps.h"
#include "formats/item_data.h"
#include "formats/warp_gates.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

using Op = sf::ScenarioScript::Operand;
using Cmd = sf::ScenarioScript::Command;

Op constant(int v) { return {sf::ScenarioScript::kConstant, v}; }
Op save(int id) { return {sf::ScenarioScript::kSave, id}; }
Op raw(int v) { return {sf::ScenarioScript::kOperator, v}; }

Cmd ifCmd(Op a, int op, Op b, int sentence) { return {sf::ScenarioScript::kIf, {a, raw(op), b, raw(sentence)}}; }
Cmd setFlag(Op flag, Op value) { return {sf::ScenarioScript::kSetFlag, {flag, value}}; }
Cmd change(int scenario, int entry) { return {sf::ScenarioScript::kChangeScenario, {constant(scenario), constant(entry)}}; }

void testScriptExit() {
    // Sentence 0 is an exit guarded by SAVE[71] != 0 (like scenario 1's
    // exit to 2999999); sentence 1 leads there.
    sf::ScenarioScript script;
    script.sentences = {{ifCmd(save(71), 1, constant(0), 1)}, {change(2999999, 0)}};
    sf::ScriptFlags flags;
    check(!sf::ScriptRunner(script, flags).run(0), "guarded exit stays shut with a fresh save");
    flags[{sf::ScenarioScript::kSave, 71}] = 1;
    const auto c = sf::ScriptRunner(script, flags).run(0);
    check(c && c->scenario == 2999999 && c->entry == 0, "guarded exit opens once the flag is set");
}

void testScriptFlagsAndOperators() {
    sf::ScenarioScript script;
    script.sentences = {
        {setFlag(save(5), constant(3)), ifCmd(save(5), 2, constant(2), 1), ifCmd(save(5), 3, constant(2), 2)},
        {setFlag(save(6), constant(1))},  // runs: 3 > 2
        {setFlag(save(7), constant(1))},  // does not run: 3 < 2 is false
    };
    sf::ScriptFlags flags;
    check(!sf::ScriptRunner(script, flags).run(0), "no scenario change");
    check(flags[{sf::ScenarioScript::kSave, 5}] == 3, "SETFLAG stores the value");
    check(flags[{sf::ScenarioScript::kSave, 6}] == 1, "IF > runs its branch");
    check(flags[{sf::ScenarioScript::kSave, 7}] == 0, "IF < skips its branch");
}

void testScriptStopsAtChange() {
    sf::ScenarioScript script;
    script.sentences = {{change(3, 5), setFlag(save(1), constant(9)), change(4, 0)}};
    sf::ScriptFlags flags;
    const auto c = sf::ScriptRunner(script, flags).run(0);
    check(c && c->scenario == 3 && c->entry == 5, "first CHANGESCENARIO wins");
    check(flags[{sf::ScenarioScript::kSave, 1}] == 0, "commands after a scenario change are skipped");
    check(!sf::ScriptRunner(script, flags).run(7), "out of range sentence is ignored");
}

void testScriptQuestFlags() {
    // SETQUESTFLAG 0 2 0 clears a mission only once it was taken (0x433f29).
    using S = sf::ScenarioScript;
    auto quest = [](int q, int state) { return Cmd{S::kSetQuestFlag, {constant(q), constant(state), constant(0)}}; };
    S script;
    script.sentences = {{quest(0, 2)}, {quest(0, 1)}};
    sf::ScriptFlags flags;
    sf::ScriptRunner(script, flags).run(0);
    check(flags[{S::kQuest, 0}] == 0, "a mission not taken is not cleared");
    sf::ScriptRunner(script, flags).run(1);
    check(flags[{S::kQuest, 0}] == 1, "SETQUESTFLAG 1 takes the mission");
    sf::ScriptRunner(script, flags).run(0);
    check(flags[{S::kQuest, 0}] == 2, "SETQUESTFLAG 2 clears a taken mission");

    // Original mode: the first clear is spent even on a mission not taken,
    // so the mission stays taken once it is given (0x48cebc).
    sf::ScriptFlags original;
    auto run = [&](int sentence) {
        sf::ScriptRunner runner(script, original);
        runner.setRules(sf::Rules::original());
        runner.run(sentence);
    };
    run(0);
    run(1);
    run(0);
    check(original[{S::kQuest, 0}] == 1, "original mode: a mission cleared before it was taken stays taken");
    check(original[{sf::kQuestClearedType, 0}] == 1, "original mode remembers the clear");

    // 0x41 for any state but 2, 0x42 for clearing a taken mission, heard everywhere.
    struct Host : sf::ScriptHost {
        std::vector<int> voices;
        void playSound(int voice, bool everywhere, int x, int) override {
            if (everywhere && x == -1) voices.push_back(voice);
        }
    } host;
    sf::ScriptFlags sounded;
    for (int sentence : {0, 1, 1, 0, 0}) sf::ScriptRunner(script, sounded, &host).run(sentence);
    check(host.voices == std::vector<int>{0x41, 0x41, 0x42}, "SETQUESTFLAG sounds");
}

void testScriptMissionNames() {
    // DRAWQUESTNAME names a mission; SETCOMPASSES names the area from a message.
    using S = sf::ScenarioScript;
    struct Host : sf::ScriptHost {
        int quest = -1;
        std::string area;
        void showQuestName(int q) override { quest = q; }
        void showAreaName(const std::string& name) override { area = name; }
    } host;
    S script;
    script.messages[1000059] = "Remote Town\n";
    script.sentences = {{Cmd{S::kSetCompasses, {{S::kMessage, 1000059}}}, Cmd{S::kDrawQuestName, {constant(3)}}}};
    sf::ScriptFlags flags;
    sf::ScriptRunner(script, flags, &host).run(0);
    check(host.area == "Remote Town\n", "SETCOMPASSES passes its message on");
    check(host.quest == 3, "DRAWQUESTNAME passes its mission on");
}

sf::AiControl::Entry entry(int action, int priority, int weight, int minGap = -2, int maxGap = -2) {
    sf::AiControl::Entry e;
    e.action = action;
    e.param[0] = priority;
    e.param[2] = weight;
    if (minGap != -2) {
        e.cond[3] = 1;
        e.cond[4] = minGap;
        e.cond[5] = maxGap;
    }
    return e;
}

void testAiDistanceCondition() {
    // A bat: attack when touching, otherwise close in.
    sf::AiControl::List list;
    list.events.resize(18);
    list.events[0] = {entry(sf::AiControl::kApproach, 0, 100, 46, 2500), entry(sf::AiControl::kAttack, 0, 100, 0, 45)};
    sf::Random random;
    const auto* near = sf::selectAiEntry(list, 0, {10, 100}, random);
    const auto* far = sf::selectAiEntry(list, 0, {800, 100}, random);
    const auto* gone = sf::selectAiEntry(list, 0, {5000, 100}, random);
    check(near && near->action == sf::AiControl::kAttack, "attacks when touching");
    check(far && far->action == sf::AiControl::kApproach, "closes in from a distance");
    check(!gone, "nothing applies out of range");
}

void testAiPriorityAndWeights() {
    sf::AiControl::List list;
    list.events.resize(18);
    // Low priority first, then a higher one that should wipe it out.
    list.events[0] = {entry(sf::AiControl::kWait, 0, 100), entry(sf::AiControl::kWander, 100, 100)};
    sf::Random random;
    for (int i = 0; i < 20; ++i) {
        const auto* e = sf::selectAiEntry(list, 0, {0, 100}, random);
        check(e && e->action == sf::AiControl::kWander, "higher priority wins");
    }
    // Equal priority: both get picked, roughly by weight.
    list.events[0] = {entry(sf::AiControl::kWait, 0, 300), entry(sf::AiControl::kWander, 0, 100)};
    int waits = 0;
    for (int i = 0; i < 4000; ++i) waits += sf::selectAiEntry(list, 0, {0, 100}, random)->action == sf::AiControl::kWait;
    check(waits > 2700 && waits < 3300, "weights split picks about 3:1");
    // A zero total weight means no choice.
    list.events[0] = {entry(sf::AiControl::kWait, 0, 0)};
    check(!sf::selectAiEntry(list, 0, {0, 100}, random), "zero weight picks nothing");
}

void testAiHealthCondition() {
    sf::AiControl::List list;
    list.events.resize(18);
    auto flee = entry(sf::AiControl::kRetreat, 0, 100);
    flee.cond[0] = 1;
    flee.cond[1] = -1;
    flee.cond[2] = 30;  // only at 30% health or less
    list.events[17] = {flee};
    sf::Random random;
    check(!sf::selectAiEntry(list, 17, {0, 80}, random), "healthy monster does not flee");
    check(sf::selectAiEntry(list, 17, {0, 20}, random) != nullptr, "wounded monster flees");
}

void testAiSameSeedSameChoices() {
    sf::AiControl::List list;
    list.events.resize(18);
    list.events[0] = {entry(sf::AiControl::kWait, 0, 50), entry(sf::AiControl::kWander, 0, 50)};
    sf::Random a(7), b(7);
    bool same = true;
    for (int i = 0; i < 100; ++i) same &= sf::selectAiEntry(list, 0, {0, 100}, a) == sf::selectAiEntry(list, 0, {0, 100}, b);
    check(same, "same seed, same choices");
}

// A 4x4 judge grid (160 world units per cell) with one blocked cell at (2, 1).
sf::GroundLayer smallGround() {
    sf::GroundLayer g;
    g.judgeWidth = g.judgeHeight = 4;
    g.judge.assign(16, 0);
    g.judge[1 * 4 + 2] = sf::GroundLayer::kBlocked;
    return g;
}

void testMovement() {
    const auto g = smallGround();
    double x = 80, y = 80;
    check(sf::tryMove(g, x, y, 160, 160) && x == 240 && y == 240, "free diagonal step");
    x = 240, y = 80;  // cell (1, 0); (2, 1) is blocked
    check(sf::tryMove(g, x, y, 160, 160) && x == 400 && y == 80, "diagonal into a corner slides along x");
    x = 240, y = 240;  // cell (1, 1); east is blocked
    check(!sf::tryMove(g, x, y, 160, 0) && x == 240, "walking straight into a wall stops");
    x = 80, y = 80;
    check(!sf::tryMove(g, x, y, -160, 0), "the edge of the grid is a wall");
}

void testLevels() {
    const auto curve = sf::LevelCurve::standard();
    check(curve.need(1) == 25 && curve.need(2) == 80 && curve.need(3) == 150, "the original's first levels");
    check(curve.maxLevel() == 99 && curve.need(98) == 700000000, "the curve tops out at level 99");
    check(curve.need(99) == 0 && curve.need(0) == 0, "no need outside the curve");

    // A Table.Tbd table 13 like the original's: amounts per level (which
    // can repeat), then 999999999 and 0.
    sf::GameTables tables;
    auto& t = tables.tables[13];
    t.rows = 6;
    t.cols = 1;
    t.values = {25, 80, 11000000, 11000000, 999999999, 0};
    t.strings.resize(6);
    const auto original = sf::LevelCurve::fromTables(tables);
    check(original.needs.size() == 4, "the table ends at 999999999, not where amounts repeat");
    check(original.need(4) == 11000000, "repeated amounts are kept");
    check(sf::LevelCurve::fromTables(sf::GameTables{}).needs.empty(), "no table 13, no curve");

    using S = sf::HeroStats;
    const auto start = sf::heroStats(1);
    check(start[S::kHp] == 150 && start[S::kAttack] == 10 && start[S::kDefense] == 10 && start[S::kHit] == 40 &&
              start[S::kEvasion] == 40,
          "level 1 has the original's starting stats");
    const auto five = sf::heroStats(5, sf::HeroClass::Mercenary);
    check(five[S::kHp] == 310 && five[S::kAttack] == 26 && five[S::kMagicAttack] == 18, "four classless levels");
    check(sf::heroStats(6)[S::kHp] == 360 && sf::heroStats(26)[S::kHp] == 1350, "a warrior gains 50 health for 20 levels");
    check(sf::heroStats(35)[S::kHp] == 1710 && sf::heroStats(36)[S::kHp] == 1740, "then 40, then 30");
    check(sf::heroStats(16)[S::kAttack] == 26 + 90 + 7, "a warrior's attack: 9 for 10 levels, then 7");
    check(sf::heroStats(10, sf::HeroClass::Wizard)[S::kMagicAttack] == 18 + 60, "a wizard's magic");
    check(sf::heroStats(9, sf::HeroClass::Hunter)[S::kAttackSpeed] == 102, "a hunter's speed every second level");
    // The heroine: table 901, then 903/905/907.
    using C = sf::HeroClass;
    constexpr auto F = sf::HeroSex::Female;
    const auto her = sf::heroStats(1, C::Warrior, F);
    check(her[S::kHp] == 140 && her[S::kMp] == 160 && her[S::kAttack] == 8 && her[S::kDefense] == 12,
          "the heroine starts with table 901's stats");
    const auto herFive = sf::heroStats(5, C::Mercenary, F);
    check(herFive[S::kHp] == 300 && herFive[S::kAttack] == 20 && herFive[S::kDefense] == 32, "her classless levels");
    check(sf::heroStats(27, C::Wizard)[S::kMp] == 1230 && sf::heroStats(27, C::Wizard, F)[S::kMp] == 1250,
          "a witch gains 50 magic a level for one level longer than a wizard");
    check(sf::heroStats(20, C::Warrior, F)[S::kHp] - sf::heroStats(20, C::Warrior)[S::kHp] == -10,
          "a heroine warrior grows as the hero does");
    check(sf::className(C::Wizard, F) == "Witch" && sf::className(C::Wizard) == "Wizard", "a wizard heroine is a witch");

    check(sf::hitChance(140, 0) == 98 && sf::hitChance(50, 60) == 20 && sf::hitChance(70, 20) == 50,
          "hit chance is hit minus evasion, from 20 to 98");
    check(sf::meleeDamage(30, 10, 0) == 17 && sf::meleeDamage(30, 10, 1) == 20 && sf::meleeDamage(30, 10, 2) == 23,
          "damage is 90-110% of attack minus defence");
    check(sf::meleeDamage(10, 30, 1) == 1, "at least 1 damage");
    check(sf::defaultExperience(30, 4) == 14, "a goblin's default experience");
}

}  // namespace

// A host that records what the script asked for.
struct TestHost : sf::ScriptHost {
    std::vector<Talk> talks;
    std::vector<std::pair<int, int>> trades;
    int healed = 0, chosenClass = 0;
    std::int64_t purse = 500;
    void talk(const Talk& t) override { talks.push_back(t); }
    void initTrade(int shop, int list) override { trades.emplace_back(shop, list); }
    void healLife() override { ++healed; }
    int playerLevel() const override { return 7; }
    std::int64_t gold() const override { return purse; }
    void payGold(std::int64_t amount) override { purse -= amount; }
    void offerClass(int cls) override { chosenClass = cls; }
    int storages = 0;
    std::vector<Label> labels;
    void openStorage() override { ++storages; }
    void showLabel(const Label& l) override { labels.push_back(l); }
    bool unidentified = true;
    std::vector<int> repaired;
    void identifyItems() override { unidentified = false; }
    bool hasUnidentifiedItems() const override { return unidentified; }
    void repair(int part) override { repaired.push_back(part); }
    int repairPrice(int part) const override { return part == -1 ? 75 : 10 + part; }
    int kind = 2;
    std::optional<std::pair<int, int>> life;  // none: no companion
    int partnerKind() const override { return kind; }
    void changePartner(int k) override { kind = k; }
    bool partnerLife(int& hp, int& max) const override {
        if (!life) return false;
        hp = life->first;
        max = life->second;
        return true;
    }
    std::string partnerStatus(int k) const override { return k == 4 ? "Harley\nLevel 1" : ""; }
    std::vector<int> gatesOpened, gatesClosed;
    void openWarpGate(int gate) override { gatesOpened.push_back(gate); }
    void closeWarpGate(int gate) override { gatesClosed.push_back(gate); }
};

// A town gate's script, as Remote Town's (Scenario/00000000 sentences
// 150-157): standing on it finds the gate, a click opens the GATE window
// and walking away closes it.
void testScriptWarpGates() {
    using S = sf::ScenarioScript;
    const Op warp{S::kWarp, 4};
    S script;
    script.sentences = {{ifCmd(warp, 0, constant(0), 1)},
                        {setFlag(warp, constant(1))},
                        {{S::kWarpGate, {constant(1)}}},
                        {{S::kDeleteWarpGate, {constant(1)}}}};
    sf::ScriptFlags flags;
    TestHost host;
    sf::ScriptRunner(script, flags, &host).run(0);
    check(flags[{S::kWarp, 4}] == 1, "SETFLAG marks a warp gate found");
    check(sf::ScriptRunner(script, flags).value(warp) == 1, "WARP reads the gate's flag");
    sf::ScriptRunner(script, flags, &host).run(2);
    sf::ScriptRunner(script, flags, &host).run(3);
    check(host.gatesOpened == std::vector<int>{1}, "WARPGATE opens the window for its gate");
    check(host.gatesClosed == std::vector<int>{1}, "DELETEWARPGATE closes it for its gate");
    check(sf::ScriptRunner::implemented(S::kWarpGate) && sf::ScriptRunner::implemented(S::kDeleteWarpGate),
          "the warp gate commands are carried out");
}

// Table.Tbd table 40: a gate's name, scenario and entry point.
void testWarpGateTable() {
    sf::GameTables tables;
    sf::GameTables::Table t;
    t.rows = 2;
    t.cols = 3;
    t.values = {0, 0, 50, 0, 3900000, 50};
    t.strings = {"Remote Town", "", "", "South Camp", "", ""};
    tables.tables[40] = t;
    const auto gates = sf::WarpGates::fromTables(tables);
    check(gates.count() == 2 && gates.find(1) && gates.find(1)->name == "South Camp" && gates.find(1)->scenario == 3900000 &&
              gates.find(1)->entry == 50 && !gates.find(2),
          "warp gates from table 40");
}

void testScriptStorageAndLabels() {
    using S = sf::ScenarioScript;
    S script;
    script.messages[1000060] = "Remote Town";
    script.sentences = {{{S::kStorage, {raw(0)}},
                         {S::kMessageBox, {constant(10000200), constant(0), constant(-160), {S::kMessage, 1000060}, constant(224),
                                           constant(200), constant(100), raw(1000)}},
                         {S::kMessageBoxValue, {constant(10041000), constant(20), constant(-60), constant(29), constant(224),
                                                constant(224), constant(224)}}}};
    sf::ScriptFlags flags;
    TestHost host;
    sf::ScriptRunner(script, flags, &host).run(0);
    check(host.storages == 1, "STORAGE opens the warehouse");
    check(host.labels.size() == 2 && host.labels[0].text == "Remote Town" && host.labels[0].character == 10000200 &&
              host.labels[0].dy == -160 && host.labels[0].g == 200 && host.labels[0].duration == 1000,
          "MESSAGEBOX floats a message over a character");
    check(host.labels.size() == 2 && host.labels[1].text == "29" && host.labels[1].dx == 20, "MESSAGEBOXVALUE floats a number");
}

void testScriptRepairAndIdentify() {
    using S = sf::ScenarioScript;
    S script;
    const Op a{S::kTemp, 1000010}, b{S::kTemp, 1000011}, c{S::kTemp, 1000012}, d{S::kTemp, 1000013};
    script.sentences = {{{S::kCheckUnopinionItem, {a}},
                         {S::kIdentify, {}},
                         {S::kCheckUnopinionItem, {b}},
                         {S::kGetRepairPrice, {constant(3), c}},
                         {S::kGetRepairPrice, {constant(-1), d}},
                         {S::kRepair, {constant(-1)}}}};
    sf::ScriptFlags flags;
    TestHost host;
    sf::ScriptRunner(script, flags, &host).run(0);
    check(flags[{S::kTemp, 1000010}] == 1 && flags[{S::kTemp, 1000011}] == 0, "CHECKUNOPINIONITEM before and after IDENTIFY");
    check(flags[{S::kTemp, 1000012}] == 13 && flags[{S::kTemp, 1000013}] == 75, "GETREPAIRPRICE asks the game");
    check(host.repaired == std::vector{-1}, "REPAIR mends the part asked for");
}

void testScriptPartners() {
    using S = sf::ScenarioScript;
    S script;
    const Op kind{S::kTemp, 1000005}, hp{S::kTemp, 1000006}, max{S::kTemp, 1000007}, answer{S::kTemp, 1000001};
    script.sentences = {{{S::kGetPartnerKind, {kind}},
                         {S::kChangePartner, {constant(4)}},
                         {S::kGetPartnerLife, {hp, max}},
                         {S::kTalkPartnerStatus, {kind, answer}},
                         {S::kGetPartnerKind, {kind}},
                         {S::kTalkPartnerStatus, {kind, answer}}}};
    sf::ScriptFlags flags;
    flags[{S::kTemp, 1000006}] = 9;
    TestHost host;
    sf::ScriptRunner(script, flags, &host, 12010003).run(0);
    check(host.kind == 4 && flags[{S::kTemp, 1000005}] == 4, "CHANGEPARTNER swaps and GETPARTNERKIND reads the kind");
    check(flags[{S::kTemp, 1000006}] == 9 && !flags.contains({S::kTemp, 1000007}), "GETPARTNERLIFE leaves the variables with no companion");
    check(host.talks.size() == 1 && host.talks[0].text == "Harley\nLevel 1" && host.talks[0].speaker == 12010003 &&
              host.talks[0].answer.value == 1000001,
          "TALKPARTNERSTATUS shows the sheet over the speaker");

    S lifeScript;
    lifeScript.sentences = {{{S::kGetPartnerLife, {hp, max}}}};
    host.life = std::pair(-1, -1);
    sf::ScriptRunner(lifeScript, flags, &host).run(0);
    check(flags[{S::kTemp, 1000006}] == -1 && flags[{S::kTemp, 1000007}] == -1, "GETPARTNERLIFE is -1 / -1 while it is down");
}

// Enemies 14000000-14000002: the first is dead, the others alive.
struct QuestHost : sf::ScriptHost {
    std::optional<bool> enemyAlive(std::int32_t id) const override {
        if (id < 14000000 || id > 14000002) return std::nullopt;
        return id != 14000000;
    }
    std::vector<std::pair<int, int>> items{{4, 99000000}};
    bool hasItem(int cls, int code) const override { return std::ranges::find(items, std::pair(cls, code)) != items.end(); }
    void deleteItem(int cls, int code) override { std::erase(items, std::pair(cls, code)); }
    void playerLife(int& hp, int& max) const override { hp = 80, max = 150; }
    void playerMental(int& mp, int& max) const override { mp = 20, max = 30; }
    int percent = 0;
    void giveExperiencePercent(int p) override { percent += p; }
    void giveItem(int cls, int code) override { items.emplace_back(cls, code); }
};

void testScriptQuestChecks() {
    using S = sf::ScenarioScript;
    S script;
    const Op live{S::kTemp, 1}, dead{S::kTemp, 2}, none{S::kTemp, 3}, has{S::kTemp, 4}, gone{S::kTemp, 5}, hp{S::kTemp, 6},
        hpMax{S::kTemp, 7}, mp{S::kTemp, 8}, mpMax{S::kTemp, 9};
    script.sentences = {{{S::kCheckEnemyLive, {constant(14000000), constant(14000002), live}},
                         {S::kCheckEnemyDead, {constant(14000000), constant(14000002), dead}},
                         {S::kCheckEnemyLive, {constant(14000005), constant(14000007), none}},
                         {S::kCheckItemExist, {constant(4), constant(99000000), has}},
                         {S::kDeleteItem, {constant(4), constant(99000000)}},
                         {S::kCheckItemExist, {constant(4), constant(99000000), gone}},
                         {S::kGetPlayerLife, {hp, hpMax}},
                         {S::kGetPlayerMental, {mp, mpMax}},
                         {S::kGetExperience, {constant(30)}},
                         {S::kSetItemDirect, {constant(4), constant(99000010)}},
                         {S::kSetFlag, {{S::kTemp, 10}, {S::kEnemyAlive, 0}}},
                         {S::kSetFlag, {{S::kTemp, 11}, {S::kEnemyAlive, 1}}}}};
    sf::ScriptFlags flags;
    QuestHost host;
    sf::ScriptRunner(script, flags, &host).run(0);
    check(flags[{S::kTemp, 1}] == 14000001 && flags[{S::kTemp, 2}] == 14000000, "CHECKENEMYLIVE / DEAD give the first match");
    check(flags[{S::kTemp, 3}] == -1, "CHECKENEMYLIVE gives -1 when none is alive");
    check(flags[{S::kTemp, 4}] == 1 && flags[{S::kTemp, 5}] == 0, "CHECKITEMEXIST and DELETEITEM");
    check(host.items == std::vector<std::pair<int, int>>{{4, 99000010}}, "SETITEMDIRECT hands the item over");
    check(flags[{S::kTemp, 6}] == 80 && flags[{S::kTemp, 7}] == 150 && flags[{S::kTemp, 8}] == 20 && flags[{S::kTemp, 9}] == 30,
          "GETPLAYERLIFE and GETPLAYERMENTAL");
    check(host.percent == 30, "GETEXPERIENCE gives a share of the level's experience");
    check(flags[{S::kTemp, 10}] == 0 && flags[{S::kTemp, 11}] == 1, "ELIV reads whether an enemy is alive");
}

void testScriptActiveFlagsAndRoutines() {
    using S = sf::ScenarioScript;
    S script;
    // Sentence 0 calls routine 41000 (sentence 1), then hides a door;
    // sentence 1 counts in TEMP[1].
    script.triggers = {{0, S::kOnRoutine, 41000, 1}, {0, S::kOnRoutine, 42000, 2}};
    script.sentences = {{{S::kRoutine, {constant(41000)}}, {S::kRoutine, {constant(41000)}}, {S::kSetInactive, {constant(10011000)}}},
                        {{S::kAdd, {{S::kTemp, 1}, constant(1)}}},
                        {{S::kAdd, {{S::kTemp, 1}, constant(100)}}}};
    sf::ScriptFlags flags;
    for (int base : sf::kStateFlagBases) flags[{S::kNet, base + 10011000}] = 1;
    sf::ScriptRunner(script, flags).run(0);
    check(flags[{S::kTemp, 1}] == 2, "ROUTIN runs that routine's triggers only");
    check(flags[{S::kNet, 110011000}] == 0 && flags[{S::kNet, 210011000}] == 0 && flags[{S::kNet, 310011000}] == 0,
          "SETINACTIVE clears the three NET flags");
}

struct ObjectHost : sf::ScriptHost {
    std::int32_t fixedId = 0;
    int states[3] = {-1, -1, -1};
    void setObjectStates(std::int32_t id, int shown, int usable, int blocks) override {
        fixedId = id;
        states[0] = shown, states[1] = usable, states[2] = blocks;
    }
    bool workingObject(std::int32_t id) const override { return id == 10041000; }
    bool unlock = false;
    void setUnlockSwitch(bool on) override { unlock = on; }
};

void testScriptObjects() {
    using S = sf::ScenarioScript;
    S script;
    // A Brain Globe's frame (Scenario 00000004): CCHECK the globe, and
    // SETUNLOCKSW while it is held; then a gate fixed open.
    script.sentences = {{{S::kSetFlag, {{S::kTemp, 1}, {S::kObjectCheck, 10041000}}},
                         {S::kSetFlag, {{S::kTemp, 2}, {S::kObjectCheck, 10041001}}},
                         {S::kSetUnlockSwitch, {constant(1)}},
                         {S::kAbsoluteObjectFlag, {constant(10010001), constant(1), constant(0), constant(1)}}}};
    sf::ScriptFlags flags;
    ObjectHost host;
    sf::ScriptRunner(script, flags, &host).run(0);
    check(flags[{S::kTemp, 1}] == 1 && flags[{S::kTemp, 2}] == 0, "CCHECK reads whether the object is being worked");
    check(host.unlock, "SETUNLOCKSW");
    check(host.fixedId == 10010001 && host.states[0] == 1 && host.states[1] == 0 && host.states[2] == 1, "ABSOLUTEOBJECTFLAG");
    check(sf::ScriptRunner::implemented(S::kAbsoluteObjectFlag) && sf::ScriptRunner::implemented(S::kSetUnlockSwitch),
          "object commands are carried out");
}

void testPartners() {
    sf::GameTables tables;
    auto& kinds = tables.tables[60];
    kinds.rows = sf::kPartnerKinds;
    kinds.cols = 5;
    for (int k = 0; k < sf::kPartnerKinds; ++k) {
        kinds.values.insert(kinds.values.end(), {0, k == 4 ? 1 : 0, 1000, 500, 250});
        for (const char* s : {"Kerberos", "", "", "", ""}) kinds.strings.emplace_back(k == 0 ? s : "Dog");
    }
    check(!sf::PartnerTables::load(tables), "no stat tables, no companions");
    for (int k = 0; k < sf::kPartnerKinds; ++k) {
        auto& t = tables.tables[800 + k];
        t.rows = 19;
        t.cols = 3;
        t.values.assign(19 * 3, 0);
        auto set = [&](int row, std::initializer_list<int> v) {
            int c = 0;
            for (int x : v) t.values[static_cast<std::size_t>(row * 3 + c++)] = x;
        };
        set(1, {125, 0, 0});
        set(3, {400, 50, 60});
        set(5, {10, 2, 3});
        set(13, {2, 2, 6});
        set(18, {3, 5, 0});
    }
    const auto partners = sf::PartnerTables::load(tables);
    check(partners && partners->kind(0).name == "Kerberos" && partners->kind(4).folder == 1 && partners->kind(0).tint[1] == 500,
          "table 60 names the kinds, their folder and tint");
    if (!partners) return;
    const auto one = partners->stats(0, 1), three = partners->stats(0, 3);
    check(one.maxHp == 400 && one.attack == 10 && one.walkSpeed == 125 && one.element == 2, "level 1 is the first column");
    check(three.maxHp == 510 && three.attack == 15 && three.element == 6, "rows 0-12 add up; row 13 is the level's own");
    check(partners->need(0, 1) == 3 && partners->need(0, 2) == 5, "row 18 is the experience to the next level");

    check(sf::partnerLevelCap(1) == 2 && sf::partnerLevelCap(9) == 5 && sf::partnerLevelCap(200) == sf::kPartnerMaxLevel,
          "a companion can reach the hero's level / 3 + 2");
    sf::PartnerRecord r;
    check(r.kind == 0 && !r.active && r.level() == 1, "a new hero has Kerberos at level 1, INACTIVE");
    bool up = false;
    for (int i = 0; i < 3; ++i) up = r.countKill(*partners, 1);
    check(up && r.level() == 2 && r.experience[0] == 0, "three kills take it to level 2");
    for (int i = 0; i < 10; ++i) r.countKill(*partners, 1);
    check(r.level() == 2 && r.experience[0] == 0, "it stops at the cap and keeps no experience there");
    for (int i = 0; i < 4; ++i) r.countKill(*partners, 9);
    check(r.level() == 2 && r.experience[0] == 4, "with a higher hero it carries on");
    r.kind = 3;
    r.countKill(*partners, 9);
    check(r.level() == 1 && r.experience[3] == 1 && r.experience[0] == 4, "each kind keeps its own level and experience");
}

void testScriptTalkAndShop() {
    using S = sf::ScenarioScript;
    S script;
    script.messages[1000000] = "Pay %d gold?\n ~YES~ ~NO~\n";
    const Op temp{S::kTemp, 1000001};
    // Sentence 0: price 30, show the question to character 12000001.
    // Sentence 1: on YES pay and heal, then stock shop 0 from list 22.
    script.sentences = {
        {{S::kSetMessageDefine, {constant(30)}}, {S::kTalk, {{S::kMessage, 1000000}, temp, raw(0), constant(1), constant(-1), constant(-1)}}},
        {ifCmd(temp, 0, constant(0), 2)},
        {{S::kPayGold, {constant(30)}}, {S::kHealLife, {}}, {S::kInitTrade, {constant(0), constant(22)}},
         {S::kGetPlayerLevel, {temp}}, {S::kAdd, {temp, constant(3)}}, {S::kEnableClassChange, {constant(2)}}},
    };
    sf::ScriptFlags flags;
    TestHost host;
    sf::ScriptRunner(script, flags, &host, 12000001).run(0);
    check(host.talks.size() == 1, "TALK shows a message");
    check(!host.talks.empty() && host.talks[0].text == "Pay 30 gold?\n ~YES~ ~NO~\n", "SETMSGDEFINE fills %d");
    check(!host.talks.empty() && host.talks[0].speaker == 12000001 && host.talks[0].defaultChoice == 1, "speaker -1 is the trigger's character");
    sf::ScriptRunner(script, flags, &host).store(temp, 0);  // the player chose YES
    sf::ScriptRunner(script, flags, &host).run(1);
    check(host.purse == 470 && host.healed == 1, "PAYGOLD and HEALLIFE");
    check(host.trades.size() == 1 && host.trades[0] == std::pair(0, 22), "INITTRADE shop list");
    check(flags[{S::kTemp, 1000001}] == 10, "GETPLAYERLEVEL and ADD");
    check(host.chosenClass == 2, "ENABLECLASSCHANGE");
}

void testShopStock() {
    // Table 32 row 0 lists entries 0 and 1 of table 33: three of item
    // (3, 7), then two weapons of level 2-4 that are normal quality.
    sf::GameTables tables;
    auto& lists = tables.tables[32];
    lists.rows = 1;
    lists.cols = 3;
    lists.values = {0, 1, -1};
    auto& entries = tables.tables[33];
    entries.rows = 2;
    entries.cols = 10;
    entries.values = {3, -1, -1, 7, 3, 0, 0, 1, 1, 1,   //
                      0, 4, 2, -1, 2, -1, -1, 1, 0, 0};
    sf::ItemTable items;
    auto add = [&](sf::ItemDef::Class cls, int code, int level, int quality, int w, int h) {
        sf::ItemDef d;
        d.cls = cls;
        d.code = code;
        d.itemLevel = level;
        d.quality = quality;
        d.width = w;
        d.height = h;
        d.dropWeight = 10;
        d.poolMask = 1;
        d.price = 100;
        items.items.push_back(d);
    };
    add(sf::ItemDef::Class::Consumable, 7, 1, 0, 1, 1);
    add(sf::ItemDef::Class::Weapon, 1, 1, 0, 1, 3);  // too low a level
    add(sf::ItemDef::Class::Weapon, 2, 3, 1, 1, 3);  // magic: not allowed
    add(sf::ItemDef::Class::Weapon, 3, 3, 0, 1, 3);
    sf::Random random(7);
    const auto stock = sf::stockShop(tables, items, 0, random);
    check(stock.goods.size() == 5, "shop holds 3 + 2 items");
    int weapons = 0;
    for (const auto& g : stock.goods) weapons += g.item.def->cls == sf::ItemDef::Class::Weapon && g.item.def->code == 3;
    check(weapons == 2, "random picks honour level and quality");
    check(stock.goods.size() >= 4 && stock.goods[0].x == 0 && stock.goods[1].x == 1 && stock.goods[3].x == 3 && stock.goods[3].y == 0,
          "goods fill the top row from the left");
    check(sf::sellPrice(stock.goods[0].item, {}) == 25 && sf::buyPrice(stock.goods[0].item, {}) == 100, "sell for a quarter");
}

void testCareer() {
    using C = sf::HeroClass;
    using S = sf::HeroStats;
    sf::Career career;
    for (int level = 2; level <= 4; ++level) career.levelUp(level);
    check(career.current == C::Mercenary && career.history.size() == 3, "a mercenary up to level 4");
    // Bow kills before level 5 change nothing: everyone is a warrior at 5.
    for (int i = 0; i < 10; ++i) career.countKill(5, false);
    career.levelUp(5);
    check(career.current == C::Warrior && career.offered == C::Warrior && !career.canChange(), "a warrior at level 5");
    check(career.kills == 0, "kills count again from each level");

    // 3 of 10 kills with a bow offer the hunter; the class stays until taken.
    for (int i = 0; i < 7; ++i) career.countKill(0, false);
    for (int i = 0; i < 3; ++i) career.countKill(5, false);
    career.levelUp(6);
    check(career.current == C::Warrior && career.offered == C::Hunter && career.canChange(), "30% bow kills offer the hunter");
    check(career.change() && career.current == C::Hunter && !career.change(), "Change Career takes it");

    // Magic outranks the bow; staffs count as magic.
    for (int i = 0; i < 4; ++i) career.countKill(5, false);
    career.countKill(8, false);
    career.levelUp(7);
    check(career.offered == C::Wizard, "20% magic offers the wizard");
    // A level with no kills takes the offer back.
    career.levelUp(8);
    check(career.offered == C::Hunter && !career.canChange(), "the offer lasts one level");
    // Back to the sword: swords, axes and maces together.
    for (int i = 0; i < 2; ++i) career.countKill(0, false);
    for (int i = 0; i < 2; ++i) career.countKill(3, false);
    career.countKill(1, false);
    for (int i = 0; i < 5; ++i) career.countKill(std::nullopt, false);  // bare hands
    career.levelUp(9);
    check(career.offered == C::Warrior, "50% blade kills offer the warrior");

    // Stats keep the class of each level left: mercenary 1-4, warrior 5,
    // hunter 6-8.
    check(career.history == std::vector<C>{C::Mercenary, C::Mercenary, C::Mercenary, C::Mercenary, C::Warrior, C::Hunter,
                                           C::Hunter, C::Hunter},
          "the class at each level-up is kept");
    const auto stats = sf::heroStats(career.history);
    check(stats[S::kHp] == 310 + 50 + 25 * 3, "health from each level's class");
    check(stats[S::kAttackSpeed] == 101, "the hunter's second level adds attack speed");
    check(sf::heroStats(sf::classHistory(9, C::Warrior))[S::kHp] == sf::heroStats(9)[S::kHp], "classHistory");
    check(sf::Career::at(9, C::Hunter).current == C::Hunter && sf::Career::at(4).current == C::Mercenary, "Career::at");
}

void testMines() {
    auto t = sf::MagicTables::standard();
    // Table 23: 40 at level 1, ten more a level to 280 at 25, then steps
    // of 15, 20 and 30 to 2000 at 100.
    check(sf::mines::power(t, 1, 0) == 40 && sf::mines::power(t, 25, 0) == 280 && sf::mines::power(t, 26, 0) == 295 &&
              sf::mines::power(t, 70, 0) == 1100 && sf::mines::power(t, 100, 0) == 2000,
          "mine power follows table 23");
    check(sf::mines::power(t, 3, 15) == 75 && sf::mines::power(t, 1, -100) == 1, "Mine Effect adds to it, at least 1");
    check(sf::mines::power(t, 0, 0) == 40 && sf::mines::power(t, 150, 0) == 2000, "levels outside the table use its ends");
    check(sf::mines::most(0) == 10 && sf::mines::most(3) == 13, "ten mines, more with Mine Count");
    // No element levels behind the blast: power x 90-110% - defence.
    check(sf::mines::damage(t, 100, 3, 20, 0) == 70 && sf::mines::damage(t, 100, -1, 20, 2) == 90, "blast damage");
    check(sf::mines::damage(t, 40, 0, 500, 1) == 1, "a blast does at least 1");
    // From the player's Table.Tbd when it has table 23.
    sf::GameTables tables;
    tables.tables[23] = {2, 1, {11, 22}, {}};
    t = sf::MagicTables::fromTables(tables);
    check(sf::mines::power(t, 2, 0) == 22 && sf::mines::power(t, 3, 0) == 60, "table 23 is read from Table.Tbd");
}

void testMagic() {
    const auto t = sf::MagicTables::standard();
    using L = sf::ElementLevels;
    check(sf::oppositeElement(sf::kFire) == sf::kWater && sf::oppositeElement(sf::kMetal) == sf::kGel, "opposite elements");
    check(sf::discElementLevels(0, 0) == L{}, "a new hero's element levels are 0");
    // Tables 204-207 as the original's Table.Tbd has them.
    check(t.count(204, 1) == 2 && t.count(204, 6) == 3 && t.count(204, 29) == 7 && t.count(204, 30) == 8, "table 204");
    check(t.count(205, 10) == 3 && t.count(206, 20) == 5, "tables 205 and 206");
    check(t.count(207, 9) == 1 && t.count(207, 10) == 3 && t.count(207, 21) == 5, "table 207");
    check(sf::discElementLevels(0, 20000)[sf::kFire] == 20, "the disc's fire anchor");
    check(sf::elementLevels({}, L{15, -12, 3})[0] == 10 && sf::elementLevels({}, L{15, -12, 3})[1] == -10, "levels stay in -10..10");

    // 0x412a40: blows get table 7 at the level in the monster's opposite.
    check(sf::heroBlowDamage(t, 100, L{}, sf::kFire, 10, 1) == 90, "a plain blow");
    L water{};
    water[sf::kWater] = 10;
    check(sf::heroBlowDamage(t, 100, water, sf::kFire, 10, 1) == 110, "water strengthens blows on fire monsters");
    check(sf::heroBlowDamage(t, 100, water, -1, 10, 1) == 90, "open content monsters have no element");
    L fire{};
    fire[sf::kFire] = -10;
    check(sf::damageToHero(t, 50, sf::kFire, fire, 20, 0) == 29, "table 11 weakens defence against fire");
    check(sf::damageToHero(t, 5, sf::kFire, L{}, 100, 0) == 1, "at least 1 damage");
    check(sf::spellDamage(100, sf::kWater, sf::kFire, 5, 1, 2) == 115, "the opposite element does more");
    check(sf::spellDamage(100, sf::kFire, sf::kFire, 5, 1, 0) == 65, "the same element does less");
    check(sf::spellDamage(100, sf::kEarth, sf::kFire, 5, 1, 3) == 95, "other elements do the usual");

    // Element levels raise stats by percent; a pair at 10 adds more.
    sf::HeroStats stats = sf::heroStats(1);
    const int attack = stats[sf::HeroStats::kAttack], hp = stats[sf::HeroStats::kHp];
    L both{};
    both[sf::kFire] = both[sf::kWater] = 10;
    int chant = 0;
    sf::applyElementLevels(stats, chant, both);
    check(stats[sf::HeroStats::kAttack] == attack + attack / 10 && stats[sf::HeroStats::kHp] == hp + hp / 10, "element stat bonuses");

    // Spells are learned at level-ups by class points.
    using C = sf::HeroClass;
    auto book = sf::SpellBook::replay(t, sf::classHistory(5, C::Warrior));
    check(book.knows(sf::kTransport) && book.knows(sf::kPlasma) && !book.knows(sf::kFireBall), "a mercenary's spells");
    check(book.bar[0] == sf::kTransport && book.bar[1] == sf::kPlasma && book.bar[2] == -1, "learned spells fill the bar");
    check(sf::classPoints(t, sf::classHistory(5, C::Warrior))[0] == 5, "the new hero's level counts as a mercenary one");
    auto warrior = sf::SpellBook::replay(t, sf::classHistory(13, C::Warrior));
    check(warrior.knows(sf::kFireBall) && !warrior.knows(sf::kIceBolt), "a warrior learns Fire Ball at 80 points");
    const int need = t.spell(sf::kPlasma).usesToLevel[0];
    for (int k = 1; k < need; ++k) book.train(t, sf::kPlasma);
    check(book.castLevel(sf::kPlasma) == 1 && book.train(t, sf::kPlasma) && book.castLevel(sf::kPlasma) == 2, "training");
    {
        // The experience-rate cheat counts one use many times, through several levels.
        auto fast = sf::SpellBook::replay(t, sf::classHistory(5, C::Warrior));
        const int two = t.spell(sf::kPlasma).usesToLevel[0] + t.spell(sf::kPlasma).usesToLevel[1];
        check(fast.train(t, sf::kPlasma, two) && fast.castLevel(sf::kPlasma) == 3, "training many uses at once");
    }
    check(sf::spellMpCost(t, sf::kFireBall, 1) == 30 && sf::spellMpCost(t, sf::kFireBall, 1, 50) == 1, "MP cost");

    check(book.castLevel(sf::kPlasma, false, 3) == 5 && book.castLevel(sf::kPlasma, true, 40) == 30,
          "Magic Level modifiers raise the cast level, up to 30");

    // Moon (tables 200 and 0x4138b0): its drain, and what it adds to the
    // companion in percent, at the level it was cast at.
    check(t.moon[0][sf::MagicTables::column(1)] == -30 && t.moon[0][sf::MagicTables::column(11)] == -25, "Moon's drain");
    sf::PartnerStats dog;
    dog.attackSpeed = 250;
    dog.maxHp = 100;
    dog.attack = 50;
    dog.defense = 0;
    sf::applyMoon(dog, t, 6);
    check(dog.attackSpeed == 255 && dog.maxHp == 112 && dog.attack == 53 && dog.defense == 1, "Moon raises the companion");

    // Stuns (tables 70-78 and 24, 0x459690).
    const auto medusa = t.stun(-1, sf::kMedusa, 10);
    check(medusa.chance == 100 && medusa.length == 76 && medusa.uncapped, "Medusa's stone");
    check(t.stun(sf::kFire, sf::kIceBolt, 1).length == 20, "Ice Bolt's freeze");
    L water3{};
    water3[sf::kWater] = 3;
    const auto bonus = sf::spellStun(t, sf::kIceBolt, 1, sf::kFire, water3);
    check(bonus.chance == t.stun(sf::kFire, sf::kIceBolt, 1).chance + 10 && bonus.length == 26,
          "the hero's level in the monster's opposite element adds to a stun");
    check(sf::spellStun(t, sf::kIceBolt, 1, -1, water3).length == 20, "no bonus on a monster without an element");
    check(sf::stunKindOf(sf::kMedusa) == sf::StunKind::Stone && sf::stunKindOf(sf::kDreadDeathscythe) == sf::StunKind::Freeze &&
              sf::stunKindOf(sf::kFireBall) == sf::StunKind::Plain,
          "what each spell's stun does");

    // A monster's spell on the hero (0x443cb0): the column of the spell's
    // element, its opposite or none by the hero's level in it, table 26 for
    // a level below 0.
    const int fbElement = t.spell(sf::kFireBall).element;
    L none{}, fb2{}, fbMinus2{};
    fb2[static_cast<std::size_t>(fbElement)] = 2;
    fbMinus2[static_cast<std::size_t>(fbElement)] = -2;
    check(sf::heroSpellStun(t, sf::kFireBall, 10, none).chance == t.stun(-1, sf::kFireBall, 10).chance, "a hero at 0 takes the no-element stun");
    check(sf::heroSpellStun(t, sf::kFireBall, 10, fb2).length == t.stun(fbElement, sf::kFireBall, 10).length,
          "a hero above 0 takes the stun of the spell's own element");
    const auto weak = sf::heroSpellStun(t, sf::kFireBall, 10, fbMinus2);
    check(weak.chance == t.stun(fbElement ^ 1, sf::kFireBall, 10).chance + 10 && weak.length == t.stun(fbElement ^ 1, sf::kFireBall, 10).length + 1,
          "a hero below 0 takes the opposite element's stun and table 26");
    check(t.heroStunBonus[0][0] == 25 && t.heroStunBonus[0][1] == 15, "table 26 at -10");

    // How monsters cast (0x45b043).
    check(sf::monsterSpell(sf::kFireBall, 0)->release == 0 && sf::monsterSpell(sf::kFireBall, 0)->hitOption == 10000011,
          "a monster's Fire Ball leaves at once and bursts");
    check(sf::monsterSpell(sf::kHellFire, 0)->release == 10 && sf::monsterSpell(sf::kIceBlast, 0)->release == 10,
          "Hell Fire and Ice Blast leave 10 ticks in");
    check(sf::monsterSpell(sf::kEarthSpears, 3)->hitOption == 11000009, "Earth Spears' sparks by roll");
    check(sf::monsterSpell(sf::kDreadDeathscythe, 0)->targetOnly && !sf::monsterSpell(sf::kMedusa, 0)->targetOnly,
          "Dread Deathscythe goes for its target alone");
    check(!sf::monsterSpell(sf::kHeal, 0) && !sf::monsterSpell(sf::kSonicBlade, 0), "monsters have no Heal or Sonic Blade");
    check(t.pierces(sf::kFireBall, 15) && !t.pierces(sf::kFireBall, 14) && t.pierces(sf::kIceBolt, 11) && !t.pierces(sf::kPlasma, 30),
          "table 21: Fire Ball from 15 and Ice Bolt from 11 fly through");

    // MP drain in hundredths of a point, the fraction carried over.
    int carry = 0, total = 0;
    for (int k = 0; k < 10; ++k) total += sf::regenerate(100, -30, carry);
    check(total == -3, "Berserker's drain adds up");

    check(sf::Rules::ours().shieldsCostTheirOwnMp && !sf::Rules::ours().originalMode(), "our fixes are on by default");
    check(!sf::Rules::original().shieldsCostTheirOwnMp && sf::Rules::original().originalMode(), "original mode turns them off");
    check(sf::Rules::ours().explosionUsesItsOwnLevel && !sf::Rules::original().explosionUsesItsOwnLevel,
          "Explosion's level is a switchable fix");
    check(sf::Rules::ours().healthBarsOverMonsters && !sf::Rules::original().healthBarsOverMonsters,
          "health bars over monsters are ours alone");
    check(sf::Rules::ours().zoomOut && !sf::Rules::original().zoomOut, "original mode keeps the world at full size");
    check(sf::Rules::ours().outlineUnderPointer && !sf::Rules::original().outlineUnderPointer,
          "original mode draws no outline around what is under the pointer");
    check(sf::Rules::ours().rightClickSells && !sf::Rules::original().rightClickSells,
          "selling from the bag with a right click is ours alone");
    check(sf::Rules::ours().rightClickEquips && !sf::Rules::original().rightClickEquips,
          "wearing and taking off with a right click is ours alone");
    check(sf::Rules::ours().groundItemNames && !sf::Rules::original().groundItemNames,
          "the names of items on the ground are ours alone");
    check(sf::Rules::ours().noEmptyGoldInWarehouse && !sf::Rules::original().noEmptyGoldInWarehouse,
          "original mode leaves empty gold in the warehouse");
    check(sf::Rules::ours().absorptionHeals && !sf::Rules::original().absorptionHeals,
          "absorption heals the hero only outside original mode");
}

void testPowerMode() {
    sf::PowerMode p;
    check(!p.ready(false) && !p.start(false), "power mode needs a charge");
    p.charge = 30;
    check(!p.ready(false) && p.ready(true), "the Spirit Stone lowers the charge to 30");
    p.charge = 50;
    check(p.start(false) && p.charge == 0 && p.active() && p.left == 900, "starting empties the charge for 900 ticks");
    p.tick(899.5);
    check(p.active(), "still on before 900 ticks");
    p.tick(1);
    check(!p.active() && p.left == 0, "off after 900 ticks");
    check(sf::blockedInPowerMode(sf::kBerserker) && sf::blockedInPowerMode(sf::kMoon) &&
              sf::blockedInPowerMode(sf::kEnergyShield) && !sf::blockedInPowerMode(sf::kFireBall),
          "Berserker, Moon and Energy Shield can't be cast in power mode");

    const auto t = sf::MagicTables::standard();
    check(sf::walkSpeedFactor(128) == 1.0 && sf::walkSpeedFactor(0) < 1.0, "walking speed 128 is the normal step");
    check(std::abs(sf::walkSpeedFactor(0, true) - 4.0 / 3.0) < 1e-9, "power mode walks at the fastest step");
    check(sf::attackSpeedFactor(t, 0, true) == 2.0 && sf::attackSpeedFactor(t, 0) < 2.0, "power mode attacks at the fastest step");
    check(sf::attackSpeedFactor(t, 255) >= sf::attackSpeedFactor(t, 0), "faster attack speed, faster swings");
}

void testLighting() {
    const auto dark = sf::lighting::levels(1000);
    check(dark[0] == 0 && dark[128] == 128 && dark[255] == 255, "light 1000 keeps the light as it is");
    const auto darker = sf::lighting::levels(500);
    check(darker[200] == 100 && darker[255] == 128, "below 1000 the light is scaled down");
    const auto lit = sf::lighting::levels(1500);
    check(lit[0] == 127 && lit[255] == 255, "above 1000 it is lifted towards white");

    sf::LightMap screen{4, 3, std::vector<std::uint8_t>(12, 200)};
    const sf::LightMap lamp{2, 2, {100, 0, 30, 60}};
    sf::lighting::add(screen, lamp, 0, 0);  // its middle at (0, 0): only its bottom right lands
    check(screen.at(0, 0) == 255 && screen.at(1, 0) == 200 && screen.at(0, 1) == 200, "lights add, saturating, clipped to the screen");
    const auto e = sf::lighting::ellipse(160, 110);
    check(e.at(80, 55) >= 250 && e.at(0, 0) == 0 && e.at(80, 0) < 10, "our light is brightest in the middle");
}

void testExploredMap() {
    sf::ExploredMap m(10, 5);
    m.reveal(-2, 3, 5, 9);
    check(m.seenAt(0, 3) && m.seenAt(2, 4) && !m.seenAt(3, 4) && !m.seenAt(0, 2), "reveal marks the block on the map");
    check(!m.seenAt(-1, 3) && !m.seenAt(0, 5), "off the map is never seen");
    check(m.encode() == "10,5,30,3,7,3,7", "the mask is written as runs");
    const auto back = sf::ExploredMap::decode(m.encode());
    check(back.width == 10 && back.height == 5 && back.seen == m.seen, "the mask survives a write and a read");
    check(sf::ExploredMap::decode(sf::ExploredMap(3, 2).encode()).seen == std::vector<std::uint8_t>(6, 0), "an unseen map");
    bool threw = false;
    try {
        sf::ExploredMap::decode("2,2,5");
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "runs longer than the map are rejected");
}

void testSettings() {
    const sf::Settings defaults;
    check(!defaults.fullScreen && defaults.seeThroughPlayer && defaults.clickSize == 2 && !defaults.originalMode,
          "settings default to a window, see-through player, NORM click range and our rules");
    check(!defaults.ourArt, "the original game is drawn with its own art by default");
    check(defaults.zoom == 100, "the world is drawn at full size by default");
    check(defaults.clickPriority[0] == sf::Settings::kEnemy, "enemies come first by default");

    sf::Settings s;
    s.fullScreen = true;
    s.seeThroughShadows = false;
    s.clickRange = false;
    s.clickSize = 4;
    s.musicVolume = 3;
    s.originalMode = true;
    s.ourArt = true;
    s.zoom = 70;
    s.putFirst(sf::Settings::kPerson);
    check(s.clickPriority[0] == sf::Settings::kPerson && s.rank(sf::Settings::kEnemy) == 1, "putFirst moves a target to the front");
    const auto back = sf::Settings::parse(s.write());
    check(back.fullScreen && !back.seeThroughShadows && !back.clickRange && back.clickSize == 4 && back.musicVolume == 3 &&
              back.originalMode && back.ourArt && back.zoom == 70 && back.clickPriority == s.clickPriority,
          "settings survive a write and a parse");
    check(back.clickReach() < defaults.clickReach(), "click range off reaches less than NORM");

    const auto bad = sf::Settings::parse("click_size=9\nclick_priority=enemy,enemy,item,object,person\nnonsense\nmusic_volume=x\nzoom=10\n");
    check(bad.clickSize == 4 && bad.clickPriority == defaults.clickPriority && bad.musicVolume == sf::Settings::kMaxVolume &&
              bad.zoom == sf::Settings::kMinZoom,
          "bad settings are clamped or ignored");

    check(defaults.xpRate == 100, "experience comes at the normal rate by default");
    check(!defaults.showFps, "the frame rate is hidden by default");
    check(!defaults.itemNames, "item names show only while Alt is held by default");
    sf::Settings names;
    names.itemNames = true;
    check(sf::Settings::parse(names.write()).itemNames, "item names left on survive a write and a parse");
    sf::Settings fps;
    fps.showFps = true;
    check(sf::Settings::parse(fps.write()).showFps, "show FPS survives a write and a parse");
    sf::Settings fast;
    fast.xpRate = 1000;
    check(fast.write().find("xp_rate=10\n") != std::string::npos && sf::Settings::parse(fast.write()).xpRate == 1000,
          "the experience rate is saved as a multiplier and read back");
    check(sf::Settings::parse("xp_rate=9\n").xpRate == 500 && sf::Settings::parse("xp_rate=0.5\n").xpRate == 100 &&
              sf::Settings::parse("xp_rate=2.5\n").xpRate == 200 && sf::Settings::parse("xp_rate=50\n").xpRate == 2000 &&
              sf::Settings::parse("xp_rate=20\n").xpRate == 2000 && sf::Settings::parse("xp_rate=fast\n").xpRate == 100,
          "experience rates are kept to 1x, 2x, 5x, 10x and 20x");
}

void testTrapGeometry() {
    check(sf::calcAngleDegrees(10, 0) == 0 && sf::calcAngleDegrees(0, -10) == 90 && sf::calcAngleDegrees(0, 10) == -90,
          "CALCANGLE turns world y up");
    check(sf::calcAngleDegrees(-10, 1) == -174, "CALCANGLE cuts towards zero");
    constexpr double deg = 0.01745328888888889;
    check(sf::directionOfAngle(0) == 1 && sf::directionOfAngle(45 * deg) == 2 && sf::directionOfAngle(90 * deg) == 3 &&
              sf::directionOfAngle(180 * deg) == 5 && sf::directionOfAngle(270 * deg) == 7 && sf::directionOfAngle(-90 * deg) == 7,
          "angles face the exe's eight directions");
    check(sf::directionOfAngle(300 * deg) == 0 && sf::directionOfAngle(350 * deg) == 1, "0 lies between 7 and 1");
    const sf::JudgeRect r = sf::kHeroJudge;
    check(sf::rectGap(0, 0, r, 100, 100, r) == 0, "overlapping rectangles have no gap");
    check(sf::rectGap(0, 0, r, 200, 0, r) == 40, "side by side: the nearest edges less one");
    check(sf::rectGap(0, 0, r, 300, 300, r) == static_cast<int>(std::hypot(141.0, 141.0)) - 1, "apart both ways: the corners");
}

void testArchery() {
    using sf::HeroClass;
    check(sf::bowAttackPercent(HeroClass::Hunter, {}) == 100, "a hunter shoots with the whole attack");
    check(sf::bowAttackPercent(HeroClass::Warrior, {HeroClass::Mercenary, HeroClass::Warrior}) == 40, "no hunter levels: 40%");
    std::vector<HeroClass> once(4, HeroClass::Mercenary);
    once.push_back(HeroClass::Hunter);
    once.push_back(HeroClass::Hunter);
    once.push_back(HeroClass::Warrior);
    check(sf::hunterLevels(once) == 2 && sf::bowAttackPercent(HeroClass::Warrior, once) == 43, "two hunter levels: 40 + 100/30");
    check(sf::bowAttackPercent(HeroClass::Wizard, std::vector<HeroClass>(40, HeroClass::Hunter)) == 90, "at most 90%");

    constexpr double deg = 3.141592653589793 / 180;
    const auto one = sf::bowVolley(1, 0, 90 * deg);
    check(one.option == 0 && one.arrows.size() == 1 && one.arrows[0].facing == 3 && !one.flame && !one.frost,
          "a plain bowgun looses one arrow facing its way");
    const auto stones = sf::bowVolley(0, 3, 0);
    check(stones.option == 1 && stones.arrows.size() == 3 && stones.arrows[1].facing == 8 &&
              std::abs(stones.arrows[2].angle - 15 * deg) < 1e-9,
          "a triple sling throws three stones 15 degrees apart");
    const auto fire = sf::bowVolley(2, 7, 0);
    check(fire.flame && !fire.frost && fire.arrows.size() == 7 && std::abs(fire.arrows[6].angle - 0.41887902047863906) < 1e-12,
          "seven fire arrows 8 degrees apart, 24 at the edge");
    check(sf::bowVolley(3, 5, 0).frost && sf::bowVolley(3, 5, 0).arrows.size() == 5, "ice bowguns light the frost layer");
    // Odd spreads keep the centre's facing; the second of two faces itself.
    const auto fan = sf::bowVolley(1, 3, 22 * deg);
    check(fan.arrows[2].facing == 1 && sf::directionOfAngle(fan.arrows[2].angle) == 2, "a fan keeps the centre's direction");
    const auto two = sf::bowVolley(1, 1, 20 * deg);
    check(two.arrows.size() == 2 && two.arrows[0].facing == 1 && two.arrows[1].facing == 2, "two arrows: the second faces itself");

    check(sf::bowHitOption(0, 3) == 11000009 && sf::bowHitOption(1, 2) == 11000022, "the sparks of stones and arrows");
    const auto start = sf::fallingArrow(0), mid = sf::fallingArrow(5), land = sf::fallingArrow(10);
    check(start.dx == 1001 && start.dy == -999 && start.height == 4000, "the volley's arrow starts high up and to the right");
    check(mid.dx == 501 && mid.dy == -499 && mid.height == 2000 && land.dx == 1 && land.dy == 1 && land.height == 0,
          "it falls onto its target over ten ticks");
}

void testTrapShots() {
    const auto arrow = sf::trapShot(0, -1, 3.141592);
    check(arrow && arrow->direction == 5 && arrow->diesOnHit && arrow->diesOnWall && !arrow->hitsAll && arrow->box.left == -30,
          "an arrow faces where it flies and stops at the first hit");
    const auto jet = sf::trapShot(2, 3, 0);
    check(jet && jet->hitsAll && jet->lastsItsAnimation && jet->hitFrom == 5 && jet->hitTo == 30 && jet->box.top == -360,
          "a fire jet burns its whole length from tick 5 to 30");
    check(sf::trapShot(1, 4, 0)->direction == 8 && sf::trapShot(1, 4, 0)->option == 1 && !sf::trapShot(8, 0, 0),
          "kind 1 (the stone) has no facing; kind 8 is not fired");
    const auto fireArrow = sf::trapShot(4, 8, 0), big = sf::trapShot(6, 8, 0);
    check(fireArrow && fireArrow->option == 0 && fireArrow->direction == 1 && fireArrow->box.right == 30 && sf::trapShot(7, 8, 0)->option == 0,
          "kinds 4, 5 and 7 fly as kind 0's arrow");
    check(big && big->option == 4 && big->box.left == -160 && big->box.right == 159 && big->hitSound == 20 && big->diesOnWall,
          "kind 6 is OPTION 4 with a 320-unit box");
    check(sf::shotHiddenLayers(0) == 12 && sf::shotHiddenLayers(7) == 12 && sf::shotHiddenLayers(4) == 8 &&
              sf::shotHiddenLayers(5) == 4 && sf::shotHiddenLayers(1) == 0,
          "a plain arrow hides flame and frost, a fire arrow the frost, an ice arrow the flame");
    check(sf::monsterShotSpark(1, 3) == 21003 && sf::monsterShotSpark(4, 1) == 21008 && sf::monsterShotSpark(0, 3) == 21007,
          "a monster's stone sparks as a stone, its arrows as arrows");
    check(sf::monsterShotEffect(4) == 20000 && sf::monsterShotEffect(5) == 21013 && sf::monsterShotEffect(1) == -1,
          "fire and ice arrows play their effect on the one hit");
    check(sf::scriptEffectOption(21003) == 11000009 && sf::scriptEffectOption(22000) == 12000000 && sf::scriptEffectOption(1) == 0,
          "effect numbers name OPTION folders");
    check(sf::trapHitOption(0, 1) == 11000021 && sf::trapHitOption(2, 3) == 11000009, "the sparks of a trap's hit");

    check(sf::stunTicks(30, 20, false, 10, 3, 19) == 15 && !sf::stunTicks(30, 20, false, 10, 3, 20), "a stun's chance less resistance, capped at 15");
    check(sf::stunTicks(30, 20, true, 0, 0, 0) == 20 && sf::stunTicks(100, 2, false, 0, 9, 0) == 1, "a steady stun runs long; at least 1 tick");
    check(sf::stunPush(0, 5) == 120 && sf::stunPush(4, 5) == 24 && sf::stunPush(5, 5) == 0, "a stun's push shrinks each tick");

    // A 4 x 3 judge grid of 160-unit cells from cell (0, 0): (1, 1) is a
    // wall, (2, 1) a low wall shots fly over.
    sf::GroundLayer g;
    g.judgeWidth = 4;
    g.judgeHeight = 3;
    g.judge.assign(12, 0);
    g.judge[5] = 1;
    g.judge[6] = 3;
    const sf::JudgeRect box{-30, -30, 30, 30};
    check(!sf::shotMeetsWall(g, 80, 80, box) && sf::shotMeetsWall(g, 140, 200, box), "a shot meets a wall under any part of its box");
    check(!sf::shotMeetsWall(g, 400, 240, box), "shots fly over low walls");
    check(!sf::shotMeetsWall(g, -500, -500, box), "off the grid is not a wall");
}

struct TrapHost : sf::ScriptHost {
    std::vector<TrapFire> fired;
    std::vector<std::vector<int>> effects;
    std::vector<int> learned, pictures;
    std::map<int, int> opacity{{10010000, 1000}};
    void fireTrap(const TrapFire& f) override { fired.push_back(f); }
    void createEffect(int n, int x, int y, int h, int d) override { effects.push_back({n, x, y, h, d}); }
    void createEffectOn(int n, std::int32_t c) override { effects.push_back({n, c}); }
    bool nearestPlayer(std::int32_t c, int, int, int& p, int& x, int& y) const override {
        if (c != 12000000) return false;
        p = 0, x = 500, y = 600;
        return true;
    }
    int playerDistance(std::int32_t c) const override { return c == 12000000 ? 220 : -1; }
    void setObjectOpacity(std::int32_t o, int v) override { opacity[o] = v; }
    std::optional<int> objectOpacity(std::int32_t o) const override {
        const auto found = opacity.find(o);
        return found == opacity.end() ? std::nullopt : std::optional(found->second);
    }
    void startVisual(int n) override { pictures.push_back(n); }
    std::vector<std::vector<int>> sounds;
    void playSound(int voice, bool everywhere, int x, int y) override { sounds.push_back({voice, everywhere ? 1 : 0, x, y}); }
    void memorizeSpell(int n) override { learned.push_back(n); }
    bool knowsSpell(int n) const override { return std::ranges::find(learned, n) != learned.end(); }
};

void testScriptTrapsAndEffects() {
    using S = sf::ScenarioScript;
    S script;
    const Op t1{S::kTemp, 1}, t2{S::kTemp, 2}, t3{S::kTemp, 3}, t4{S::kTemp, 4}, t5{S::kTemp, 5}, t6{S::kTemp, 6},
        t7{S::kTemp, 7}, t8{S::kTemp, 8}, t9{S::kTemp, 9}, t10{S::kTemp, 10};
    script.sentences = {{{S::kCreateOptionAngle, {constant(1000), constant(2000), constant(0), constant(90), constant(40),
                                                  constant(25), constant(50), constant(100), constant(-1), constant(1),
                                                  constant(40), constant(6), constant(0), constant(1)}},
                         {S::kCreateEffect, {constant(21003), constant(5), constant(6), constant(7), constant(-1)}},
                         {S::kCreateEffectChar, {constant(21000), constant(0)}},
                         {S::kGetNearPlayerPos, {constant(12000000), constant(-1), constant(-1), t1, t2, t3}},
                         {S::kGetNearPlayerPos, {constant(12000001), constant(-1), constant(-1), t4, t5, t6}},
                         {S::kCalcMyPlayerDist, {constant(12000000), t7}},
                         {S::kCalcAngle, {constant(0), constant(-5), t8}},
                         {S::kSetCharTrans, {constant(10010000), constant(400)}},
                         {S::kGetCharTrans, {constant(10010000), t9}},
                         {S::kStartVisual, {constant(3)}},
                         {S::kCheckMemorizedMagic, {constant(17), t10}},
                         {S::kMemorizeMagic, {constant(17)}},
                         {S::kCheckMemorizedMagic, {constant(17), {S::kTemp, 11}}},
                         {S::kPlaySound, {constant(7), constant(0), constant(300), constant(400)}},
                         {S::kPlaySound, {constant(20), constant(5), constant(-1), constant(9)}}}};
    sf::ScriptFlags flags;
    flags[{S::kTemp, 5}] = 77;
    TrapHost host;
    sf::ScriptRunner(script, flags, &host).run(0);
    // The exe's degree is a shade short, so 100 out at 90 degrees cuts to 99.
    check(host.fired.size() == 1 && host.fired[0].x == 1000 && host.fired[0].y == 1901 && host.fired[0].fixedDamage &&
              host.fired[0].power == 25 && host.fired[0].direction == -1 && host.fired[0].stun.chance == 40 &&
              host.fired[0].stun.length == 6 && !host.fired[0].stun.steady && host.fired[0].stun.splash,
          "CREATEOPTIONANGLE starts its distance out along the angle");
    check(host.effects.size() == 2 && host.effects[0] == std::vector{21003, 5, 6, 7, 8} && host.effects[1] == std::vector{21000, 0},
          "CREATEEFFECT (no direction is 8) and CREATEEFFECTCHAR");
    check(flags[{S::kTemp, 1}] == 0 && flags[{S::kTemp, 2}] == 500 && flags[{S::kTemp, 3}] == 600, "GETNEARPLAYERPOS finds the player");
    check(flags[{S::kTemp, 4}] == -1 && flags[{S::kTemp, 5}] == 77, "GETNEARPLAYERPOS gives -1 and leaves x and y with none");
    check(flags[{S::kTemp, 7}] == 220 && flags[{S::kTemp, 8}] == 90, "CALCMYPLAYERDIST and CALCANGLE");
    check(flags[{S::kTemp, 9}] == 400, "SETCHARTRANS and GETCHARTRANS");
    check(host.pictures == std::vector{3}, "STARTVISUAL shows a story picture");
    check(flags[{S::kTemp, 10}] == 0 && flags[{S::kTemp, 11}] == 1, "MEMORIZEMAGIC teaches a spell CHECKMEMORIZEDMAGIC sees");
    check(host.sounds == std::vector<std::vector<int>>{{7, 0, 300, 400}, {20, 1, -1, -1}},
          "PLAYSOUND: voice, heard everywhere when not 0, at x y or at the player for x -1");
    check(sf::trapShot(0, 8, 0)->hitSound == 7 && sf::trapShot(2, 1, 0)->hitSound == -1, "arrows sound when they hit, jets do not");
}

// A voice of 8-bit mono samples at the mixer's own rate.
sf::VoiceBank oneVoice(std::vector<std::uint8_t> pcm, int copies = 0) {
    sf::VoiceBank bank;
    bank.copies = copies;
    sf::VoiceBank::Voice v;
    v.sampleRate = sf::Mixer::kRate;
    v.pcm = std::move(pcm);
    bank.voices.push_back(std::move(v));
    return bank;
}

void testItemLabels() {
    // Three names over a pile, nearest the front first, and one apart.
    std::vector<sf::LabelBox> boxes{{100, 200, 60, 18}, {110, 195, 40, 18}, {90, 190, 80, 18}, {400, 200, 50, 18}};
    sf::stackLabels(boxes);
    check(boxes[0].y == 200, "the front item's name keeps its place");
    check(boxes[1].y == 200 - 18 - 1, "a covered name rises just above the one in front");
    check(boxes[2].y == boxes[1].y - 18 - 1, "and the next above that");
    check(boxes[3].y == 200, "a name clear of the others stays put");
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        for (std::size_t j = i + 1; j < boxes.size(); ++j) check(!boxes[i].overlaps(boxes[j]), "stacked names do not overlap");
    }
}

void testMixer() {
    check(sf::Mixer::attenuation(0) == sf::Mixer::kSilent && sf::Mixer::attenuation(10) == 0 && sf::Mixer::attenuation(5) == -1500,
          "volume sliders: 0 silent, then -3000 + 300 a step");
    check(sf::Mixer::gain(sf::Mixer::kSilent) == 0 && sf::Mixer::gain(0) == 1 && std::abs(sf::Mixer::gain(-2000) - 0.1) < 1e-9,
          "attenuation is hundredths of a decibel");

    sf::Mixer m;
    m.load(0, oneVoice({255, 255, 128}));
    std::vector<std::int16_t> out(8);
    check(!m.play(0, 1, false, 0) && !m.play(3, 0, false, 0), "voices that are not loaded do not play");
    check(m.play(0, 0, false, 0) && m.playing(0, 0), "a voice plays");
    m.mix(out);
    check(out[0] > 32000 && out[1] == out[0] && out[4] == 0 && out[6] == 0 && !m.playing(0, 0),
          "mono goes to both sides, and a voice stops at its end");

    m.play(0, 0, true, 0);
    m.mix(out);
    check(out[6] > 32000 && m.playing(0, 0), "a looping voice starts again");
    m.stop(0, 0);
    check(!m.playing(0, 0), "stop");

    // Two at once clip; with no spare copy the second play restarts it.
    m.load(1, oneVoice({192, 192, 192, 192}, 1));
    m.play(1, 0, false, 0);
    m.play(1, 0, false, 0);
    m.mix(out);
    check(out[0] > 32000, "a voice's copies play together");
    m.load(2, oneVoice({192, 192, 192, 192}));
    m.release(1);
    m.play(2, 0, false, 0);
    m.play(2, 0, false, 0);
    m.mix(out);
    check(out[0] > 16000 && out[0] < 17000, "with every copy busy, the oldest starts again");
    m.play(2, 0, false, -2000);
    m.mix(out);
    check(out[0] > 1600 && out[0] < 1700, "quieter by its attenuation");
}

int main() {
    testItemLabels();
    testMixer();
    testLighting();
    testExploredMap();
    testSettings();
    testMagic();
    testPowerMode();
    testMines();
    testScriptTalkAndShop();
    testShopStock();
    testScriptStorageAndLabels();
    testScriptRepairAndIdentify();
    testScriptPartners();
    testScriptWarpGates();
    testWarpGateTable();
    testScriptQuestChecks();
    testScriptActiveFlagsAndRoutines();
    testScriptObjects();
    testPartners();
    testCareer();
    testScriptExit();
    testScriptFlagsAndOperators();
    testScriptStopsAtChange();
    testScriptQuestFlags();
    testScriptMissionNames();
    testAiDistanceCondition();
    testAiPriorityAndWeights();
    testAiHealthCondition();
    testAiSameSeedSameChoices();
    testMovement();
    testLevels();
    testTrapGeometry();
    testTrapShots();
    testArchery();
    testScriptTrapsAndEffects();
    if (failures == 0) std::puts("all logic tests passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

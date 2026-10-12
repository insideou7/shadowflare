// Unit tests for the game rules that need neither SDL nor game data.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "logic/ai_select.h"
#include "logic/explored_map.h"
#include "logic/lighting.h"
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
#include "formats/item_data.h"

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
};

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
    check(sf::spellMpCost(t, sf::kFireBall, 1) == 30 && sf::spellMpCost(t, sf::kFireBall, 1, 50) == 1, "MP cost");

    // MP drain in hundredths of a point, the fraction carried over.
    int carry = 0, total = 0;
    for (int k = 0; k < 10; ++k) total += sf::regenerate(100, -30, carry);
    check(total == -3, "Berserker's drain adds up");

    check(sf::Rules::ours().shieldsCostTheirOwnMp && !sf::Rules::ours().originalMode(), "our fixes are on by default");
    check(!sf::Rules::original().shieldsCostTheirOwnMp && sf::Rules::original().originalMode(), "original mode turns them off");
    check(sf::Rules::ours().explosionUsesItsOwnLevel && !sf::Rules::original().explosionUsesItsOwnLevel,
          "Explosion's level is a switchable fix");
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
    check(defaults.clickPriority[0] == sf::Settings::kEnemy, "enemies come first by default");

    sf::Settings s;
    s.fullScreen = true;
    s.seeThroughShadows = false;
    s.clickRange = false;
    s.clickSize = 4;
    s.musicVolume = 3;
    s.originalMode = true;
    s.putFirst(sf::Settings::kPerson);
    check(s.clickPriority[0] == sf::Settings::kPerson && s.rank(sf::Settings::kEnemy) == 1, "putFirst moves a target to the front");
    const auto back = sf::Settings::parse(s.write());
    check(back.fullScreen && !back.seeThroughShadows && !back.clickRange && back.clickSize == 4 && back.musicVolume == 3 &&
              back.originalMode && back.clickPriority == s.clickPriority,
          "settings survive a write and a parse");
    check(back.clickReach() < defaults.clickReach(), "click range off reaches less than NORM");

    const auto bad = sf::Settings::parse("click_size=9\nclick_priority=enemy,enemy,item,object,person\nnonsense\nmusic_volume=x\n");
    check(bad.clickSize == 4 && bad.clickPriority == defaults.clickPriority && bad.musicVolume == sf::Settings::kMaxVolume,
          "bad settings are clamped or ignored");
}

int main() {
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
    if (failures == 0) std::puts("all logic tests passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

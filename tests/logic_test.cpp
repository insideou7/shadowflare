// Unit tests for the game rules that need neither SDL nor game data.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "logic/ai_select.h"
#include "formats/table_data.h"
#include "logic/movement.h"
#include "logic/progression.h"
#include "logic/script_runner.h"

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
    check(sf::classAtLevel(5) == sf::HeroClass::Novice && sf::classAtLevel(6) == sf::HeroClass::Warrior,
          "a warrior from level 6");
    const auto five = sf::heroStats(5, sf::HeroClass::Novice);
    check(five[S::kHp] == 310 && five[S::kAttack] == 26 && five[S::kMagicAttack] == 18, "four classless levels");
    check(sf::heroStats(6)[S::kHp] == 360 && sf::heroStats(26)[S::kHp] == 1350, "a warrior gains 50 health for 20 levels");
    check(sf::heroStats(35)[S::kHp] == 1710 && sf::heroStats(36)[S::kHp] == 1740, "then 40, then 30");
    check(sf::heroStats(16)[S::kAttack] == 26 + 90 + 7, "a warrior's attack: 9 for 10 levels, then 7");
    check(sf::heroStats(10, sf::HeroClass::Magician)[S::kMagicAttack] == 18 + 60, "a magician's magic");
    check(sf::heroStats(9, sf::HeroClass::Hunter)[S::kAttackSpeed] == 102, "a hunter's speed every second level");

    check(sf::hitChance(140, 0) == 98 && sf::hitChance(50, 60) == 20 && sf::hitChance(70, 20) == 50,
          "hit chance is hit minus evasion, from 20 to 98");
    check(sf::meleeDamage(30, 10, 0) == 17 && sf::meleeDamage(30, 10, 1) == 20 && sf::meleeDamage(30, 10, 2) == 23,
          "damage is 90-110% of attack minus defence");
    check(sf::meleeDamage(10, 30, 1) == 1, "at least 1 damage");
    check(sf::defaultExperience(30, 4) == 14, "a goblin's default experience");
}

}  // namespace

int main() {
    testScriptExit();
    testScriptFlagsAndOperators();
    testScriptStopsAtChange();
    testAiDistanceCondition();
    testAiPriorityAndWeights();
    testAiHealthCondition();
    testAiSameSeedSameChoices();
    testMovement();
    testLevels();
    if (failures == 0) std::puts("all logic tests passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

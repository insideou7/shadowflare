// Small self-contained checks for the format readers. Uses synthetic data so it
// runs without the original game files.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

#include "formats/ai_control.h"
#include "formats/animation.h"
#include "formats/binary_reader.h"
#include "formats/item_data.h"
#include "formats/map_layers.h"
#include "formats/rclib.h"
#include "formats/scenario.h"
#include "formats/scenario_script.h"
#include "formats/sprite_bank.h"
#include "formats/table_data.h"
#include "formats/voice_bank.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void testLzLiterals() {
    // Flag byte 0x00: eight literals.
    const std::vector<std::uint8_t> packed = {0x00, 'S', 'h', 'a', 'd', 'o', 'w', 'F', 'l'};
    auto out = sf::rclib::lzDecode(packed, 8);
    check(std::string(out.begin(), out.end()) == "ShadowFl", "literal run");
}

void testLzBackReference() {
    // "ab" as literals, then a reference to ring position 0xFEE (where
    // output starts) of length 3 + 3 = 6: "ababab".
    const std::vector<std::uint8_t> packed = {0x20, 'a', 'b', 0xee, 0xf3};
    auto out = sf::rclib::lzDecode(packed, 8);
    check(std::string(out.begin(), out.end()) == "abababab", "overlapping back reference");
}

void testLzZeroFill() {
    // References before any output read the zero-filled ring buffer.
    const std::vector<std::uint8_t> packed = {0x40, 0x8a, 0xeb, 0xf0};
    auto out = sf::rclib::lzDecode(packed, 4);
    check(out == std::vector<std::uint8_t>{0x8a, 0, 0, 0}, "zero-filled ring");
}

void testRclibBlock() {
    std::vector<std::uint8_t> block = {'R', 'C', 'L', 'I', 'B', '-', 'L', 0x1a, 3, 0, 0, 0, 4, 0, 0, 0,
                                       0x00, 'x', 'y', 'z'};
    sf::BinaryReader in(block);
    auto out = sf::rclib::readBlock(in);
    check(std::string(out.begin(), out.end()) == "xyz", "RCLIB block");
    check(in.atEnd(), "RCLIB block consumed");
}

void testTruncated() {
    bool threw = false;
    try {
        sf::rclib::lzDecode(std::vector<std::uint8_t>{0x80, 0x01}, 4);
    } catch (const sf::FormatError&) {
        threw = true;
    }
    check(threw, "truncated stream throws");
}

void put16(std::vector<std::uint8_t>& v, int x) {
    v.push_back(static_cast<std::uint8_t>(x & 0xff));
    v.push_back(static_cast<std::uint8_t>((x >> 8) & 0xff));
}
void put32(std::vector<std::uint8_t>& v, int x) {
    put16(v, x & 0xffff);
    put16(v, (x >> 16) & 0xffff);
}

void testAnimation() {
    // One chart; direction 0 has one layer with two frames, the rest are empty.
    std::string magic = "CHRAnimation002";
    std::vector<std::uint8_t> d(magic.begin(), magic.end());
    d.push_back(0);
    put32(d, 1);  // charts
    put16(d, 1);  // looping
    for (int dir = 0; dir < 9; ++dir) {
        put32(d, dir == 0 ? 1 : 0);  // layers
        put16(d, dir == 0 ? 2 : 0);  // frame count
        if (dir == 0) {
            put32(d, 2);
            for (int f = 0; f < 2; ++f) {
                put16(d, 0);
                put16(d, 1000);
                put32(d, 70000 + f);  // 32-bit pattern numbers in version 2
                put16(d, 5);
            }
        }
    }
    auto set = sf::AnimationSet::parse(d);
    check(set.charts.size() == 1, "animation chart count");
    const auto& dir = set.charts[0].directions[0];
    check(dir.frameCount == 2 && dir.layers.size() == 1, "animation direction");
    check(dir.layers[0][1].pattern == 70001 && dir.layers[0][1].priority == 5, "animation cell");
}

void testProjection() {
    int sx, sy;
    sf::worldToScreen(1000, 0, sx, sy);
    check(sx == 150 && sy == 100, "world to screen +x");
    sf::worldToScreen(0, 1000, sx, sy);
    check(sx == -150 && sy == 100, "world to screen +y");
    double wx, wy;
    sf::screenToWorld(-150, 100, wx, wy);
    check(std::abs(wx) < 1e-6 && std::abs(wy - 1000) < 1e-6, "screen to world");
}

void testGroundJudge() {
    // A 1x1 chip map (64x48 pixels, magnification 160) has a 4x5 judge grid
    // starting at cell (-1, -3).
    std::vector<std::uint8_t> d;
    auto i32 = [&](std::int32_t v) {
        for (int k = 0; k < 4; ++k) d.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
    };
    auto i16 = [&](std::int16_t v) {
        d.push_back(static_cast<std::uint8_t>(v));
        d.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    const std::string magic = "RPGSCRN_GNDv000";
    d.insert(d.end(), magic.begin(), magic.end());
    d.push_back(0);
    for (int v : {1, 1, 64, 48, 160, 160}) i32(v);
    d.push_back(0);  // area planes not packed
    for (int v : {0, 2, 7}) i16(static_cast<std::int16_t>(v));
    d.push_back(0);  // judge not packed
    for (int k = 0; k < 20; ++k) i16(k == 1 + 3 * 4 ? 0 : 1);  // only cell (0, 0) open
    const auto g = sf::GroundLayer::parse(d);
    check(g.judgeX == -1 && g.judgeY == -3, "judge origin");
    check(g.judgeWidth == 4 && g.judgeHeight == 5, "judge size");
    check(g.walkable(10, 150) && !g.walkable(170, 10) && !g.walkable(-10, 10), "judge lookup");
    check(!g.walkable(100000, 0), "outside the judge grid is blocked");
    check(g.onGrid(170, 10) && g.onGrid(-10, 10) && !g.onGrid(-170, 10) && !g.onGrid(100000, 0), "a blocked cell is on the grid, off it is not");
}

void testScenario() {
    std::vector<std::uint8_t> d;
    auto i32 = [&](std::int32_t v) {
        for (int k = 0; k < 4; ++k) d.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
    };
    auto text = [&](const std::string& s, std::size_t size) {
        d.insert(d.end(), s.begin(), s.end());
        d.resize(d.size() + size - s.size());
    };
    text("MCED DATA v0000\x1a", 16);
    text("System\\Game\\Parameter\\Control.aid", 0x104);
    text("Map\\c02_00.map", 0x104);
    i32(0);
    i32(0);
    i32(5);  // a cave
    text("Kanfore", 0x100);
    for (int k = 0; k < 3; ++k) i32(0);  // character type lists
    i32(0);                              // objects
    i32(1);                              // people
    i32(5);
    i32(1000002);
    i32(4);
    text("Dune", 4);
    i32(0xe0e0e0);
    i32(80);
    i32(1200);
    i32(-300);
    for (int v : {-80, -80, 79, 79}) i32(v);
    i32(3);  // direction
    i32(2);
    i32(1);
    i32(1);
    i32(1);  // has a table
    i32(2);
    d.resize(d.size() + 2 * 10);
    i32(2);  // carries Dark.njp's light 2
    d.resize(d.size() + 0x2c);
    i32(1);  // enemies
    i32(7);
    i32(2);
    i32(0);  // no name
    i32(0);
    i32(500);
    i32(600);
    for (int v : {-80, -80, 79, 79}) i32(v);
    i32(1);
    i32(0);  // no list
    i32(0);  // no table
    i32(-1);
    for (int w = 0; w < 0x13c / 4; ++w) {
        i32(w == 8 ? 40 : w == 9 ? 10 : w == 10 ? 50 : w == 11 ? 6 : w == 14 ? 3 : w == 23 ? 12 : w == 29 ? 170 : w == 32 ? 1 : w == 33 || w == 34 ? -1 : w == 35 ? 30 : w == 6 ? 5 : w == 15 ? 0x0041474f : w == 77 ? 1100 : w == 49 ? 10 : w == 51 ? 25 : w == 65 ? 4 : w == 67 ? 1 : 0);
    }
    i32(1);  // items
    i32(0);
    i32(0);
    i32(0);
    i32(0);
    i32(7000);
    i32(800);
    for (int v : {-40, -40, 39, 39}) i32(v);
    i32(8);
    i32(0);
    i32(0);
    i32(-1);
    for (int v : {3, 40000008, 0, 0}) i32(v);
    i32(1);  // entry points
    for (int v : {4, 90000, 5000, 7}) i32(v);
    const auto lit = sf::ScenarioControl::parse(d);
    check(lit.light == sf::ScenarioControl::kLit, "a scenario without its closing words is lit");
    for (int v : {0, 0, 1000}) i32(v);  // two flags and the area's light
    const auto s = sf::ScenarioControl::parse(d);
    check(s.areaKind == 5 && s.light == 1000, "scenario area kind and light");
    check(s.placements[0].light == 2 && s.placements[1].light == -1, "scenario placement lights");
    check(s.mapPath == "Map\\c02_00.map" && s.title == "Kanfore", "scenario header");
    check(s.placements.size() == 3, "scenario placement count");
    const auto& e = s.placements[1];
    check(e.group == sf::ScenarioControl::Group::Enemy && e.kind == 2 && e.x == 500, "scenario enemy");
    check(e.stats.hp == 40 && e.stats.defense == 10 && e.stats.evasion == 50 && e.stats.magicDefense == 6 && e.stats.dropList == 3,
          "scenario enemy stats");
    check(e.stats.element == 5 && e.stats.attack() == 12 && e.stats.hit() == 170, "scenario enemy element and attack");
    check(e.stats.spells[0] == 1 && e.stats.spells[1] == -1 && e.stats.spellPower[0] == 30, "scenario enemy spells");
    check(e.stats.attackCharts[1] == 4 && e.stats.spellCharts[0] == 1 && e.stats.spellCharts[1] == 0, "scenario enemy charts");
    check(e.stats.ai == "OGA" && e.stats.speed == 1100, "scenario enemy AI");
    check(e.stats.goldChance == 10 && e.stats.goldMax == 25, "scenario enemy gold");
    const auto& item = s.placements[2];
    check(item.group == sf::ScenarioControl::Group::Item && item.itemClass == 3 && item.itemCode == 40000008 && item.x == 7000,
          "scenario item");
    const auto& p = s.placements[0];
    check(p.group == sf::ScenarioControl::Group::Person && p.id == 5 && p.kind == 1000002 && p.name == "Dune",
          "scenario person");
    check(p.x == 1200 && p.y == -300 && p.direction == 3 && p.rect[2] == 79, "scenario position");
    check(s.entryPoints.size() == 1 && s.entryPoints[0].id == 4 && s.entryPoints[0].y == 5000, "scenario entry point");
}

void testAi() {
    std::vector<std::uint8_t> d;
    auto i32 = [&](std::int32_t v) {
        for (int k = 0; k < 4; ++k) d.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
    };
    const std::string magic = "RKC_AIDATA v001\x1a";
    d.insert(d.end(), magic.begin(), magic.end());
    i32(1);  // lists
    i32(2);  // events
    i32(3);
    d.insert(d.end(), {'B', 'a', 't'});
    i32(40);  // walk speed
    i32(1);   // event 0: one entry
    i32(10);  // approach
    for (int k = 0; k < 9; ++k) i32(k == 1 ? 150 : 0);
    for (int k = 0; k < 6; ++k) i32(k == 3 ? 1 : k == 5 ? 2500 : 0);
    i32(0);  // event 1: none
    const auto ai = sf::AiControl::parse(d);
    check(ai.lists.size() == 1 && ai.find("Bat") == 0 && ai.lists[0].walkSpeed == 40, "AI list");
    const auto& e = ai.lists[0].events.at(0).at(0);
    check(e.action == sf::AiControl::kApproach && e.param[1] == 150 && e.cond[5] == 2500, "AI entry");
    check(ai.lists[0].events.at(1).empty(), "AI empty event");
}

// Corrupt files must be rejected with FormatError, never crash or try to
// allocate gigabytes because of a garbage count.
template <typename Parse>
void expectRejected(const char* what, const std::string& magic, std::initializer_list<std::uint32_t> words, Parse parse) {
    std::vector<std::uint8_t> d(magic.begin(), magic.end());
    d.resize(16);
    for (std::uint32_t w : words) {
        for (int k = 0; k < 4; ++k) d.push_back(static_cast<std::uint8_t>(w >> (8 * k)));
    }
    try {
        parse(d);
        check(false, what);
    } catch (const sf::FormatError&) {
    } catch (...) {
        check(false, what);
    }
}

void testCorruptFiles() {
    const std::uint32_t huge = 0xffffffffu;
    expectRejected("corrupt animation", "CHRAnimation002", {huge}, [](auto& d) { sf::AnimationSet::parse(d); });
    expectRejected("corrupt sprite parts", "NJudgeUniPat003", {huge}, [](auto& d) { sf::SpriteBank::parse(d); });
    expectRejected("corrupt sprite patterns", "NJudgeUniPat000", {0, huge}, [](auto& d) { sf::SpriteBank::parse(d); });
    expectRejected("corrupt sprite size", "NJudgeUniPat000", {1, 8, 100000, 100000, 0}, [](auto& d) { sf::SpriteBank::parse(d); });
    expectRejected("corrupt objects", "RPGSCRN_OBJv001", {0x7fffffff}, [](auto& d) { sf::ObjectLayer::parse(d); });
    expectRejected("corrupt ground", "RPGSCRN_GNDv000", {4, 4, 64, 48, 0, 0, 0}, [](auto& d) { sf::GroundLayer::parse(d); });
    expectRejected("corrupt scenario script", "ScenaScriptV000", {huge}, [](auto& d) { sf::ScenarioScript::parse(d); });
    expectRejected("corrupt AI", "RKC_AIDATA v001", {huge, 18}, [](auto& d) { sf::AiControl::parse(d); });
    expectRejected("corrupt table", "TABLE DATA V000", {0, huge}, [](auto& d) { sf::GameTables::parse(d); });
    // An LZ header claiming far more output than its data can hold.
    std::vector<std::uint8_t> lz = {'R', 'C', 'L', 'I', 'B', '-', 'L', 0x1a};
    for (std::uint32_t w : {0x7fffffffu, 1u}) {
        for (int k = 0; k < 4; ++k) lz.push_back(static_cast<std::uint8_t>(w >> (8 * k)));
    }
    lz.push_back(0);
    try {
        sf::BinaryReader in(lz);
        sf::rclib::readBlock(in);
        check(false, "corrupt LZ size");
    } catch (const sf::FormatError&) {
    } catch (...) {
        check(false, "corrupt LZ size");
    }
}

void testScript() {
    std::vector<std::uint8_t> d;
    auto i32 = [&](std::int32_t v) {
        for (int k = 0; k < 4; ++k) d.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
    };
    const std::string magic = "ScenaScriptV000";
    d.insert(d.end(), magic.begin(), magic.end());
    d.push_back(0);
    i32(1);  // temp flags
    i32(1000000);
    i32(0);
    i32(0);  // network flags
    i32(1);  // messages
    i32(1000000);
    i32(2);
    d.push_back(static_cast<std::uint8_t>(~'H'));
    d.push_back(static_cast<std::uint8_t>(~'i'));
    i32(1);  // triggers
    for (int v : {0, 3, 10000000, 0}) i32(v);
    i32(1);  // sentences
    i32(1);  // commands
    i32(17);
    i32(2);
    for (int v : {1, 3, 1, 5}) i32(v);
    const auto s = sf::ScenarioScript::parse(d);
    check(s.messages.at(1000000) == "Hi", "script message");
    check(s.triggers.size() == 1 && s.triggers[0].event == sf::ScenarioScript::kOnJudge && s.triggers[0].character == 10000000,
          "script trigger");
    const auto& c = s.sentences.at(0).at(0);
    check(c.opcode == sf::ScenarioScript::kChangeScenario && c.operands.size() == 2 && c.operands[1].value == 5,
          "script command");
}

void testTables() {
    std::vector<std::uint8_t> d;
    auto i32 = [&](std::int32_t v) {
        for (int k = 0; k < 4; ++k) d.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
    };
    const std::string magic = "TABLE DATA V000\x1a";
    d.insert(d.end(), magic.begin(), magic.end());
    i32(0);  // not packed
    const std::size_t sizeAt = d.size();
    i32(0);  // size, patched below
    i32(1);  // one table
    i32(40);
    i32(1);  // rows
    i32(2);  // cols
    i32(7);
    i32(-3);
    i32(2);
    d.push_back(static_cast<std::uint8_t>(~'O'));
    d.push_back(static_cast<std::uint8_t>(~'k'));
    i32(0);
    const auto size = static_cast<std::int32_t>(d.size() - sizeAt - 4);
    for (int k = 0; k < 4; ++k) d[sizeAt + k] = static_cast<std::uint8_t>(size >> (8 * k));
    const auto tables = sf::GameTables::parse(d);
    const auto* t = tables.find(40);
    check(t && t->rows == 1 && t->cols == 2, "table shape");
    check(t && t->value(0, 0) == 7 && t->value(0, 1) == -3, "table values");
    check(t && t->string(0, 0) == "Ok" && t->string(0, 1).empty(), "table strings are inverted");
}

// Item.Ibn records, built field by field in file order.
struct ItemWriter {
    std::vector<std::uint8_t> d;
    void i32(int x) { put32(d, x); }
    void words(int count, int value = 0) {
        for (int i = 0; i < count; ++i) i32(value);
    }
    void text(const std::string& s) {
        i32(static_cast<int>(s.size()));
        for (char c : s) d.push_back(static_cast<std::uint8_t>(~static_cast<std::uint8_t>(c)));
    }
    // name, base name, then the shared part: subtype, code, quality, pool,
    // weight, price, fixed price, w, h, weight, inventory bank/pattern,
    // ground folder/picture, icon parameter, tint, glow layer, its tint,
    // three unknowns.
    void common(const std::string& name, int subtype, int code, int w, int h, int glow = -1) {
        text(name);
        text("?");
        for (int v : {subtype, code, 0, 1, 10, 400, 0, w, h, 30, 2, 17, 7, 5, -1, 1000, 1000, 1000, glow, 0, 0, 0, 0, 0, 0}) i32(v);
    }
};

std::vector<std::uint8_t> itemBody() {
    ItemWriter w;
    w.i32(2);  // weapons
    w.common("Sword", 0, 0, 1, 4);
    w.i32(300);                                                // durability
    for (int v : {20, 100, 0, 0, 0, 0, 0, 0, 0, 0}) w.i32(v);  // stats
    w.i32(1);                                                  // item level
    w.i32(3);                                                  // required level
    w.words(4);                                                // 0xec, effect tint
    w.i32(12);                                                 // hero layer
    w.words(8);
    w.i32(0);  // two-handed flag
    w.words(8 + 117 + 24);
    w.common("Axe", 1, 4000000, 2, 4, 3);
    w.i32(300);
    w.words(10, 5);
    w.i32(30);
    w.i32(30);
    w.words(4);
    w.i32(21);
    w.words(8);
    w.i32(0);
    w.words(8 + 117 + 24);
    w.i32(1);  // armour
    w.common("Cloth", 1, 0, 2, 3);
    w.i32(100);
    for (int v : {0, 0, 20, 20, 0, 0, 10, 20, 0, 0}) w.i32(v);
    w.i32(1);
    w.i32(1);
    w.i32(5);  // hero layer
    w.words(3 + 8 + 117 + 24);
    w.i32(0);  // accessories
    w.i32(1);  // consumables
    w.common("Tablet", -1, 0, 1, 1);
    w.i32(1);
    for (int v : {1, 200, 0, 0, 0, 0, 0, -1, 0}) w.i32(v);
    w.i32(1);  // special
    w.common("Gold", -1, 0, 1, 1);
    return w.d;
}

// Wraps a body the way Item.Ibn does: literal-only LZSS, then every packed
// byte replaced by the one the table maps back to it.
std::vector<std::uint8_t> itemFile(const std::vector<std::uint8_t>& body, const std::array<std::uint8_t, 256>& table,
                                   int checksumDelta = 0) {
    std::vector<std::uint8_t> packed;
    for (std::size_t i = 0; i < body.size(); i += 8) {
        packed.push_back(0);
        for (std::size_t k = i; k < i + 8 && k < body.size(); ++k) packed.push_back(body[k]);
    }
    std::array<std::uint8_t, 256> inverse{};
    for (int i = 0; i < 256; ++i) inverse[table[i]] = static_cast<std::uint8_t>(i);
    for (auto& b : packed) b = inverse[b];
    std::vector<std::uint8_t> d;
    for (char c : std::string("SFItemDataV0000\x1a")) d.push_back(static_cast<std::uint8_t>(c));
    int sum = 0;
    for (std::uint8_t b : body) sum += static_cast<std::int8_t>(b);
    put32(d, sum + checksumDelta);
    put32(d, 1);
    for (char c : std::string("RCLIB-L\x1a")) d.push_back(static_cast<std::uint8_t>(c));
    put32(d, static_cast<int>(body.size()));
    put32(d, static_cast<int>(packed.size()));
    d.insert(d.end(), packed.begin(), packed.end());
    return d;
}

std::array<std::uint8_t, 256> testTable() {
    std::array<std::uint8_t, 256> t{};
    for (int i = 0; i < 256; ++i) t[i] = static_cast<std::uint8_t>(i * 7 + 3);
    return t;
}

void testItems() {
    const auto table = testTable();
    const auto items = sf::ItemTable::parse(itemFile(itemBody(), table), table);
    check(items.items.size() == 5, "item count");
    using C = sf::ItemDef::Class;
    const auto* sword = items.find(C::Weapon, 0);
    check(sword && sword->name == "Sword" && sword->width == 1 && sword->height == 4 && sword->weight == 30,
          "item name, size and weight");
    check(sword && sword->stats[sf::ItemDef::kAttack] == 20 && sword->requiredLevel == 3 && sword->heroLayer == 12,
          "weapon stats, level and layer");
    check(sword && sword->invBank == 2 && sword->invPattern == 17 && sword->groundFolder == 7 && sword->groundPicture == 5,
          "item pictures");
    check(sword && !sword->twoHanded() && sword->glowLayer == -1, "one-handed sword");
    const auto* axe = items.find(C::Weapon, 4000000);
    check(axe && axe->twoHanded() && axe->glowLayer == 3 && axe->itemLevel == 30, "axes are two-handed");
    const auto* cloth = items.find(C::Armour, 0);
    check(cloth && cloth->subtype == sf::ItemDef::kBody && cloth->stats[sf::ItemDef::kDefense] == 20 && cloth->heroLayer == 5,
          "armour");
    const auto* tablet = items.find(C::Consumable, 0);
    check(tablet && tablet->effect[1] == 200, "consumable effect");
    const auto* gold = items.find(C::Special, 0);
    check(gold && gold->isGold(), "gold");
    check(!items.find(C::Accessory, 0), "missing item");

    auto rejected = [&](const char* what, const std::vector<std::uint8_t>& data) {
        try {
            sf::ItemTable::parse(data, table);
            check(false, what);
        } catch (const sf::FormatError&) {
        }
    };
    rejected("item checksum mismatch", itemFile(itemBody(), table, 1));
    auto wrongTable = table;
    std::swap(wrongTable[0], wrongTable[1]);
    try {
        sf::ItemTable::parse(itemFile(itemBody(), table), wrongTable);
        check(false, "wrong substitution table is caught");
    } catch (const sf::FormatError&) {
    }
    auto body = itemBody();
    body.resize(body.size() - 2);
    rejected("truncated item record", itemFile(body, table));
    body = itemBody();
    body[3] = 0x7f;  // a huge weapon count
    rejected("bad item count", itemFile(body, table));
    auto file = itemFile(itemBody(), table);
    file.resize(file.size() - 10);
    rejected("truncated item file", file);
    rejected("not an item file", std::vector<std::uint8_t>(64, 0));
}

// A minimal PE whose code at 0x463031 builds a table the way ShadowFlare.exe
// does: short and long mov forms, then a push and lea instructions.
void testItemSubstitutionTable() {
    const auto table = testTable();
    constexpr std::uint32_t kSectionVa = 0x1000, kFileOffset = 0x200;
    constexpr std::size_t kCodeSize = 0x46376e - 0x463031;
    std::vector<std::uint8_t> code;
    auto bytes = [&](std::initializer_list<int> list) {
        for (int b : list) code.push_back(static_cast<std::uint8_t>(b));
    };
    for (int i = 0; i < 256; ++i) {
        const int disp = 0x3c + i;
        if (disp <= 0x7f) {
            bytes({0xc6, 0x44, 0x24, disp, table[i]});
        } else if (table[i] == 0) {
            bytes({0x88, 0x9c, 0x24});  // mov [esp+disp32], bl
            put32(code, disp);
        } else {
            bytes({0xc6, 0x84, 0x24});
            put32(code, disp);
            code.push_back(table[i]);
        }
    }
    bytes({0x53});
    while (code.size() + 4 <= kCodeSize) bytes({0x8d, 0x4c, 0x24, 0x10});
    while (code.size() < kCodeSize) bytes({0x53});

    std::vector<std::uint8_t> exe(kFileOffset, 0);
    exe[0] = 'M';
    exe[1] = 'Z';
    exe[0x3c] = 0x40;
    const std::uint8_t header[] = {'P', 'E', 0, 0, 0x4c, 0x01, 1, 0};  // i386, one section
    std::copy(std::begin(header), std::end(header), exe.begin() + 0x40);
    exe[0x40 + 20] = 0xe0;  // optional header size
    const std::size_t optional = 0x40 + 24;
    for (int k = 0; k < 4; ++k) exe[optional + 28 + k] = static_cast<std::uint8_t>(0x400000 >> (8 * k));
    const std::uint32_t rawSize = 0x64000;
    std::vector<std::uint8_t> fields;
    put32(fields, static_cast<int>(rawSize));
    put32(fields, static_cast<int>(kSectionVa));
    put32(fields, static_cast<int>(rawSize));
    put32(fields, static_cast<int>(kFileOffset));
    std::copy(fields.begin(), fields.end(), exe.begin() + static_cast<std::ptrdiff_t>(optional + 0xe0 + 8));
    exe.resize(kFileOffset + rawSize, 0);
    const std::size_t codeAt = kFileOffset + (0x463031 - 0x400000 - kSectionVa);
    std::copy(code.begin(), code.end(), exe.begin() + static_cast<std::ptrdiff_t>(codeAt));
    check(sf::itemSubstitutionTable(exe) == table, "substitution table read from the executable");

    exe[codeAt] = 0x90;
    try {
        sf::itemSubstitutionTable(exe);
        check(false, "unexpected code is rejected");
    } catch (const sf::FormatError&) {
    }
    try {
        sf::itemSubstitutionTable(std::vector<std::uint8_t>(100, 0));
        check(false, "a non-executable is rejected");
    } catch (const sf::FormatError&) {
    }
}

}  // namespace

// A .Voc bank: version 3, one more copy of each voice, a 16-bit stereo
// sound, an 8-bit one and another name for the first.
void testVoiceBank() {
    std::vector<std::uint8_t> d;
    auto put32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) d.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    };
    auto put16 = [&](std::uint16_t v) {
        d.push_back(static_cast<std::uint8_t>(v));
        d.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    auto name = [&](const std::string& n) {
        std::string padded = n;
        padded.resize(0x100, '\0');
        d.insert(d.end(), padded.begin(), padded.end());
    };
    auto sound = [&](const std::string& n, int channels, int rate, int bits, std::vector<std::uint8_t> pcm) {
        put32(0);
        name(n);
        name("C:\\voice\\" + n + ".wav");
        put16(1);
        put16(static_cast<std::uint16_t>(channels));
        put32(static_cast<std::uint32_t>(rate));
        put32(static_cast<std::uint32_t>(rate * channels * bits / 8));
        put16(static_cast<std::uint16_t>(channels * bits / 8));
        put16(static_cast<std::uint16_t>(bits));
        put16(0);
        put32(static_cast<std::uint32_t>(pcm.size()));
        d.insert(d.end(), pcm.begin(), pcm.end());
    };
    const std::string magic = "VoiceData  V003";
    d.insert(d.end(), magic.begin(), magic.end());
    d.push_back(0);
    put32(3);
    put32(1);
    sound("0000", 2, 22050, 16, {1, 0, 2, 0, 3, 0, 4, 0});
    sound("0001", 1, 12000, 8, {128, 129, 130});
    put32(1);
    name("0002");
    name("0000");
    const auto bank = sf::VoiceBank::parse(d);
    check(bank.version == 3 && bank.copies == 1 && bank.voices.size() == 3, "voice bank header");
    check(bank.voices[0].channels == 2 && bank.voices[0].bits == 16 && bank.voices[0].sampleRate == 22050 &&
              bank.voices[0].frames() == 2,
          "16-bit stereo voice");
    check(bank.voices[1].sampleRate == 12000 && bank.voices[1].frames() == 3 && bank.voices[1].pcm[2] == 130, "8-bit mono voice");
    check(bank.voices[2].sameAs == 0 && bank.voices[2].pcm.empty(), "another name for a voice");
    d.pop_back();
    bool threw = false;
    try {
        sf::VoiceBank::parse(d);
    } catch (const sf::FormatError&) {
        threw = true;
    }
    check(threw, "a cut-off voice bank is refused");
}

// A palisade piece running along x: its footprint is 476 long, 90 deep.
void testWallSides() {
    sf::MapObject wall;
    wall.x = 1000;
    wall.y = 1000;
    wall.judge[0] = 0;
    wall.judge[1] = 0;
    wall.judge[2] = 476;
    wall.judge[3] = 90;
    check(wall.hides(1200, 950) && wall.surelyHides(1200, 950), "behind a wall");
    check(!wall.hides(1200, 1150) && wall.inFront(1200, 1150), "in front of a wall");
    check(!wall.surelyHides(1200, 1150), "in front is not hidden");
    // Past the wall's end and a little behind its line: hides() still says
    // behind by depth, but neither side is sure, so a piece the monster is
    // surely in front of can keep it.
    check(wall.hides(1520, 950), "beside the end, by depth");
    check(!wall.surelyHides(1520, 950) && !wall.inFront(1520, 950), "beside the end is neither side");
}

int main() {
    testWallSides();
    testVoiceBank();
    testLzLiterals();
    testLzBackReference();
    testLzZeroFill();
    testRclibBlock();
    testTruncated();
    testAnimation();
    testProjection();
    testGroundJudge();
    testScenario();
    testScript();
    testAi();
    testCorruptFiles();
    testTables();
    testItems();
    testItemSubstitutionTable();
    if (failures == 0) std::puts("all format tests passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

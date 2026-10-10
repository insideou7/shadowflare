// Small self-contained checks for the format readers. Uses synthetic data so it
// runs without the original game files.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "formats/animation.h"
#include "formats/binary_reader.h"
#include "formats/map_layers.h"
#include "formats/rclib.h"
#include "formats/scenario.h"
#include "formats/table_data.h"

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
    v.push_back(x & 0xff);
    v.push_back((x >> 8) & 0xff);
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
    for (int k = 0; k < 3; ++k) i32(0);
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
    i32(-1);
    d.resize(d.size() + 0x2c);
    i32(0);  // enemies
    i32(0);  // items
    const auto s = sf::ScenarioControl::parse(d);
    check(s.mapPath == "Map\\c02_00.map" && s.title == "Kanfore", "scenario header");
    check(s.placements.size() == 1, "scenario placement count");
    const auto& p = s.placements[0];
    check(p.group == sf::ScenarioControl::Group::Person && p.id == 5 && p.kind == 1000002 && p.name == "Dune",
          "scenario person");
    check(p.x == 1200 && p.y == -300 && p.direction == 3 && p.rect[2] == 79, "scenario position");
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

}  // namespace

int main() {
    testLzLiterals();
    testLzBackReference();
    testLzZeroFill();
    testRclibBlock();
    testTruncated();
    testAnimation();
    testProjection();
    testGroundJudge();
    testScenario();
    testTables();
    if (failures == 0) std::puts("all format tests passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

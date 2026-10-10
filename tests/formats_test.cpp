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

}  // namespace

int main() {
    testLzLiterals();
    testLzBackReference();
    testLzZeroFill();
    testRclibBlock();
    testTruncated();
    testAnimation();
    testProjection();
    if (failures == 0) std::puts("all format tests passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

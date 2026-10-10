// Small self-contained checks for the format readers. Uses synthetic data so it
// runs without the original game files.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

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

void testProjection() {
    int sx, sy;
    sf::worldToScreen(1000, 0, sx, sy);
    check(sx == 150 && sy == 100, "world to screen +x");
    sf::worldToScreen(0, 1000, sx, sy);
    check(sx == -150 && sy == 100, "world to screen +y");
}

}  // namespace

int main() {
    testLzLiterals();
    testLzBackReference();
    testLzZeroFill();
    testRclibBlock();
    testTruncated();
    testProjection();
    if (failures == 0) std::puts("all format tests passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

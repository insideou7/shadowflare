// Tests for drawing the original's ground with our textures
// (src/formats/ground_skin.h).
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "formats/ground_skin.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// A one-colour w x h tile, anchored at its centre.
void addTile(sf::SpriteBank& bank, std::uint32_t colour, int w, int h) {
    sf::SpritePart part;
    part.bpp = 32;
    part.width = w;
    part.height = h;
    part.argb.assign(static_cast<std::size_t>(w) * h, colour);
    sf::Pattern pattern;
    sf::PatternPart placed;
    placed.part = static_cast<std::int32_t>(bank.parts.size());
    placed.x = -w / 2;
    placed.y = -h / 2;
    pattern.parts.push_back(placed);
    bank.parts.push_back(part);
    bank.patterns.push_back(pattern);
}

sf::MaterialTexture uniform(std::uint32_t colour) {
    return {2, 2, std::vector<std::uint32_t>(4, colour)};
}

void testMaterialTexture() {
    sf::SpriteBank bank;
    addTile(bank, 0xff0000ffu, 4, 2);  // not part of the material
    for (std::uint32_t c : {0xff100000u, 0xff200000u, 0xff300000u, 0xff400000u}) addTile(bank, c, 4, 2);
    const auto t = sf::buildMaterialTexture(bank, 1, 2, 2, 1);
    check(t.width == 8 && t.height == 4 && t.argb.size() == 32, "material texture size");
    bool opaque = true, ours = true;
    for (std::uint32_t c : t.argb) {
        opaque = opaque && (c >> 24) == 0xff;
        ours = ours && (c & 0xff) == 0;  // never the blue tile before `first`
    }
    check(opaque, "material texture covers every pixel");
    check(ours, "material texture uses its own tiles");
    check(t.at(-1, -1) == t.at(7, 3) && t.at(8, 4) == t.at(0, 0), "material texture repeats");
    check(sf::buildMaterialTexture(bank, 2, 2, 2, 1).argb.empty(), "material texture needs all its tiles");
}

void testTinted() {
    const auto t = sf::tinted(uniform(0xff646464u), {120, 100, 255});
    check((t.argb[0] & 0xffffff) == 0x7864a0u, "tinted to the target, at most 1.6 times brighter");
}

void testPaintGround() {
    const auto red = uniform(0xffff0000u), blue = uniform(0xff0000ffu);
    std::vector<sf::GroundRule> rules(2);
    rules[0].entries = {{&red, {40, 50, 10}}, {&blue, {50, 30, 20}}};
    rules[1].entries = {{nullptr, {}}};
    // A 6 x 2 area with a margin of 1: grass-coloured on the left, dirt on
    // the right; the last column has no rule, the one before it is black.
    const int w = 6, h = 2, m = 1, fw = w + 2 * m, fh = h + 2 * m;
    std::vector<std::uint32_t> original(fw * fh);
    std::vector<std::int16_t> rule(fw * fh, 0);
    for (int y = 0; y < fh; ++y) {
        for (int x = 0; x < fw; ++x) {
            original[y * fw + x] = x < fw / 2 ? 0xff28320au : 0xff321e14u;
            if (x == fw - 2) rule[y * fw + x] = -1;
            if (x == fw - 3) rule[y * fw + x] = 1;
        }
    }
    const auto out = sf::paintGround(0, 0, w, h, m, original, rule, rules);
    check(out.size() == w * h, "painted size");
    check(out[0] == 0xffff0000u && out[w] == 0xffff0000u, "grass-coloured ground gets the grass texture");
    check(out[4] == 0xff000000u, "a black material paints black");
    check(out[5] == 0xff321e14u, "ground with no rule keeps the original");
    const std::uint32_t edge = out[3];
    check((edge & 0xff) > 0 && (edge >> 16 & 0xff) < 0xff, "colours are smoothed where materials meet");
}

}  // namespace

int main() {
    testMaterialTexture();
    testTinted();
    testPaintGround();
    if (failures == 0) std::puts("all ground skin tests passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

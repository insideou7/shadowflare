#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace sf {

// A grey picture, one byte per pixel, top row first: how much light falls
// on each pixel of the screen (0 none, 255 full), or a light to add to it.
struct LightMap {
    int width = 0, height = 0;
    std::vector<std::uint8_t> level;

    std::uint8_t at(int x, int y) const { return level[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)]; }
};

namespace lighting {

// The darkness as ShadowFlare.exe draws it in its frame code (0x41e4f0):
// Dark.njp's part 0, a 640 x 420 ellipse brightest at the middle of the
// screen, has the light of everyone and everything that carries one added
// to it, saturating at 255 (RKC_DIB::TransferToDIBEx with mode 4, whose
// table at RKC_DIBHISPEEDMODE + 0xfa300 is min(255, a + b)). The result is
// given a grey palette from the area's light (levels() below) and the
// screen is multiplied by it.
inline constexpr int kWidth = 640, kHeight = 420;
// Dark.njp's part 3, the light around each player (160 x 110).
inline constexpr int kPlayerLight = 2;
// Lights are Dark.njp parts 1-6; a character's light word picks part + 1.
inline constexpr int kLightSizes = 6;

// Adds `light` with its middle at (cx, cy); its 0 pixels add nothing.
void add(LightMap& screen, const LightMap& light, int cx, int cy);

// The brightness each light value gives at an area light (0x41e4f0): up
// to 1000 it scales the value down (value * light / 1000), above it lifts
// the value towards white (value + (255 - value) * (light - 1000) / 1000).
std::array<std::uint8_t, 256> levels(int areaLight);

// Ours, for the open content pack: a soft ellipse w x h, 255 at the middle
// falling evenly to 0 at its rim, the shape of Dark.njp's.
LightMap ellipse(int w, int h);
// The sizes of Dark.njp's parts: the screen's light, then the six lights.
inline constexpr std::array<std::array<int, 2>, 7> kPartSizes{{{640, 420}, {90, 60}, {120, 80}, {160, 110}, {200, 130}, {240, 160}, {280, 200}}};

}  // namespace lighting

}  // namespace sf

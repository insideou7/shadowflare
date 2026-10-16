#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace sf {

// What the player asks of their hero in one tick: the mouse button, Shift,
// Ctrl, the arrow keys and what the pointer is on, already turned into the
// world's terms (an enemy, an item or an object by its number in the area,
// and a point in world units). The game reads the mouse and keyboard into
// one of these and the hero's tick acts on it, never on SDL directly.
//
// This is the seam for online play (docs/online.md): the server runs each
// player's hero from the HeroInput their game sends, and checks it like
// any request, since a player can send anything.
struct HeroInput {
    // What the pointer is on, by the click priority. Enemy and Item come
    // with their number in `index`.
    enum class Aim : std::uint8_t { Ground, Enemy, Item };
    bool held = false;     // left button down: walk to the pointer, or attack, pick up or use what is under it
    bool stayPut = false;  // Shift: strike in place, never walk
    bool running = false;  // Ctrl: run (R's run mode is the hero's own)
    std::int8_t stepX = 0, stepY = 0;  // the arrow keys, -1, 0 or 1 in screen directions
    Aim aim = Aim::Ground;
    std::uint32_t index = 0;   // which enemy or item
    std::int32_t object = -1;  // the chest or switch under the pointer (a scenario placement), or -1
    float x = 0, y = 0;        // the pointer, in world units

    bool operator==(const HeroInput&) const = default;

    // A fixed-size message: a version byte, then the fields little-endian.
    static constexpr std::uint8_t kVersion = 1;
    static constexpr std::size_t kEncodedSize = 1 + 1 + 2 + 1 + 4 + 4 + 4 + 4;
    std::array<std::uint8_t, kEncodedSize> encode() const;
    // Null when the bytes are not a HeroInput this game can read: another
    // size or version, a flag or direction out of range, or a point that is
    // not a number. Whether the enemy, item or object exists is for the
    // hero's tick to check.
    static std::optional<HeroInput> decode(std::span<const std::uint8_t> bytes);
};

}  // namespace sf

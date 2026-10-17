#include "logic/hero_input.h"

#include <bit>
#include <cmath>

namespace sf {
namespace {

enum Flag : std::uint8_t { kHeld = 1, kStayPut = 2, kRunning = 4, kFlags = kHeld | kStayPut | kRunning };

void put32(std::uint8_t*& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) *out++ = static_cast<std::uint8_t>(v >> (8 * i));
}

std::uint32_t get32(const std::uint8_t*& in) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= std::uint32_t(*in++) << (8 * i);
    return v;
}

}  // namespace

std::array<std::uint8_t, HeroInput::kEncodedSize> HeroInput::encode() const {
    std::array<std::uint8_t, kEncodedSize> bytes{};
    std::uint8_t* out = bytes.data();
    *out++ = kVersion;
    *out++ = static_cast<std::uint8_t>((held ? kHeld : 0) | (stayPut ? kStayPut : 0) | (running ? kRunning : 0));
    *out++ = static_cast<std::uint8_t>(stepX);
    *out++ = static_cast<std::uint8_t>(stepY);
    *out++ = static_cast<std::uint8_t>(aim);
    put32(out, index);
    put32(out, static_cast<std::uint32_t>(object));
    put32(out, std::bit_cast<std::uint32_t>(x));
    put32(out, std::bit_cast<std::uint32_t>(y));
    return bytes;
}

std::optional<HeroInput> HeroInput::decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != kEncodedSize || bytes[0] != kVersion) return std::nullopt;
    const std::uint8_t* in = bytes.data() + 1;
    HeroInput h;
    const std::uint8_t flags = *in++;
    if (flags & ~kFlags) return std::nullopt;
    h.held = flags & kHeld;
    h.stayPut = flags & kStayPut;
    h.running = flags & kRunning;
    h.stepX = static_cast<std::int8_t>(*in++);
    h.stepY = static_cast<std::int8_t>(*in++);
    if (h.stepX < -1 || h.stepX > 1 || h.stepY < -1 || h.stepY > 1) return std::nullopt;
    const std::uint8_t aim = *in++;
    if (aim > static_cast<std::uint8_t>(Aim::Item)) return std::nullopt;
    h.aim = static_cast<Aim>(aim);
    h.index = get32(in);
    h.object = static_cast<std::int32_t>(get32(in));
    if (h.object < -1) return std::nullopt;
    h.x = std::bit_cast<float>(get32(in));
    h.y = std::bit_cast<float>(get32(in));
    if (!std::isfinite(h.x) || !std::isfinite(h.y)) return std::nullopt;
    return h;
}

}  // namespace sf

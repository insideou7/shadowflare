#include "logic/talk_request.h"

namespace sf {

bool TalkRequest::valid() const {
    switch (kind) {
        case Kind::TalkTo:
        case Kind::UseObject: return index >= 0;
        case Kind::Answer: return index >= -1 && index < kMaxOptions;
    }
    return false;  // a kind byte this game does not know
}

std::array<std::uint8_t, TalkRequest::kEncodedSize> TalkRequest::encode() const {
    const auto u = static_cast<std::uint32_t>(index);
    return {kVersion,
            static_cast<std::uint8_t>(kind),
            static_cast<std::uint8_t>(u),
            static_cast<std::uint8_t>(u >> 8),
            static_cast<std::uint8_t>(u >> 16),
            static_cast<std::uint8_t>(u >> 24)};
}

std::optional<TalkRequest> TalkRequest::decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != kEncodedSize || bytes[0] != kVersion || bytes[1] >= kKindCount) return std::nullopt;
    TalkRequest r;
    r.kind = static_cast<Kind>(bytes[1]);
    r.index = static_cast<std::int32_t>(std::uint32_t(bytes[2]) | std::uint32_t(bytes[3]) << 8 | std::uint32_t(bytes[4]) << 16 |
                                        std::uint32_t(bytes[5]) << 24);
    if (!r.valid()) return std::nullopt;
    return r;
}

}  // namespace sf

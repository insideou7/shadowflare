#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace sf {

// Talking and quest choices: walking up to a person to talk, going to a
// chest, switch or other object to use it, and the answer chosen when a
// conversation closes. The click handlers and the dialogue box turn the
// mouse and keys into these, and Game::applyTalkRequest checks each one
// against what is there before acting, so online the server can take them
// from a player's game (docs/online.md). A person or object is named by its
// number in the area, an answer by its place in the options shown.
struct TalkRequest {
    enum class Kind : std::uint8_t {
        TalkTo,     // index: a person (not a monster) to walk up to and talk to
        UseObject,  // index: a scenario placement to walk up to and use
        Answer,     // index: the option chosen, or -1 to close a box with none
    };
    static constexpr int kKindCount = static_cast<int>(Kind::Answer) + 1;
    // More options than any conversation shows.
    static constexpr int kMaxOptions = 64;

    Kind kind = Kind::Answer;
    std::int32_t index = -1;

    static TalkRequest talkTo(std::size_t person) { return {Kind::TalkTo, static_cast<std::int32_t>(person)}; }
    static TalkRequest useObject(std::size_t placement) { return {Kind::UseObject, static_cast<std::int32_t>(placement)}; }
    static TalkRequest answer(int option) { return {Kind::Answer, option}; }

    bool operator==(const TalkRequest&) const = default;

    // A known kind, a person or object number of 0 or more, an answer from
    // -1 to below kMaxOptions. Whether the person, object or option is there
    // is for the game to say.
    bool valid() const;

    // A fixed-size message: a version byte, the kind, the index little-endian.
    static constexpr std::uint8_t kVersion = 1;
    static constexpr std::size_t kEncodedSize = 1 + 1 + 4;
    std::array<std::uint8_t, kEncodedSize> encode() const;
    // Null for another size or version, or a request that is not valid().
    static std::optional<TalkRequest> decode(std::span<const std::uint8_t> bytes);
};

}  // namespace sf

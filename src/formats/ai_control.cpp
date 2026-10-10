#include "formats/ai_control.h"

#include "formats/binary_reader.h"

namespace sf {

namespace {

std::int32_t count(BinaryReader& in, std::size_t itemSize) {
    const std::int32_t n = in.i32();
    if (n < 0 || static_cast<std::size_t>(n) * itemSize > in.remaining()) throw FormatError("bad count in AI file");
    return n;
}

}  // namespace

int AiControl::find(const std::string& name) const {
    for (std::size_t i = 0; i < lists.size(); ++i) {
        if (lists[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

AiControl AiControl::parse(std::span<const std::uint8_t> data) {
    BinaryReader in(data);
    const std::string_view magic = in.chars(16);
    if (!magic.starts_with("RKC_AIDATA v")) throw FormatError("not an AI data file");
    const int version = magic[12] == '0' && magic[13] == '0' ? magic[14] - '0' : 0;
    AiControl ai;
    const std::int32_t listCount = count(in, 4);
    const std::int32_t eventCount = in.i32();
    if (eventCount < 0 || eventCount > 256) throw FormatError("bad event count in AI file");
    ai.lists.resize(static_cast<std::size_t>(listCount));
    for (auto& list : ai.lists) {
        list.name = std::string(in.chars(static_cast<std::size_t>(count(in, 1))));
        if (version >= 1) list.walkSpeed = in.i32();
        list.events.resize(static_cast<std::size_t>(eventCount));
        for (auto& entries : list.events) {
            entries.resize(static_cast<std::size_t>(count(in, 4 + 0x24 + 0x18)));
            for (auto& e : entries) {
                e.action = in.i32();
                for (auto& p : e.param) p = in.i32();
                for (auto& c : e.cond) c = in.i32();
            }
        }
    }
    return ai;
}

}  // namespace sf

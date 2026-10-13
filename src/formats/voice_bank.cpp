#include "formats/voice_bank.h"

#include <algorithm>
#include <cstring>

#include "formats/binary_reader.h"

namespace sf {

namespace {

constexpr std::size_t kNameSize = 0x100;
constexpr std::uint32_t kAlias = 1;
constexpr int kPcm = 1;  // WAVE_FORMAT_PCM

std::string cString(std::string_view chars) { return std::string(chars.substr(0, chars.find('\0'))); }

}  // namespace

std::size_t VoiceBank::Voice::frames() const {
    const int frameBytes = channels * bits / 8;
    return frameBytes > 0 ? pcm.size() / static_cast<std::size_t>(frameBytes) : 0;
}

VoiceBank VoiceBank::parse(std::span<const std::uint8_t> data) {
    BinaryReader r(data);
    const std::string_view magic = r.chars(16);
    if (magic.substr(0, 12) != "VoiceData  V") throw FormatError("not a .Voc voice bank");
    VoiceBank bank;
    for (char c : magic.substr(12, 3)) {
        if (c < '0' || c > '9') throw FormatError("bad .Voc version");
        bank.version = bank.version * 10 + (c - '0');
    }
    const std::int32_t count = r.i32();
    if (count < 0 || count > 100000) throw FormatError("bad .Voc voice count");
    if (bank.version >= 3) bank.copies = std::max(0, r.i32());
    bank.voices.reserve(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        Voice v;
        const std::uint32_t flags = r.u32();
        v.name = cString(r.chars(kNameSize));
        if (flags & kAlias) {
            const std::string other = cString(r.chars(kNameSize));
            for (std::size_t k = 0; k < bank.voices.size(); ++k) {
                if (bank.voices[k].name == other) v.sameAs = static_cast<int>(k);
            }
            bank.voices.push_back(std::move(v));
            continue;
        }
        r.skip(kNameSize);  // the file it was made from
        const int format = static_cast<std::uint16_t>(r.i16());
        v.channels = static_cast<std::uint16_t>(r.i16());
        v.sampleRate = static_cast<int>(r.u32());
        r.skip(4 + 2);  // bytes a second, block align
        v.bits = static_cast<std::uint16_t>(r.i16());
        r.skip(2);  // cbSize
        const std::uint32_t size = r.u32();
        const auto pcm = r.bytes(size);
        if (format != kPcm || (v.channels != 1 && v.channels != 2) || (v.bits != 8 && v.bits != 16) || v.sampleRate <= 0) {
            throw FormatError("unsupported .Voc sound format");
        }
        v.pcm.assign(pcm.begin(), pcm.end());
        bank.voices.push_back(std::move(v));
    }
    return bank;
}

}  // namespace sf

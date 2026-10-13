// The open content pack's sounds: Ogg Vorbis files (docs/content.md).
#include <cstdlib>

#include "formats/binary_reader.h"
#include "formats/open_content.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

namespace sf::open {

VoiceBank::Voice decodeSound(std::span<const std::uint8_t> data) {
    int channels = 0, rate = 0;
    short* samples = nullptr;
    const int frames = stb_vorbis_decode_memory(data.data(), static_cast<int>(data.size()), &channels, &rate, &samples);
    if (frames < 0 || !samples) throw FormatError("not an Ogg Vorbis sound");
    VoiceBank::Voice v;
    v.sampleRate = rate;
    v.channels = channels;
    v.bits = 16;
    if (channels == 1 || channels == 2) {
        const std::size_t count = static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels);
        v.pcm.reserve(count * 2);
        for (std::size_t i = 0; i < count; ++i) {
            const auto s = static_cast<std::uint16_t>(samples[i]);
            v.pcm.push_back(static_cast<std::uint8_t>(s & 0xff));
            v.pcm.push_back(static_cast<std::uint8_t>(s >> 8));
        }
    }
    std::free(samples);
    if (v.pcm.empty() && frames > 0) throw FormatError("sounds must be mono or stereo");
    return v;
}

}  // namespace sf::open

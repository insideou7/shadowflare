#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf {

// A .Voc file: a bank of sounds in one file, as RKC_DSOUND.dll reads it
// (RKC_DSOUND_VOC::Read). System/Game/Voice/Voice00.Voc holds the sound
// effects and System/Game/Music/BGMnn.Voc and System/Title/Music/BGM00.Voc
// one piece of music each.
//
// Layout: "VoiceData  V00n" padded to 16 bytes, the voice count, and from
// version 3 on how many more copies of each voice may play at once. Then
// each voice: 32-bit flags and a 256-byte name; flag bit 0 makes it another
// name for an earlier voice (a 256-byte name follows), otherwise a 256-byte
// source path, a WAVEFORMATEX (18 bytes), the data size and the PCM data.
struct VoiceBank {
    struct Voice {
        std::string name;
        int sampleRate = 0;
        int channels = 1;
        int bits = 8;  // 8: unsigned, 16: signed
        std::vector<std::uint8_t> pcm;
        // For another name of an earlier voice: that voice's index.
        int sameAs = -1;

        std::size_t frames() const;
    };

    int version = 0;
    int copies = 0;  // more copies of each voice that may play at once
    std::vector<Voice> voices;

    // Throws FormatError on a file that does not match.
    static VoiceBank parse(std::span<const std::uint8_t> data);
};

}  // namespace sf

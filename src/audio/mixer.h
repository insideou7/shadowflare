#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include "formats/voice_bank.h"

namespace sf {

// Plays the voices of loaded .Voc banks, the way RKC_DSOUND.dll does with
// DirectSound buffers, and mixes them into stereo samples. It has no
// device of its own (platform/audio.h feeds one), so it runs in the game's
// one thread and can be tested without sound hardware.
class Mixer {
public:
    static constexpr int kRate = 22050;  // output frames a second
    // DirectSound attenuation, in hundredths of a decibel: 0 is full volume
    // and -10000 silent.
    static constexpr int kSilent = -10000;
    // The banks the game uses: sound effects (Voice00.Voc) and music.
    static constexpr int kSoundBank = 0;
    static constexpr int kMusicBank = 500;

    // The attenuation for a volume slider at 0-10, as the options code
    // works it out: 0 is silent, otherwise -3000 + 300 a step.
    static int attenuation(int volume);
    // The amplitude scale for an attenuation.
    static double gain(int attenuation);

    // Puts a bank in a slot, stopping whatever the slot was playing
    // (RKC_DSOUND::ReadVocFile).
    void load(int bank, VoiceBank voices);
    void release(int bank);
    bool has(int bank, int voice) const;

    // Starts a voice from its start (RKC_DSOUND_VOC::Play): on a copy that
    // is not playing, or when all are busy, on the one that has played
    // longest. Returns false for a voice that is not loaded.
    bool play(int bank, int voice, bool loop, int attenuation);
    void stop(int bank, int voice);
    bool playing(int bank, int voice) const;
    // Changes the volume of a voice's copies as they play.
    void setVolume(int bank, int voice, int attenuation);

    // Fills interleaved left and right samples at kRate, moving every
    // playing voice on.
    void mix(std::span<std::int16_t> stereo);

private:
    struct Copy {
        bool on = false, loop = false;
        double position = 0;  // in the voice's own frames
        double gain = 1;
        std::uint64_t started = 0;
    };
    struct Bank {
        VoiceBank data;
        std::vector<std::vector<Copy>> copies;  // per voice
    };
    const VoiceBank::Voice* sound(const Bank& bank, int voice) const;

    std::map<int, Bank> banks_;
    std::uint64_t plays_ = 0;
    std::vector<float> scratch_;
};

}  // namespace sf

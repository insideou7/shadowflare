#pragma once

#include <cstdint>
#include <vector>

namespace sf {

class Mixer;

// The sound device. Rather than have SDL call back from its own thread, the
// main loop tops up SDL's queue from the mixer each frame, so the game
// stays in one thread (and runs the same in a browser).
class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    bool open() const { return device_ != 0; }
    // Mixes enough to keep about a tenth of a second queued.
    void pump(Mixer& mixer);

private:
    std::uint32_t device_ = 0;
    std::vector<std::int16_t> buffer_;
};

}  // namespace sf

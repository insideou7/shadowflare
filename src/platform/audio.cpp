#include "platform/audio.h"

#include <SDL.h>

#include "audio/mixer.h"
#include "core/log.h"

namespace sf {

namespace {

constexpr int kQueuedFrames = Mixer::kRate / 10;
constexpr int kFrameBytes = 2 * static_cast<int>(sizeof(std::int16_t));

}  // namespace

AudioOutput::AudioOutput() {
    SDL_AudioSpec want{};
    want.freq = Mixer::kRate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    SDL_AudioSpec have{};
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (device_ == 0) {
        log::warn("No sound: {}", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(device_, 0);
}

AudioOutput::~AudioOutput() {
    if (device_ != 0) SDL_CloseAudioDevice(device_);
}

void AudioOutput::pump(Mixer& mixer) {
    if (device_ == 0) return;
    const int queued = static_cast<int>(SDL_GetQueuedAudioSize(device_)) / kFrameBytes;
    if (queued >= kQueuedFrames) return;
    buffer_.resize(static_cast<std::size_t>(kQueuedFrames - queued) * 2);
    mixer.mix(buffer_);
    SDL_QueueAudio(device_, buffer_.data(), static_cast<Uint32>(buffer_.size() * sizeof(std::int16_t)));
}

}  // namespace sf

#include "audio/mixer.h"

#include <algorithm>
#include <cmath>

namespace sf {

int Mixer::attenuation(int volume) { return volume <= 0 ? kSilent : -3000 + 300 * std::min(volume, 10); }

double Mixer::gain(int attenuation) {
    if (attenuation <= kSilent) return 0;
    return std::pow(10.0, std::min(attenuation, 0) / 2000.0);
}

void Mixer::load(int bank, VoiceBank voices) {
    Bank b;
    b.copies.assign(voices.voices.size(), std::vector<Copy>(static_cast<std::size_t>(1 + voices.copies)));
    b.data = std::move(voices);
    banks_[bank] = std::move(b);
}

void Mixer::release(int bank) { banks_.erase(bank); }

const VoiceBank::Voice* Mixer::sound(const Bank& bank, int voice) const {
    if (voice < 0 || static_cast<std::size_t>(voice) >= bank.data.voices.size()) return nullptr;
    const VoiceBank::Voice* v = &bank.data.voices[static_cast<std::size_t>(voice)];
    if (v->sameAs >= 0) v = &bank.data.voices[static_cast<std::size_t>(v->sameAs)];
    return v->frames() > 0 ? v : nullptr;
}

bool Mixer::has(int bank, int voice) const {
    const auto b = banks_.find(bank);
    return b != banks_.end() && sound(b->second, voice);
}

bool Mixer::play(int bank, int voice, bool loop, int attenuation) {
    const auto b = banks_.find(bank);
    if (b == banks_.end() || !sound(b->second, voice)) return false;
    auto& copies = b->second.copies[static_cast<std::size_t>(voice)];
    auto pick = std::ranges::find_if(copies, [](const Copy& c) { return !c.on; });
    if (pick == copies.end()) pick = std::ranges::min_element(copies, {}, &Copy::started);
    *pick = {true, loop, 0, gain(attenuation), ++plays_};
    return true;
}

void Mixer::stop(int bank, int voice) {
    const auto b = banks_.find(bank);
    if (b == banks_.end() || voice < 0 || static_cast<std::size_t>(voice) >= b->second.copies.size()) return;
    for (Copy& c : b->second.copies[static_cast<std::size_t>(voice)]) c.on = false;
}

bool Mixer::playing(int bank, int voice) const {
    const auto b = banks_.find(bank);
    if (b == banks_.end() || voice < 0 || static_cast<std::size_t>(voice) >= b->second.copies.size()) return false;
    return std::ranges::any_of(b->second.copies[static_cast<std::size_t>(voice)], &Copy::on);
}

void Mixer::setVolume(int bank, int voice, int attenuation) {
    const auto b = banks_.find(bank);
    if (b == banks_.end() || voice < 0 || static_cast<std::size_t>(voice) >= b->second.copies.size()) return;
    for (Copy& c : b->second.copies[static_cast<std::size_t>(voice)]) c.gain = gain(attenuation);
}

namespace {

// One sample of a voice, -1 to 1.
float sampleAt(const VoiceBank::Voice& v, std::size_t frame, int channel) {
    const std::size_t index = frame * static_cast<std::size_t>(v.channels) + static_cast<std::size_t>(std::min(channel, v.channels - 1));
    if (v.bits == 8) return static_cast<float>(static_cast<int>(v.pcm[index]) - 128) / 128.0f;
    const auto lo = v.pcm[index * 2], hi = v.pcm[index * 2 + 1];
    return static_cast<std::int16_t>(lo | (hi << 8)) / 32768.0f;
}

}  // namespace

void Mixer::mix(std::span<std::int16_t> stereo) {
    const std::size_t frames = stereo.size() / 2;
    scratch_.assign(frames * 2, 0.0f);
    for (auto& [id, bank] : banks_) {
        for (std::size_t voice = 0; voice < bank.copies.size(); ++voice) {
            const VoiceBank::Voice* v = sound(bank, static_cast<int>(voice));
            if (!v) continue;
            const std::size_t length = v->frames();
            const double step = static_cast<double>(v->sampleRate) / kRate;
            for (Copy& c : bank.copies[voice]) {
                if (!c.on) continue;
                const auto g = static_cast<float>(c.gain);
                for (std::size_t f = 0; f < frames; ++f) {
                    if (c.position >= static_cast<double>(length)) {
                        if (!c.loop) {
                            c.on = false;
                            break;
                        }
                        c.position = std::fmod(c.position, static_cast<double>(length));
                    }
                    // Straight lines between the voice's own samples.
                    const auto at = static_cast<std::size_t>(c.position);
                    const std::size_t next = at + 1 < length ? at + 1 : c.loop ? 0 : at;
                    const auto t = static_cast<float>(c.position - static_cast<double>(at));
                    for (int ch = 0; ch < 2; ++ch) {
                        const float a = sampleAt(*v, at, ch), b = sampleAt(*v, next, ch);
                        scratch_[f * 2 + static_cast<std::size_t>(ch)] += (a + (b - a) * t) * g;
                    }
                    c.position += step;
                }
                if (c.on && !c.loop && c.position >= static_cast<double>(length)) c.on = false;
            }
        }
    }
    for (std::size_t i = 0; i < frames * 2; ++i) {
        stereo[i] = static_cast<std::int16_t>(std::lround(std::clamp(scratch_[i], -1.0f, 1.0f) * 32767.0f));
    }
}

}  // namespace sf

// Sound effects and music from the original's .Voc banks, played the way
// ShadowFlare.exe asks RKC_DSOUND.dll to play them (see docs/sound.md).
#include <cmath>
#include <format>

#include "core/log.h"
#include "formats/binary_reader.h"
#include "game/game.h"
#include "platform/filesystem.h"

namespace sf {

namespace {

constexpr const char* kVoices = "System/Game/Voice/Voice00.Voc";
constexpr const char* kTitleMusic = "System/Title/Music/BGM00.Voc";
// How far from the hero a sound is heard (0x417260).
constexpr int kHearing = 3000;

std::optional<VoiceBank> readBank(const FileSystem& data, const std::string& path) {
    const auto bytes = data.read(path);
    if (!bytes) return std::nullopt;
    try {
        return VoiceBank::parse(*bytes);
    } catch (const FormatError& e) {
        log::warn("{}: {}", path, e.what());
        return std::nullopt;
    }
}

}  // namespace

void Game::loadSounds() {
    auto bank = readBank(*data_, kVoices);
    if (!bank) return;
    log::info("{} sounds in {}", bank->voices.size(), kVoices);
    mixer_.load(Mixer::kSoundBank, std::move(*bank));
}

void Game::playSound(int voice, double x, double y, bool everywhere) {
    if (voice < 0 || !player_) return;
    if (!everywhere && static_cast<int>(std::hypot(x - player_->x, y - player_->y)) > kHearing) return;
    mixer_.play(Mixer::kSoundBank, voice, false, Mixer::attenuation(settings_.effectVolume));
}

void Game::playSound(int voice) {
    if (voice >= 0) mixer_.play(Mixer::kSoundBank, voice, false, Mixer::attenuation(settings_.effectVolume));
}

void Game::loadMusic(const std::string& path) {
    mixer_.release(Mixer::kMusicBank);
    musicFile_ = path;
    if (auto bank = readBank(*data_, path)) mixer_.load(Mixer::kMusicBank, std::move(*bank));
}

// The scenario's third word picks BGM00-BGM10; the file is only read again
// when the number changes, but the music starts from the top either way.
void Game::startAreaMusic(int areaKind) {
    if (areaKind != areaMusic_) {
        loadMusic(std::format("System/Game/Music/BGM{:02}.Voc", areaKind));
        areaMusic_ = areaKind;
    }
    musicTicks_ = 0;
}

void Game::tickMusic() {
    const int volume = Mixer::attenuation(settings_.musicVolume);
    mixer_.setVolume(Mixer::kMusicBank, 0, volume);
    // The title screen, and the new character and options screens it
    // leads to, play the title's music, looping, if it is not already on.
    if (menuOpen_ || newCharacter_ || (optionsOpen_ && !optionsInGame_)) {
        if (musicFile_ != kTitleMusic) {
            loadMusic(kTitleMusic);
            areaMusic_ = -1;
        }
        if (!mixer_.playing(Mixer::kMusicBank, 0)) mixer_.play(Mixer::kMusicBank, 0, true, volume);
        return;
    }
    if (areaMusic_ < 0) return;
    if (musicTicks_ == 0) mixer_.play(Mixer::kMusicBank, 0, true, volume);
    ++musicTicks_;
}

}  // namespace sf

// Sound effects and music from the original's .Voc banks, played the way
// ShadowFlare.exe asks RKC_DSOUND.dll to play them (see docs/sound.md).
#include <array>
#include <cmath>
#include <format>
#include <iterator>
#include <string>

#include "core/log.h"
#include "formats/binary_reader.h"
#include "formats/open_content.h"
#include "game/game.h"
#include "platform/filesystem.h"

namespace sf {

namespace {

constexpr const char* kVoices = "System/Game/Voice/Voice00.Voc";
constexpr const char* kTitleMusic = "System/Title/Music/BGM00.Voc";
// How far from the hero a sound is heard (0x417260).
constexpr int kHearing = 3000;

// More copies of each effect that may play at once, as in Voice00.Voc.
constexpr int kOpenCopies = 4;

std::optional<VoiceBank::Voice> readOpenSound(const FileSystem& data, const std::string& path) {
    const auto bytes = data.read(path);
    if (!bytes) return std::nullopt;
    try {
        return open::decodeSound(*bytes);
    } catch (const FormatError& e) {
        log::warn("{}: {}", path, e.what());
        return std::nullopt;
    }
}

// The open content pack has no .Voc banks: the effects are
// System/Game/Voice/<voice>.ogg, one a voice, and each piece of music is
// the .Voc's own path ending in .ogg (docs/content.md).
std::optional<VoiceBank> readOpenBank(const FileSystem& data, const std::string& path) {
    const auto dot = path.rfind('.');
    if (path == kVoices) {
        const std::string folder = path.substr(0, path.rfind('/'));
        VoiceBank bank;
        bank.copies = kOpenCopies;
        for (const auto& name : data.list(folder)) {
            if (name.size() < 5 || name.substr(name.size() - 4) != ".ogg") continue;
            const std::string stem = name.substr(0, name.size() - 4);
            if (stem.empty() || stem.size() > 4 || stem.find_first_not_of("0123456789") != std::string::npos) continue;
            const auto voice = static_cast<std::size_t>(std::stoi(stem));
            auto sound = readOpenSound(data, folder + "/" + name);
            if (!sound) continue;
            if (bank.voices.size() <= voice) bank.voices.resize(voice + 1);
            bank.voices[voice] = std::move(*sound);
        }
        if (bank.voices.empty()) return std::nullopt;
        return bank;
    }
    auto sound = readOpenSound(data, path.substr(0, dot) + ".ogg");
    if (!sound) return std::nullopt;
    VoiceBank bank;
    bank.voices.push_back(std::move(*sound));
    return bank;
}

std::optional<VoiceBank> readBank(const FileSystem& data, const std::string& path) {
    const auto bytes = data.read(path);
    if (!bytes) return readOpenBank(data, path);
    try {
        return VoiceBank::parse(*bytes);
    } catch (const FormatError& e) {
        log::warn("{}: {}", path, e.what());
        return std::nullopt;
    }
}

}  // namespace

// Footsteps: one as the hero sets off, then every 12 of the original's
// ticks walking or 8 running (0x4351f0, 0x435530).
void Game::heroSteps(bool walking, bool running) {
    if (!walking) {
        stepTicks_ = -1;
        return;
    }
    const double period = running ? 8 : 12;
    if (stepTicks_ < 0 || running != stepRunning_) stepTicks_ = 0;
    stepRunning_ = running;
    const double from = stepTicks_;
    stepTicks_ += 1.5;
    if (std::ceil(from / period) * period < stepTicks_) playSound(0, player_->x, player_->y);
}

void Game::loadSounds() {
    auto bank = readBank(*data_, kVoices);
    if (!bank) return;
    log::info("{} sounds", bank->voices.size());
    mixer_.load(Mixer::kSoundBank, std::move(*bank));
}

void Game::playSound(int voice, double x, double y, bool everywhere) { world_.sound(voice, x, y, everywhere); }

void Game::playSound(int voice) { world_.events.sound(voice); }

void Game::deliverEvents() {
    if (world_.events.empty()) return;
    for (const auto& s : world_.events.takeSounds()) {
        // A sound in the world is heard by the hero, if they are near.
        if (s.placed && (!player_ || (!s.everywhere && static_cast<int>(std::hypot(s.x - player_->x, s.y - player_->y)) > kHearing))) {
            continue;
        }
        mixer_.play(Mixer::kSoundBank, s.voice, false, Mixer::attenuation(settings_.effectVolume));
    }
    for (auto& text : world_.events.takeMessages()) {
        message_ = std::move(text);
        messageTicks_ = kMessageTicks;
    }
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

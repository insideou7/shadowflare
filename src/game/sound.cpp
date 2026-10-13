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

// Each monster kind's (Character/ENEMY folder's) death cry, -1 for none
// (0x45bec0 reads it from DAT_004815d8).
constexpr int kDeathCry[] = {-1,  105, 104, 107, 106, 104, -1, -1,  114, 111, 118, 118, 117,
                             114, 133, 130, 148, 148, 142, -1, -1, 151, 111, 114, 136};

// The sounds monsters' animations call for (0x45a2a0): a frame with cue
// flag 0x400, 0x800 or 0x1000 (slot 0-2) plays the kind's sound for that
// slot and animation (DAT_00480a20), or else the one every kind shares
// (DAT_004809a8: 86 on the death animation, nothing else). Animations: 0
// stand, 1 walk, 2 hit, 3 death, 4-6 blows, 7-9 spells. Only the kinds
// that have any are listed.
struct MonsterCue {
    int kind, slot;
    std::array<int, 10> voice;
};
constexpr int N = -1;
constexpr MonsterCue kMonsterCues[] = {
    {3, 0, {N, N, N, N, N, N, N, 91, 91, N}},
    {3, 1, {90, 90, 90, 90, 90, 90, 90, 90, 90, N}},
    {4, 0, {N, 92, N, 87, N, N, N, N, N, N}},
    {4, 1, {89, 89, 89, 89, 89, 89, 89, 89, 89, N}},
    {7, 0, {N, 108, N, N, N, N, N, N, N, N}},
    {8, 0, {N, N, N, N, 113, 112, N, 112, N, 110}},
    {9, 0, {N, N, N, N, N, N, N, N, N, 110}},
    {12, 0, {N, N, N, N, 116, N, N, 115, 115, N}},
    {13, 0, {N, N, N, N, 113, 112, N, N, N, N}},
    {14, 0, {N, N, N, N, 131, N, N, N, N, N}},
    {15, 0, {N, N, N, N, 129, N, N, 129, N, N}},
    {16, 0, {N, N, N, N, 146, 146, N, N, N, N}},
    {16, 1, {N, N, N, N, N, 147, N, N, N, N}},
    {17, 0, {N, N, N, N, 123, N, N, 124, 124, N}},
    {18, 0, {N, N, N, N, 140, N, N, 141, N, N}},
    {19, 0, {N, N, N, N, 3, 3, N, N, N, N}},
    {20, 0, {N, 108, N, N, 3, 3, N, N, N, N}},
    {21, 0, {N, N, N, N, 155, N, N, 155, N, N}},
    {22, 0, {N, N, N, N, N, N, N, N, N, 110}},
    {23, 0, {N, N, N, N, 147, N, N, 106, N, N}},
    {24, 0, {90, 90, N, N, 134, N, N, 134, 135, N}},
};
constexpr int kSharedDeathCue = 86;

int monsterCue(int kind, int slot, int anim) {
    if (anim < 0 || anim >= 10) return -1;
    for (const auto& c : kMonsterCues) {
        if (c.kind == kind && c.slot == slot && c.voice[static_cast<std::size_t>(anim)] >= 0) return c.voice[static_cast<std::size_t>(anim)];
    }
    return anim == chart::kEnemyDeath ? kSharedDeathCue : -1;
}

}  // namespace

void Game::monsterCries(Actor& enemy) {
    const int cues = enemy.character.takeCues();
    if (cues == 0) return;
    for (int slot = 0; slot < 3; ++slot) {
        if (cues & (0x400 << slot)) playSound(monsterCue(enemy.placement.kind, slot, enemy.character.chart()), enemy.character.x, enemy.character.y);
    }
}

void Game::monsterDeathCry(const Actor& enemy) {
    const int kind = enemy.placement.kind;
    if (kind >= 0 && kind < static_cast<int>(std::size(kDeathCry))) playSound(kDeathCry[kind], enemy.character.x, enemy.character.y);
}

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

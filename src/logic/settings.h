#pragma once

#include <array>
#include <string>
#include <string_view>

namespace sf {

// The player's options, as on the original's Settings Menu (Esc in the
// game). The original keeps them in SFlare.Cfg, sixteen 32-bit numbers
// (read at 0x401eb0, written at 0x401d10); we keep ours in a small text
// file of key=value lines next to the saved game.
struct Settings {
    // What a click picks first where things overlap, as the original's
    // Click Priority row orders them.
    enum Target { kEnemy, kItem, kObject, kPerson, kCompanion, kTargetCount };
    // Click Range sizes: MINI, SMAL, NORM, LARG, MAX.
    static constexpr int kClickSizes = 5;
    // Volumes run from 0 (silent) to kMaxVolume. The original stores
    // DirectSound attenuation, -10000 to 0 hundredths of a decibel.
    static constexpr int kMaxVolume = 10;
    // Cheats tab: the experience rates to pick from, in percent; 100 is
    // the game as it comes.
    static constexpr std::array<int, 5> kXpRates{100, 200, 500, 1000, 2000};
    static constexpr int kXpRateNormal = 100;
    // Ours: how big the world is drawn, in percent. Each notch of the
    // scroll wheel moves it kZoomStep, from 100 (the original's view)
    // out to kMinZoom.
    static constexpr int kMinZoom = 50, kMaxZoom = 100, kZoomStep = 10;

    bool fullScreen = false;          // Screen Mode at Start: FULL, else WINDOW
    bool seeThroughPlayer = true;     // Semi-transparent Player: seen through walls
    bool seeThroughShadows = true;    // Semi-transparent Shadow: else solid black
    bool darkness = true;             // Display Darkness in dungeons
    bool saveImageAtEnd = true;       // Save Image at Game End (for the load screen)
    bool clickRange = true;           // Click Range ON: a square around the cursor
    int clickSize = 2;                // Click Range size, 0-4; NORM, as the original's default
    std::array<int, kTargetCount> clickPriority{kEnemy, kItem, kObject, kPerson, kCompanion};
    int effectVolume = kMaxVolume;
    int musicVolume = kMaxVolume;
    // Ours, not the original's: play by the original's rules, bugs included
    // (logic/rules.h). Used for new games; a saved game keeps its own.
    bool originalMode = false;
    // Ours: with the original game's files, draw its maps and characters
    // with the open content's art (F12 swaps while playing).
    bool ourArt = false;
    // Ours, on the Cheats tab: hero experience is multiplied by this, in
    // percent. Original mode always plays at 100.
    int xpRate = kXpRateNormal;
    // Ours: the world's zoom, kMinZoom-kMaxZoom percent (the scroll wheel).
    int zoom = kMaxZoom;

    // Half the side of the square a click reaches, in screen pixels.
    int clickReach() const;
    // Moves a kind of target to the front of the click priority.
    void putFirst(int target);
    // Where a kind of target is in the click priority, 0 = first.
    int rank(int target) const;
    // The highest of kXpRates no more than `percent` (the lowest below it).
    static int clampXpRate(int percent);

    std::string write() const;
    // Unknown keys and bad values are skipped, keeping the defaults, so an
    // old or hand-edited file never stops the game from starting.
    static Settings parse(std::string_view text);
};

}  // namespace sf

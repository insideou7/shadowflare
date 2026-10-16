#pragma once

#include <string>
#include <utility>
#include <vector>

namespace sf {

// What the rules tell the player as they run: sound effects and messages
// on screen. The rules only queue them here; the game takes them after
// each tick or click and plays and shows them (Game::deliverEvents), so
// the rules call no audio or drawing code and the online server can send
// each player the events they should hear and see (docs/online.md).
class WorldEvents {
public:
    struct Sound {
        int voice = -1;
        // Where it happens, in world units, unless heard everywhere (the
        // menus' clicks, and the few effects the original plays at full
        // volume wherever the hero is).
        double x = 0, y = 0;
        bool placed = false;
        bool everywhere = false;
        bool operator==(const Sound&) const = default;
    };

    // A sound where something happens; the listener only hears it nearby
    // unless `everywhere`.
    void sound(int voice, double x, double y, bool everywhere = false) {
        if (voice >= 0) sounds_.push_back({voice, x, y, true, everywhere});
    }
    // A sound heard wherever the listener is.
    void sound(int voice) {
        if (voice >= 0) sounds_.push_back({voice, 0, 0, false, true});
    }
    void message(std::string text) { messages_.push_back(std::move(text)); }

    bool empty() const { return sounds_.empty() && messages_.empty(); }
    // The events queued since the last take, oldest first.
    std::vector<Sound> takeSounds() { return std::exchange(sounds_, {}); }
    std::vector<std::string> takeMessages() { return std::exchange(messages_, {}); }

private:
    std::vector<Sound> sounds_;
    std::vector<std::string> messages_;
};

}  // namespace sf

#pragma once

#include <SDL.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "game/character.h"
#include "formats/scenario.h"
#include "game/map.h"
#include "render/sprite_cache.h"

namespace sf {

class FileSystem;

// The original runs at 640x480; we render to that and let SDL scale it.
inline constexpr int kScreenWidth = 640;
inline constexpr int kScreenHeight = 480;

// Simulation runs at a fixed rate, separate from rendering, so game logic
// can later be matched tick-for-tick against the original executable.
inline constexpr int kTicksPerSecond = 20;

// For now the game is a map viewer with a walking player: it loads a level
// from the original data and lets you walk around it.
class Game {
public:
    Game(SDL_Renderer* renderer, const FileSystem* data, const std::string& startMap);

    void handleEvent(const SDL_Event& event);
    void tick();
    void render(double interpolation);

    bool wantsQuit() const { return quit_; }

    // Starts walking towards a point given relative to the player on screen
    // (used for scripted smoke tests).
    void walkBy(int screenDx, int screenDy);

    // Puts the player on the walkable spot nearest a map pixel.
    void placePlayerAt(double screenX, double screenY);

    // Makes the player attack the nearest monster on their own (for
    // scripted smoke tests of combat).
    void setAutoFight(bool on) { autoFight_ = on; }

    // Draws the walkability grid over the map (toggled with J).
    void setShowJudge(bool show) { showJudge_ = show; }
    std::uint64_t tickCount() const { return ticks_; }

private:
    // People and monsters placed by the map's scenario file.
    struct Actor {
        enum class State { Idle, Chase, Attack, Hit, Dead };
        ScenarioControl::Placement placement;
        Character character;
        State state = State::Idle;
        int hp = 0, maxHp = 0;
        bool hostile() const { return placement.group == ScenarioControl::Group::Enemy; }
        bool alive() const { return state != State::Dead; }
    };
    void loadMap(std::size_t index);
    void indexScenarios();
    void spawnActors();
    const CharacterModel* model(const std::string& stem);
    void placePlayer();
    void updateCamera();
    bool tryMove(Character& who, double dx, double dy);
    void tickPlayer();
    void tickEnemy(Actor& enemy);
    void hurtEnemy(Actor& enemy, int damage);
    void hurtPlayer(int damage);
    std::optional<std::size_t> enemyAt(float screenX, float screenY) const;
    void renderHealthBars(int camX, int camY);
    void renderMap();
    void renderJudge(int camX, int camY);
    void renderPlaceholder(double interpolation);

    SDL_Renderer* renderer_;
    const FileSystem* data_;
    SpriteCache sprites_;
    std::vector<std::string> mapNames_;
    std::size_t mapIndex_ = 0;
    std::optional<Map> map_;
    std::vector<std::size_t> drawOrder_;  // objects sorted back to front

    std::map<std::string, std::string> scenarioByMap_;  // lower-case map name -> Scenario/<dir>
    std::map<std::string, std::unique_ptr<CharacterModel>> models_;  // by path stem; null = failed
    std::vector<Actor> actors_;

    std::optional<CharacterModel> playerModel_;
    std::optional<Character> player_;
    std::optional<std::pair<double, double>> walkTarget_;  // world units
    std::optional<std::size_t> attackTarget_;              // index into actors_
    int playerHp_ = 0;
    bool mouseHeld_ = false;
    bool showJudge_ = false;
    bool autoFight_ = false;

    double cameraX_ = 0, cameraY_ = 0;  // top-left of the view, in map pixels
    std::uint64_t ticks_ = 0;
    bool quit_ = false;
};

}  // namespace sf

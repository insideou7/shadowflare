#pragma once

#include <SDL.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "formats/animation.h"
#include "formats/sprite_bank.h"

namespace sf {

class FileSystem;
class SpriteCache;

// Sprites and animation charts shared by every instance of a character type.
struct CharacterModel {
    AnimationSet animations;
    SpriteBank body;    // .njp
    SpriteBank shadow;  // .sdw

    // `stem` is the path without extension, e.g. "Player/Male/Animation00".
    static std::optional<CharacterModel> load(const FileSystem& data, const std::string& stem);
};

// Animation chart numbers. Idle and walk are the same for everyone; the
// rest differ between the player and monsters (worked out by rendering
// each chart; see docs/formats.md).
enum class Action { Idle = 0, Walk = 1 };

namespace chart {
inline constexpr int kIdle = 0;
inline constexpr int kWalk = 1;
inline constexpr int kPlayerRun = 2;
inline constexpr int kPlayerHit = 3;
inline constexpr int kPlayerPunch = 5;
inline constexpr int kPlayerHeal = 11;  // Heal's own casting (the spell's controller)
inline constexpr int kPlayerCast = 13;
inline constexpr int kEnemyHit = 2;
inline constexpr int kEnemyDeath = 3;
inline constexpr int kEnemyAttack = 4;
}  // namespace chart

// The direction (0-7) a world-space vector points on screen: 0 = south,
// then counter-clockwise on screen through east, north and west.
int facingOf(double dx, double dy);

// A character standing in the world: position, facing and animation state.
class Character {
public:
    explicit Character(const CharacterModel* model) : model_(model) {}
    void setModel(const CharacterModel* model) { model_ = model; }

    double x = 0, y = 0;  // world units
    int direction = 0;    // 0 = south, then counter-clockwise: south-east, east, ...

    // Which layers to draw besides the shadow. The player's follow what
    // they wear (Inventory::heroLayers).
    std::vector<int> visibleLayers = {0};
    // Colour and opacity the body is drawn with (partner dogs are recoloured
    // per kind, and fade out when they die).
    SDL_Color tint{255, 255, 255, 255};
    // Drawn without colour (turned to stone by Medusa).
    bool grey = false;
    // The animation stands still on its frame (stone).
    bool held = false;

    void setAction(Action action) { play(static_cast<int>(action)); }

    // Starts a chart from its first frame unless it is already playing.
    // Looping charts (idle, walk) wrap around; the others stop on their
    // last frame.
    void play(int chart);
    // `speed` is frames per tick (spells cast faster or slower).
    void restart(int chart, double speed = 1.0);
    int chart() const { return chart_; }
    int frame() const { return frame_; }
    int frameCount() const;
    bool finished() const;  // a non-looping chart reached its last frame
    bool hasChart(int chart) const;

    // Face towards a world-space movement vector.
    void faceTowards(double dx, double dy);

    void tick();
    // `shadowAlpha` is how dark the shadow is drawn. Below 255, `alpha`
    // draws a see-through copy with no shadow.
    void draw(SDL_Renderer* renderer, SpriteCache& sprites, int cameraX, int cameraY, int shadowAlpha = 96,
              int alpha = 255) const;

private:
    const AnimationSet::Direction* currentDirection() const;

    const CharacterModel* model_;
    int chart_ = chart::kIdle;
    int frame_ = 0;
    double speed_ = 1.0, progress_ = 0;
};

}  // namespace sf

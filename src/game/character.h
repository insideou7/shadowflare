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

// Action charts as used by every character file seen so far.
enum class Action { Idle = 0, Walk = 1 };

// A character standing in the world: position, facing and animation state.
class Character {
public:
    explicit Character(const CharacterModel* model) : model_(model) {}

    double x = 0, y = 0;  // world units
    int direction = 0;    // 0 = south, then counter-clockwise: south-east, east, ...

    // Which layers to draw besides the shadow; the player's equipment layers
    // stay hidden until items are decoded.
    std::vector<int> visibleLayers = {0};

    void setAction(Action action);
    Action action() const { return action_; }

    // Face towards a world-space movement vector.
    void faceTowards(double dx, double dy);

    void tick();
    void draw(SDL_Renderer* renderer, SpriteCache& sprites, int cameraX, int cameraY) const;

private:
    const AnimationSet::Direction* currentDirection() const;

    const CharacterModel* model_;
    Action action_ = Action::Idle;
    int frame_ = 0;
};

}  // namespace sf

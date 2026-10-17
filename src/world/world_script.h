#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "logic/script_runner.h"

namespace sf {

class World;

// What scenario scripts ask of the world itself: where characters stand,
// showing, stopping and turning them, their scripted actions, objects'
// states and opacity, bringing monsters back, rolls and sounds. What they
// ask of the hero and the screen (talks, shops, items, the party) is left
// to a class built on this one; Game's GameScript is that for now.
class WorldScript : public ScriptHost {
public:
    // `character` is whose event runs the script (an object, a person, or
    // -1 for none).
    WorldScript(World& world, std::int32_t character) : world_(world), character_(character) {}

    std::int32_t character() const { return character_; }
    // World::runScript sets it for each script it runs with this host.
    void setCharacter(std::int32_t character) { character_ = character; }

    std::int32_t characterX(std::int32_t id) const override;
    std::int32_t characterY(std::int32_t id) const override;
    void stopCharacter(std::int32_t id, bool stop) override;
    void requestAction(const ActionRequest& r) override;
    void faceCharacter(std::int32_t id, std::int32_t towards) override;
    void setActive(std::int32_t id, bool active) override;
    void setObjectStates(std::int32_t id, int shown, int usable, int blocks) override;
    void setUnlockSwitch(bool on) override;
    int random(int n) override;
    int entryPoint() const override;
    std::optional<bool> enemyAlive(std::int32_t id) const override;
    void skippedCommand(std::int32_t opcode) override;
    bool nearestPlayer(std::int32_t character, int min, int max, int& player, int& x, int& y) const override;
    int playerDistance(std::int32_t character) const override;
    void resurrect(std::int32_t enemy, int x, int y, int direction) override;
    void setObjectOpacity(std::int32_t object, int opacity) override;
    std::optional<int> objectOpacity(std::int32_t object) const override;
    void playSound(int voice, bool everywhere, int x, int y) override;

protected:
    World& world_;
    std::int32_t character_;
};

}  // namespace sf

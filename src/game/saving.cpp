// Saving and loading the game (F5 and F9).
#include <algorithm>
#include <fstream>
#include <sstream>

#include "core/log.h"
#include "formats/binary_reader.h"
#include "game/game.h"
#include "logic/save_game.h"

namespace sf {

bool Game::saveGame() {
    if (!map_ || !player_ || mapNames_.empty()) return false;
    // An item held in the panel goes back in the bag, or is lost with it.
    if (hand_ && inventory_.add(*hand_)) hand_.reset();

    SaveGame save;
    save.map = mapNames_[mapIndex_];
    save.scenario = scenarioDir_;
    save.x = static_cast<int>(player_->x);
    save.y = static_cast<int>(player_->y);
    save.direction = player_->direction;
    save.hp = playerHp_;
    save.level = playerLevel_;
    save.experience = playerExperience_;
    save.store(inventory_);
    for (const auto& [key, value] : flags_) {
        if (key.first != ScenarioScript::kTemp) save.flags[key] = value;
    }

    std::ofstream out(saveFile_, std::ios::binary | std::ios::trunc);
    out << save.write();
    if (!out.flush()) {
        log::warn("Could not write {}", saveFile_);
        showMessage("Could not save");
        return false;
    }
    log::info("Saved the game to {}", saveFile_);
    showMessage("Game saved");
    return true;
}

bool Game::loadGame() {
    std::ifstream in(saveFile_, std::ios::binary);
    if (!in) {
        showMessage("No saved game");
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    SaveGame save;
    try {
        save = SaveGame::parse(text.str());
    } catch (const FormatError& e) {
        log::warn("{}: {}", saveFile_, e.what());
        showMessage("The saved game is damaged");
        return false;
    }
    std::size_t index = mapNames_.size();
    for (std::size_t i = 0; i < mapNames_.size(); ++i) {
        if (mapNames_[i] == save.map) index = i;
    }
    if (index == mapNames_.size()) {
        log::warn("{}: map {} is not in this game's data", saveFile_, save.map);
        showMessage("The saved game is for other data");
        return false;
    }

    hand_.reset();
    flags_ = save.flags;
    loadMap(index, save.scenario);
    if (!map_ || !player_) return false;
    player_->x = save.x;
    player_->y = save.y;
    player_->direction = save.direction;
    walkTarget_.reset();
    attackTarget_.reset();
    playerExperience_ = save.experience;
    playerLevel_ = std::max(1, save.level);
    if (const int skipped = save.restore(inventory_, items_)) log::warn("{}: {} saved items are unknown", saveFile_, skipped);
    updateEquipment();
    playerHp_ = std::clamp(save.hp, 1, playerMaxHp());
    checkTriggers(false);  // loading inside an exit does not take it
    updateCamera();
    log::info("Loaded the game from {}", saveFile_);
    showMessage("Game loaded");
    return true;
}

}  // namespace sf

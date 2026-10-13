// Saving and loading the game (F10 and F9).
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
    save.mp = playerMp_;
    save.mines = mineCount_;
    save.spells = spells_;
    save.spellsOn = spellOn_;
    save.originalMode = originalMode();
    save.level = playerLevel_;
    save.experience = playerExperience_;
    save.career = career_;
    save.sex = heroSex_;
    save.name = heroName_;
    save.partner = partnerRecord_;
    save.partnerHp = !partner_ ? partnerHpCarried_ : partnerAlive() ? partner_->hp : 0;
    save.store(inventory_);
    if (const auto storage = shops_.find(kStorage); storage != shops_.end()) save.storeStorage(storage->second);
    for (const auto& [key, value] : flags_) {
        // TEMP and NET flags start again with every scenario.
        if (key.first != ScenarioScript::kTemp && key.first != ScenarioScript::kNet && key.first >= 0) save.flags[key] = value;
    }
    save.explored = explored_;

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
    explored_ = save.explored;
    // Saves from before companions: Kerberos at level 1, at rest.
    partnerRecord_ = save.partner.value_or(PartnerRecord{});
    partner_.reset();
    partnerHpCarried_ = save.partnerHp;
    setHero(save.sex, save.name);
    loadMap(index, save.scenario);
    if (!map_ || !player_) return false;
    player_->x = save.x;
    player_->y = save.y;
    player_->direction = save.direction;
    walkTarget_.reset();
    attackTarget_.reset();
    playerExperience_ = save.experience;
    playerLevel_ = std::max(1, save.level);
    // Saves from before classes: a warrior from level 5.
    career_ = save.career && save.career->history.size() == static_cast<std::size_t>(playerLevel_ - 1) ? *save.career
                                                                                                    : Career::at(playerLevel_);
    // Saves from before magic: the spells the hero would have learned.
    spells_ = save.spells ? *save.spells : SpellBook::replay(magic_, career_.history);
    power_ = {};  // the charge is not saved
    closeMagicWindow();
    closeWarpWindow();
    spellOn_ = save.spells ? save.spellsOn : std::array<bool, kSpellCount>{};
    for (int s = 0; s < kSpellCount; ++s) {
        if (!isToggleSpell(s) || !spells_.knows(s)) setSpellOn(s, false);
    }
    moonLevel_ = castLevel(kMoon);
    setOriginalMode(save.originalMode);
    casting_.reset();
    clearSpells();
    transportGate_.reset();  // the gate is not saved
    if (const int skipped = save.restore(inventory_, items_)) log::warn("{}: {} saved items are unknown", saveFile_, skipped);
    // Saves from before mines: a new hero's five, and any mines that went
    // into the bag then.
    mineCount_ = save.mines.value_or(mines::kStartCount);
    if (!save.mines) mineCount_ += inventory_.takeAll(ItemDef::Class::Special, 1);
    if (const int skipped = save.restoreStorage(shops_[kStorage], items_)) {
        log::warn("{}: {} stored items are unknown", saveFile_, skipped);
    }
    updateEquipment();
    // The companion came before the hero's things, which can raise its health.
    if (partnerAlive() && save.partnerHp > 0) partner_->hp = std::min(save.partnerHp, partner_->stats.maxHp);
    playerHp_ = std::clamp(save.hp, 1, playerMaxHp());
    playerMp_ = std::clamp(save.mp.value_or(playerMaxMp()), 0, playerMaxMp());
    checkTriggers(false);  // loading inside an exit does not take it
    updateCamera();
    log::info("Loaded the game from {}", saveFile_);
    showMessage("Game loaded");
    return true;
}

}  // namespace sf

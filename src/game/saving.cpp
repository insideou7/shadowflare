// Saving and loading the game (F10 and F11), in six save slots.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/log.h"
#include "formats/binary_reader.h"
#include "game/game.h"
#include "logic/open_areas.h"
#include "logic/save_game.h"
#include "platform/filesystem.h"

namespace sf {
namespace {

// The picture kept with each save, shown on the Load Data screen: 391 x 114
// pixels of the game, as the exe writes Save\%04d.Bmp (0x44e830). Which
// part of the screen it takes is inferred from the original's own saves:
// the hero stands in the middle, their feet two thirds of the way down.
constexpr int kPictureWidth = 391;
constexpr int kPictureHeight = 114;
constexpr int kPictureFeet = 80;

std::filesystem::path pictureOf(const std::string& saveFile) {
    return std::filesystem::path(saveFile).replace_extension(".bmp");
}

}  // namespace

void Game::setSaveFolder(std::string folder) {
    saveFolder_ = std::move(folder);
    saveFile_.clear();
}

std::string Game::saveFolder() const {
    if (!saveFolder_.empty()) return saveFolder_;
    return std::filesystem::path(saveFile_).parent_path().string();
}

std::string Game::slotFile(int number) const {
    return (std::filesystem::path(saveFolder_) / ("save" + std::to_string(number + 1) + ".txt")).string();
}

std::vector<int> Game::savedSlots() const {
    std::vector<int> used;
    if (saveFolder_.empty()) return used;
    std::error_code ec;
    for (int n = 0; n < kSaveSlots; ++n) {
        if (std::filesystem::exists(slotFile(n), ec)) used.push_back(n);
    }
    return used;
}

// A new hero takes the first number not in use (0x4021b0); with all six in
// use there is no New Game.
std::optional<int> Game::freeSlot() const {
    if (saveFolder_.empty()) return std::nullopt;
    std::error_code ec;
    for (int n = 0; n < kSaveSlots; ++n) {
        if (!std::filesystem::exists(slotFile(n), ec)) return n;
    }
    return std::nullopt;
}

// Before there were slots the one save was save.txt in the same folder; it
// moves into the first free slot so it can still be loaded.
void Game::adoptOldSave() {
    if (saveFolder_.empty()) return;
    std::error_code ec;
    const auto old = std::filesystem::path(saveFolder_) / "save.txt";
    if (!std::filesystem::exists(old, ec)) return;
    const auto slot = freeSlot();
    if (!slot) return;
    std::filesystem::rename(old, slotFile(*slot), ec);
    if (ec) {
        log::warn("Could not move {} into a save slot: {}", old.string(), ec.message());
    } else {
        log::info("Moved the old save {} to {}", old.string(), slotFile(*slot));
    }
}

void Game::saveThumbnail(const std::string& path) {
    if (!map_ || !player_) return;
    SDL_Texture* target = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
                                            kScreenWidth, kScreenHeight);
    if (!target) return;
    SDL_Texture* was = SDL_GetRenderTarget(renderer_);
    // The world alone, without the panel or any window, at full size
    // whatever the zoom.
    const double cameraX = cameraX_, cameraY = cameraY_;
    fullSize_ = true;
    updateCamera();
    if (SDL_SetRenderTarget(renderer_, target) == 0) {
        worldOnly_ = true;
        renderMap();
        worldOnly_ = false;
        int sx, sy;
        worldToScreen(static_cast<int>(player_->x), static_cast<int>(player_->y), sx, sy);
        sx -= static_cast<int>(std::floor(cameraX_));
        sy -= static_cast<int>(std::floor(cameraY_));
        const SDL_Rect area{std::clamp(sx - kPictureWidth / 2, 0, kScreenWidth - kPictureWidth),
                            std::clamp(sy - kPictureFeet, 0, kScreenHeight - kPictureHeight), kPictureWidth,
                            kPictureHeight};
        if (SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, kPictureWidth, kPictureHeight, 24, SDL_PIXELFORMAT_RGB24)) {
            if (SDL_RenderReadPixels(renderer_, &area, SDL_PIXELFORMAT_RGB24, s->pixels, s->pitch) != 0 ||
                SDL_SaveBMP(s, path.c_str()) != 0) {
                log::warn("Could not keep a picture with the save: {}", SDL_GetError());
            }
            SDL_FreeSurface(s);
        }
    }
    fullSize_ = false;
    cameraX_ = cameraX;
    cameraY_ = cameraY;
    SDL_SetRenderTarget(renderer_, was);
    SDL_DestroyTexture(target);
}

bool Game::saveGame() {
    if (!map_ || !player_ || mapNames_.empty()) return false;
    if (saveFile_.empty()) {
        const auto slot = freeSlot();
        if (!slot) {
            log::warn("All six save slots in {} are in use", saveFolder_);
            showMessage("All six save slots are in use");
            return false;
        }
        saveFile_ = slotFile(*slot);
    }
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
    out.close();
    log::info("Saved the game to {}", saveFile_);
    saveThumbnail(pictureOf(saveFile_).string());
    showMessage("Game saved");
    return true;
}

bool Game::loadGame() {
    if (saveFile_.empty()) {
        // No hero chosen yet: the save written last.
        adoptOldSave();
        std::error_code ec;
        std::filesystem::file_time_type newest{};
        for (const int n : savedSlots()) {
            const auto when = std::filesystem::last_write_time(slotFile(n), ec);
            if (saveFile_.empty() || when > newest) {
                saveFile_ = slotFile(n);
                newest = when;
            }
        }
        if (saveFile_.empty()) {
            showMessage("No saved game");
            return false;
        }
    }
    return loadGameFrom(saveFile_);
}

// The map's name alone is not enough: the open pack's look-alike maps take
// the original's names (f00_01 is Remote Town in both), but its scenarios
// are numbered apart (Scenario/10000000 against Scenario/00000000), and a
// hero put down where the other data's map has nothing stands in the dark.
bool Game::saveFitsData(const std::string& map, const std::string& scenario) const {
    if (std::ranges::find(mapNames_, map) == mapNames_.end()) return false;
    return scenario.empty() || data_->exists(scenario + "/scenario.txt") || data_->exists(scenario + "/Scenario.Mct");
}

// The two share the first area (Remote Town, Cold Svalt and round them),
// so a hero saved there with either data goes on with the other.
bool Game::fitSave(SaveGame& save) const {
    if (saveFitsData(save.map, save.scenario)) return true;
    SaveGame moved = save;
    if (!moveSaveToOtherData(moved) || !saveFitsData(moved.map, moved.scenario)) return false;
    save = std::move(moved);
    return true;
}

bool Game::loadGameFrom(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
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
        log::warn("{}: {}", path, e.what());
        showMessage("The saved game is damaged");
        return false;
    }
    const std::string savedIn = save.scenario;
    if (!fitSave(save)) {
        log::warn("{}: {} ({}) is not in this game's data", path, save.scenario, save.map);
        showMessage("The saved game is for other data");
        return false;
    }
    if (save.scenario != savedIn) log::info("{}: saved in {}, the same place here is {}", path, savedIn, save.scenario);
    const auto index = static_cast<std::size_t>(std::ranges::find(mapNames_, save.map) - mapNames_.begin());

    forgetVisitedScenarios();
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
    if (const int skipped = save.restore(inventory_, items_)) log::warn("{}: {} saved items are unknown", path, skipped);
    // Saves from before mines start with a new hero's five. Mines that went
    // into the bag (picked up before mines, or bought before buying counted
    // them) join the hero's mines while there is room.
    mineCount_ = save.mines.value_or(mines::kStartCount);
    while (mineCount_ < mostMines() && inventory_.removeOne(ItemDef::Class::Special, 1)) ++mineCount_;
    if (const int skipped = save.restoreStorage(shops_[kStorage], items_)) {
        log::warn("{}: {} stored items are unknown", path, skipped);
    }
    updateEquipment();
    // The companion came before the hero's things, which can raise its health.
    if (partnerAlive() && save.partnerHp > 0) partner_->hp = std::min(save.partnerHp, partner_->stats.maxHp);
    playerHp_ = std::clamp(save.hp, 1, playerMaxHp());
    playerMp_ = std::clamp(save.mp.value_or(playerMaxMp()), 0, playerMaxMp());
    checkTriggers(false);  // loading inside an exit does not take it
    updateCamera();
    saveFile_ = path;  // saving goes back to the same slot
    log::info("Loaded the game from {}", path);
    showMessage("Game loaded");
    return true;
}

}  // namespace sf

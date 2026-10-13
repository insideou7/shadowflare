#include "game/game.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <format>

#include "core/log.h"
#include "formats/binary_reader.h"
#include "formats/open_content.h"
#include "game/skin.h"
#include "logic/movement.h"
#include "render/tiny_font.h"
#include "platform/filesystem.h"

namespace sf {

namespace {

// Walking speed in world units per tick. Not yet taken from the original;
// nor is running, which is ours at twice the walk.
constexpr double kWalkSpeed = kPlayerWalkSpeed;
constexpr double kRunSpeed = 140.0;

// Monsters take their health, attack and defense from the scenario file.
// Blows follow the original's rules (see strikeEnemy and strikePlayer).
constexpr double kEnemySpeed = 35.0;
constexpr double kMeleeRange = 220.0;      // world units between the two characters' feet
constexpr double kTalkRange = 320.0;      // close enough to speak to someone
constexpr double kAggroRange = 1600.0;     // monsters notice the player within this distance
constexpr double kGiveUpRange = 3200.0;    // and stop chasing beyond this one

double distance(const Character& a, const Character& b) { return std::hypot(a.x - b.x, a.y - b.y); }

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string stem(const std::string& file) {
    const auto dot = file.rfind('.');
    return dot == std::string::npos ? file : file.substr(0, dot);
}

bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

}  // namespace

Game::Game(SDL_Renderer* renderer, const FileSystem* data, const std::string& startMap, SkinLoader skinLoader)
    : renderer_(renderer), data_(data), skinLoader_(std::move(skinLoader)), sprites_(renderer) {
    // Every level has a ground file; use those to enumerate maps. An open
    // content pack has one text file per map instead.
    for (const auto& file : data_->list("Map/Ground")) mapNames_.push_back(stem(file));
    if (mapNames_.empty()) {
        for (const auto& file : data_->list("Map")) {
            if (lower(file).ends_with(".txt")) mapNames_.push_back(stem(file));
        }
    }
    try {
        if (auto raw = data_->read("System/Game/Parameter/Control.aid")) {
            ai_ = AiControl::parse(*raw);
        } else if (auto text = data_->read("ai.txt")) {
            ai_ = open::parseAi(*text);
        }
    } catch (const FormatError& e) {
        log::warn("Monster AI: {}", e.what());
    }
    loadItems();
    loadLevelCurve();
    loadMissions();
    loadWarpGates();
    loadSounds();
    indexScenarios();
    if (auto& hero = playerModels()[static_cast<int>(HeroSex::Male)]) player_.emplace(&*hero);
    giveStartingKit();
    updateEquipment();
    if (mapNames_.empty()) {
        log::warn("No maps found in the game data; showing a placeholder screen");
        return;
    }
    // The start is a scenario number (the original begins every new game
    // in scenario 0, Remote Town, at entry 0: 0x440f70 clears the saved
    // scenario and entry) or a map name.
    if (!startMap.empty() && startMap.size() <= 9 && std::ranges::all_of(startMap, [](unsigned char c) { return std::isdigit(c); })) {
        startScenario_ = std::stoi(startMap);
    } else {
        for (std::size_t i = 0; i < mapNames_.size(); ++i) {
            if (equalsIgnoreCase(mapNames_[i], startMap)) startMapIndex_ = i;
        }
    }
    goToStart();
}

// Where a new game begins.
void Game::goToStart() {
    map_.reset();
    if (startScenario_ >= 0) changeScenario(startScenario_, 0);
    if (!map_) loadMap(startMapIndex_);
}

void Game::loadMap(std::size_t index, const std::string& scenarioDir) {
    // The companion keeps its health from map to map (0x4505d0).
    if (partner_) partnerHpCarried_ = partner_->state == Partner::State::Dead ? 0 : partner_->hp;
    partner_.reset();
    clearSpells();
    mapIndex_ = index;
    sprites_.clear();
    map_ = Map::load(*data_, mapNames_[index]);
    if (skin_) skin_->setMap(map_ ? &*map_ : nullptr);
    drawOrder_.clear();
    if (!map_) return;

    const auto& objects = map_->objects().objects;
    drawOrder_.resize(objects.size());
    for (std::size_t i = 0; i < objects.size(); ++i) drawOrder_[i] = i;
    // Painter's order by where each object stands, not where its sprite's
    // corner is: things nearer the bottom of the screen are drawn later.
    std::ranges::stable_sort(drawOrder_, [&](std::size_t a, std::size_t b) { return objects[a].depth() < objects[b].depth(); });
    if (!scenarioDir.empty()) {
        loadScenario(scenarioDir);
    } else {
        const auto found = scenarioByMap_.find(lower(mapNames_[mapIndex_]));
        loadScenario(found == scenarioByMap_.end() ? std::string() : found->second);
    }
}

void Game::loadScenario(const std::string& dir) {
    scenarioDir_ = dir;
    skippedCommands_.clear();
    control_ = {};
    script_.reset();
    triggers_.clear();
    pendingScenario_.reset();
    // TEMP flags last for one visit, and out of network play so do NET
    // ones (0x4300e0); the others persist.
    std::erase_if(flags_, [](const auto& f) { return f.first.first == ScenarioScript::kTemp || f.first.first == ScenarioScript::kNet; });
    warpPage_ = 0;
    if (!dir.empty()) {
        readScenario(dir, control_, &script_);
        startAreaMusic(control_.areaKind);
    }
    // The NET flags start as the script lists them, then each character's
    // and object's activity flags join them.
    if (script_) {
        for (const auto& [id, value] : script_->netFlags) flags_[{ScenarioScript::kNet, id}] = value;
    }
    for (const auto& p : control_.placements) {
        for (std::size_t k = 0; k < kStateFlagBases.size(); ++k) {
            flags_[{ScenarioScript::kNet, kStateFlagBases[k] + ScenarioControl::idBase(p.group) + p.id}] = p.states[k];
        }
    }
    spawnActors();
    spawnObjects();
    spawnGroundItems();
    placePlayer();
    dialogue_.reset();
    openShop_.reset();
    talkTarget_.reset();
    objectTarget_.reset();
    labels_.clear();
    // The original's scenario scripts name their area with SETCOMPASSES;
    // an open content pack's scenario names it with its title.
    if (!dir.empty() && data_->read(dir + "/scenario.txt") && !control_.title.empty()) showAreaName(control_.title);
    if (!script_) return;
    for (const auto& [id, value] : script_->tempFlags) flags_[{ScenarioScript::kTemp, id}] = value;
    startScripts_ = true;
    for (const auto& t : script_->triggers) {
        if (t.event != ScenarioScript::kOnJudge) continue;
        for (const auto& p : control_.placements) {
            if (ScenarioControl::idBase(p.group) + p.id != t.character) continue;
            triggers_.push_back({static_cast<double>(p.x + p.rect[0]), static_cast<double>(p.y + p.rect[1]),
                                 static_cast<double>(p.x + p.rect[2]), static_cast<double>(p.y + p.rect[3]), t.sentence});
        }
    }
    checkTriggers(false);  // arriving inside an area does not set it off
}

// Several scenarios can share a map (the same place at different points in
// the story); until story progress exists, use the first one.
void Game::indexScenarios() {
    for (const auto& dir : data_->list("Scenario")) {
        ScenarioControl control;
        if (!readScenario("Scenario/" + dir, control, nullptr)) continue;
        std::string map = control.mapPath;
        const auto slash = map.find_last_of("\\/");
        if (slash != std::string::npos) map = map.substr(slash + 1);
        scenarioByMap_.emplace(lower(stem(map)), "Scenario/" + dir);
    }
    log::info("Indexed {} scenario maps", scenarioByMap_.size());
}

// Scenario.Mct and Scenario.Scs from the original, or scenario.txt from an
// open content pack. False when the scenario is missing or broken.
bool Game::readScenario(const std::string& dir, ScenarioControl& control, std::optional<ScenarioScript>* script) const {
    try {
        if (auto text = data_->read(dir + "/scenario.txt")) {
            auto parsed = open::parseScenario(*text);
            if (auto events = data_->read(dir + "/script.txt")) open::parseScript(*events, parsed);
            control = std::move(parsed.control);
            if (script) *script = std::move(parsed.script);
            return true;
        }
        auto raw = data_->read(dir + "/Scenario.Mct");
        if (!raw) return false;
        control = ScenarioControl::parse(*raw);
        if (script) {
            if (auto scs = data_->read(dir + "/Scenario.Scs")) *script = ScenarioScript::parse(*scs);
        }
        return true;
    } catch (const FormatError& e) {
        log::warn("{}: {}", dir, e.what());
        return false;
    }
}

Game::~Game() = default;

std::array<std::optional<CharacterModel>, 2>& Game::playerModels() {
    const int art = skin_ ? 1 : 0;
    auto& models = playerModels_[art];
    if (!playerModelsLoaded_[art]) {
        playerModelsLoaded_[art] = true;
        // Both of the player's figures (0x412806 picks by the hero's sex); a
        // pack without a heroine draws her as the hero.
        for (const HeroSex sex : {HeroSex::Male, HeroSex::Female}) {
            const std::string stem = sex == HeroSex::Male ? "Player/Male/Animation00" : "Player/Female/Animation00";
            auto& model = models[static_cast<int>(sex)];
            if (skin_) model = skin_->character(stem);
            const bool there = data_->exists(stem + ".anim") || data_->exists(stem + ".caf");
            if (!model && (sex == HeroSex::Male || there)) model = CharacterModel::load(*data_, stem);
        }
    }
    return models;
}

void Game::setOurArt(bool on) {
    if (on == ourArt()) return;
    if (on && !ownedSkin_) {
        if (!skinLoader_) return;
        ownedSkin_ = skinLoader_();
        skinLoader_ = nullptr;
        if (!ownedSkin_) {
            showMessage("No art of ours to draw the game with");
            return;
        }
    }
    // Everyone on the map keeps what they are doing and only changes
    // pictures: each model in use is found again by its stem in the other
    // art's set.
    std::map<const CharacterModel*, std::string> stems;
    for (const auto& [stem, m] : models_[skin_ ? 1 : 0]) {
        if (m) stems.emplace(m.get(), stem);
    }
    skin_ = on ? ownedSkin_.get() : nullptr;
    auto swap = [&](Character& c) {
        const auto found = stems.find(c.model());
        if (found == stems.end()) return;
        if (const CharacterModel* m = model(found->second)) c.setModel(m);
    };
    for (auto& a : actors_) swap(a.character);
    for (auto& o : sceneObjects_) {
        if (o.animation) swap(*o.animation);
    }
    for (auto& g : groundItems_) {
        if (g.sprite) swap(*g.sprite);
    }
    if (partner_) swap(partner_->character);
    if (player_) {
        const auto& heroes = playerModels();
        const auto& hero = heroes[static_cast<int>(heroSex_)];
        const auto& fallback = heroes[static_cast<int>(HeroSex::Male)];
        if (const CharacterModel* m = hero ? &*hero : fallback ? &*fallback : nullptr) player_->setModel(m);
        updateEquipment();
    }
    sprites_.clear();
    if (skin_) skin_->setMap(map_ ? &*map_ : nullptr);
    // The area map's picture is drawn again in the new art.
    mapPicture_.reset();
    mapPictureFor_.clear();
    scenarioPictureFor_.clear();
    log::info("Drawing {} art", on ? "our" : "the original");
}

const CharacterModel* Game::model(const std::string& stem) {
    auto [it, inserted] = models_[skin_ ? 1 : 0].try_emplace(stem);
    if (inserted) {
        auto loaded = skin_ ? skin_->character(stem) : std::nullopt;
        if (!loaded) loaded = CharacterModel::load(*data_, stem);
        if (loaded) it->second = std::make_unique<CharacterModel>(std::move(*loaded));
    }
    return it->second.get();
}

void Game::spawnActors() {
    actors_.clear();
    attackTarget_.reset();
    if (scenarioDir_.empty()) return;
    for (const auto& p : control_.placements) {
        // Map objects (chests, doors) and items come later.
        if (p.kind < 0 || p.group == ScenarioControl::Group::Object || p.group == ScenarioControl::Group::Item) continue;
        const char* folder = p.group == ScenarioControl::Group::Enemy ? "ENEMY" : "PEOPLE";
        int kind = p.kind;
        if (p.group == ScenarioControl::Group::Person && kind >= ScenarioControl::kPartnerBase) {
            folder = "PARTNER";
            kind -= ScenarioControl::kPartnerBase;
        }
        char stemBuf[64];
        std::snprintf(stemBuf, sizeof stemBuf, "Character/%s/%08d/Animation", folder, kind);
        const CharacterModel* m = model(stemBuf);
        if (!m) continue;
        Actor actor{p, Character(m)};
        actor.active = p.startsActive();
        actor.character.x = p.x;
        actor.character.y = p.y;
        actor.character.direction = p.direction >= 0 && p.direction < 8 ? p.direction : 0;
        actor.hp = actor.maxHp = std::max(1, p.stats.hp);
        if (ai_ && actor.hostile()) actor.aiList = ai_->find(p.stats.ai);
        nameOpenEnemy(actor);
        // Non-player characters wear what their animation file shows.
        actor.character.visibleLayers.clear();
        if (!m->animations.charts.empty() && !m->animations.charts[0].directions.empty()) {
            for (std::size_t i = 0; i < m->animations.charts[0].directions[0].layers.size(); ++i)
                actor.character.visibleLayers.push_back(static_cast<int>(i));
        }
        actors_.push_back(std::move(actor));
    }
    const auto withAi = std::ranges::count_if(actors_, [](const Actor& a) { return a.aiList >= 0; });
    log::info("{}: {} \"{}\" with {} characters, {} with game AI", mapNames_[mapIndex_], scenarioDir_, control_.title,
              actors_.size(), withAi);
}

// Puts the player at the scenario's first entry point, or else in the
// middle of the map's scenery, and points the camera at them.
void Game::placePlayer() {
    for (const auto& e : control_.entryPoints) {
        if (e.id != 0) continue;
        int sx, sy;
        worldToScreen(e.x, e.y, sx, sy);
        placePlayerAt(sx, sy);
        return;
    }
    const auto& objects = map_->objects().objects;
    const auto& g = map_->ground();
    int left, top, right, bottom;
    g.screenBounds(left, top, right, bottom);
    double cx = (left + right) / 2.0, cy = (top + bottom) / 2.0;
    if (!objects.empty()) {
        int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
        for (const auto& o : objects) {
            int sx, sy;
            worldToScreen(o.x, o.y, sx, sy);
            minX = std::min(minX, sx);
            maxX = std::max(maxX, sx);
            minY = std::min(minY, sy);
            maxY = std::max(maxY, sy);
        }
        cx = (minX + maxX) / 2.0;
        cy = (minY + maxY) / 2.0;
    }
    placePlayerAt(cx, cy);
}

void Game::placePlayerAt(double screenX, double screenY) {
    heroStun_.reset();
    const auto& g = map_->ground();
    // A few maps have stray objects far outside the ground; stay on the map.
    int left, top, right, bottom;
    g.screenBounds(left, top, right, bottom);
    const double cx = std::clamp(screenX, static_cast<double>(left), static_cast<double>(right));
    const double cy = std::clamp(screenY, static_cast<double>(top), static_cast<double>(bottom));
    walkTarget_.reset();
    attackTarget_.reset();
    double wx, wy;
    screenToWorld(cx, cy, wx, wy);

    // Move to the walkable cell nearest that point, searching outwards ring by ring.
    if (!g.walkable(wx, wy)) {
        const int jx = static_cast<int>(std::floor(wx / g.magX));
        const int jy = static_cast<int>(std::floor(wy / g.magY));
        const int maxRadius = std::max(g.judgeWidth, g.judgeHeight);
        bool found = false;
        for (int r = 1; r < maxRadius && !found; ++r) {
            for (int dy = -r; dy <= r && !found; ++dy) {
                for (int dx = -r; dx <= r && !found; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    const double tx = (jx + dx + 0.5) * g.magX, ty = (jy + dy + 0.5) * g.magY;
                    if (g.walkable(tx, ty)) {
                        wx = tx;
                        wy = ty;
                        found = true;
                    }
                }
            }
        }
    }
    if (player_) {
        player_->x = wx;
        player_->y = wy;
        player_->setAction(Action::Idle);
        playerHp_ = playerMaxHp();
        playerMp_ = playerMaxMp();
        casting_.reset();
        spawnPartner();
        updateCamera();
        log::info("Player starts at world ({:.0f}, {:.0f})", wx, wy);
    } else {
        cameraX_ = cx - viewWidth() / 2.0;
        cameraY_ = cy - viewHeight() / 2.0;
    }
}

bool Game::tryMove(Character& who, double dx, double dy) {
    const double x0 = who.x, y0 = who.y;
    if (!sf::tryMove(map_->ground(), who.x, who.y, dx, dy)) return false;
    if (!objectInTheWay(x0, y0, who.x, who.y)) return true;
    // A closed door or a chest in the way: slide along it as along a wall.
    for (const auto& [sx, sy] : {std::pair{dx, 0.0}, std::pair{0.0, dy}}) {
        if (sx == 0 && sy == 0) continue;
        double x = x0, y = y0;
        if (sf::tryMove(map_->ground(), x, y, sx, sy) && !objectInTheWay(x0, y0, x, y)) {
            who.x = x;
            who.y = y;
            return true;
        }
    }
    who.x = x0;
    who.y = y0;
    return false;
}

void Game::walkBy(int screenDx, int screenDy) {
    if (!player_) return;
    double dx, dy;
    screenToWorld(screenDx, screenDy, dx, dy);
    walkTarget_.emplace(player_->x + dx, player_->y + dy);
}

// The hero stays in the middle of what the windows leave of the screen
// (0x41e8f0, with the windows counted at 0x416fb0): with the item window
// open on the right, the middle of the left half; with a window open on
// the left (status, magic, GATE, map, a shop), the middle of the right
// half. When both halves are covered the view stays where it is centred.
void Game::updateCamera() {
    if (!player_) return;
    int sx, sy;
    worldToScreen(static_cast<int>(player_->x), static_cast<int>(player_->y), sx, sy);
    const bool left = showStatus_ || magicOpen_ || warpOpen_ || mapOpen_ || openShop_.has_value();
    const bool right = showInventory_;
    const double w = viewWidth();
    double centre = w / 2.0;
    if (!missionsOpen_ && left != right) centre = right ? w / 4.0 : w * 3 / 4.0;
    cameraX_ = sx - centre;
    cameraY_ = sy - viewHeight() / 2.0;
}

double Game::zoom() const {
    if (fullSize_ || !rules_.zoomOut) return 1.0;
    return std::clamp(settings_.zoom, Settings::kMinZoom, Settings::kMaxZoom) / 100.0;
}

int Game::viewWidth() const { return static_cast<int>(std::ceil(kScreenWidth / zoom())); }
int Game::viewHeight() const { return static_cast<int>(std::ceil(kScreenHeight / zoom())); }

SDL_FPoint Game::onScreen(double px, double py) const {
    const double z = zoom();
    return {static_cast<float>((px - std::floor(cameraX_)) * z), static_cast<float>((py - std::floor(cameraY_)) * z)};
}

SDL_Texture* Game::worldTexture() {
    const int w = viewWidth(), h = viewHeight();
    if (!worldTexture_ || worldTextureW_ != w || worldTextureH_ != h) {
        worldTexture_.reset(SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h));
        if (!worldTexture_) {
            log::warn("Zoomed view: {}", SDL_GetError());
            return nullptr;
        }
        // Shrunk smoothly, not by dropping pixels.
        SDL_SetTextureScaleMode(worldTexture_.get(), SDL_ScaleModeLinear);
        worldTextureW_ = w;
        worldTextureH_ = h;
    }
    return worldTexture_.get();
}

// Each notch of the scroll wheel zooms a step, about the middle of the
// view (the hero, when there is one).
void Game::zoomBy(int notches) {
    if (!rules_.zoomOut || notches == 0) return;
    const int was = settings_.zoom;
    const double middleX = cameraX_ + viewWidth() / 2.0, middleY = cameraY_ + viewHeight() / 2.0;
    settings_.zoom = std::clamp(was + notches * Settings::kZoomStep, Settings::kMinZoom, Settings::kMaxZoom);
    if (settings_.zoom == was) return;
    cameraX_ = middleX - viewWidth() / 2.0;
    cameraY_ = middleY - viewHeight() / 2.0;
    updateCamera();
    saveSettings();
}

// The keys follow the manual's Shortcut Keys page (see docs/controls.md).
void Game::handleEvent(const SDL_Event& event) {
    if (optionsOpen_) {
        optionsEvent(event);
        return;
    }
    if (newCharacter_) {
        newCharacterEvent(event);
        return;
    }
    if (loadData_) {
        loadDataEvent(event);
        return;
    }
    if (menuOpen_) {
        menuEvent(event);
        return;
    }
    if (visual_ && visualEvent(event)) return;
    if (dialogue_ && event.type != SDL_QUIT) {
        dialogueEvent(event);
        return;
    }
    // The open map takes the arrow keys and Enter, held keys repeating.
    if (event.type == SDL_KEYDOWN && mapOpen_ && areaMapKey(event.key.keysym.sym)) return;
    switch (event.type) {
        case SDL_QUIT: quit_ = true; break;
        case SDL_MOUSEWHEEL: {
            // Away from the player zooms in, towards them out (ours; the
            // original has nothing on the wheel).
            const int notches = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
            if (map_ && rules_.zoomOut && notches != 0) {
                zoomBy(notches);
                showMessage(std::format("Zoom {}%", settings_.zoom));
            }
            break;
        }
        case SDL_KEYDOWN:
            if (event.key.repeat) break;
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    // Esc closes a window, or opens the Settings Menu when
                    // none is open.
                    if (helpOpen_) {
                        helpOpen_ = false;
                    } else if (warpOpen_) {
                        closeWarpWindow();
                    } else if (missionsOpen_) {
                        closeMissions();
                    } else if (mapOpen_) {
                        closeAreaMap();
                    } else if (openShop_) {
                        if (!hand_) closeShop();
                    } else if (showInventory_) {
                        closeInventory();
                    } else if (showStatus_) {
                        showStatus_ = false;
                    } else if (magicOpen_) {
                        closeMagicWindow();
                    } else {
                        openOptions(true);
                    }
                    break;
                // Map switching for testing; N and P are the manual's map
                // and power keys.
                case SDLK_PAGEDOWN:
                    if (!mapNames_.empty()) loadMap((mapIndex_ + 1) % mapNames_.size());
                    break;
                case SDLK_PAGEUP:
                    if (!mapNames_.empty()) loadMap((mapIndex_ + mapNames_.size() - 1) % mapNames_.size());
                    break;
                case SDLK_j: showJudge_ = !showJudge_; break;
                case SDLK_s:
                    if (!openShop_) {
                        showStatus_ = !showStatus_;
                        if (showStatus_) {
                            closeMissions();
                            closeMagicWindow();
                            closeAreaMap();
                            closeWarpWindow();
                        }
                    }
                    break;
                case SDLK_i:
                    closeMissions();
                    if (openShop_) {
                        closeShop();
                    } else if (showInventory_) {
                        closeInventory();
                    } else {
                        showInventory_ = true;
                        closeWarpWindow();
                    }
                    break;
                case SDLK_h: helpOpen_ = !helpOpen_; break;
                case SDLK_r:
                    runMode_ = !runMode_;
                    showMessage(runMode_ ? "Run" : "Walk");
                    break;
                case SDLK_F1: case SDLK_F2: case SDLK_F3: case SDLK_F4:
                case SDLK_F5: case SDLK_F6: case SDLK_F7: case SDLK_F8:
                    selectSpellSlot(event.key.keysym.sym - SDLK_F1);
                    break;
                case SDLK_F9:
                    // The right button attacks instead of casting.
                    spells_.selected = -1;
                    showMessage("Attack");
                    break;
                case SDLK_F10: saveGame(); break;
                case SDLK_F11: loadGame(); break;
                case SDLK_F12:
                    if (canSwapArt()) {
                        settings_.ourArt = !ourArt();
                        setOurArt(settings_.ourArt);
                        showMessage(ourArt() ? "Our art" : "Original art");
                        saveSettings();
                    }
                    break;
                case SDLK_PRINTSCREEN: screenshotPending_ = true; break;
                case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4:
                case SDLK_5: case SDLK_6: case SDLK_7: case SDLK_8: {
                    const int key = event.key.keysym.sym - SDLK_1;
                    useBelt(key % Inventory::kBeltWidth, key / Inventory::kBeltWidth);
                    break;
                }
                case SDLK_b: layMine(); break;
                case SDLK_p: startPower(); break;
                case SDLK_CAPSLOCK: notYet("The second equipment set"); break;
                case SDLK_SPACE:
                    // Open content has no companions.
                    if (partner_) {
                        togglePartnerMode();
                    } else {
                        notYet("Companions");
                    }
                    break;
                case SDLK_x: notYet("The special items window"); break;
                case SDLK_m: toggleMagicWindow(); break;
                case SDLK_q: toggleMissions(); break;
                case SDLK_n: toggleAreaMap(); break;
                case SDLK_HOME:
                    if (map_) placePlayer();
                    break;
                default: break;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            // Mouse events arrive in logical (640x480) coordinates.
            if (event.button.button != SDL_BUTTON_LEFT && event.button.button != SDL_BUTTON_RIGHT) break;
            if (helpOpen_) {
                helpOpen_ = false;
                break;
            }
            if (warpGateClick(event.button.x, event.button.y, event.button.button == SDL_BUTTON_RIGHT)) break;
            if (missionsClick(event.button.x, event.button.y, event.button.button == SDL_BUTTON_RIGHT)) break;
            if (event.button.button == SDL_BUTTON_LEFT && missionHudClick(event.button.x, event.button.y)) break;
            if (shopClick(event.button.x, event.button.y, event.button.button == SDL_BUTTON_RIGHT)) break;
            if (statusClick(event.button.x, event.button.y)) break;
            if (magicWindowClick(event.button.x, event.button.y, event.button.button == SDL_BUTTON_RIGHT)) break;
            if (event.button.button == SDL_BUTTON_LEFT && powerButtonClick(event.button.x, event.button.y)) break;
            if (event.button.button == SDL_BUTTON_LEFT && partnerGaugeClick(event.button.x, event.button.y)) break;
            if (beltClick(event.button.x, event.button.y, event.button.button == SDL_BUTTON_RIGHT)) break;
            if (event.button.button == SDL_BUTTON_LEFT && mineButtonClick(event.button.x, event.button.y)) break;
            if (event.button.button == SDL_BUTTON_LEFT && spellBarClick(event.button.x, event.button.y)) break;
            if (event.button.button == SDL_BUTTON_LEFT && panelClick(event.button.x, event.button.y)) break;
            if (inventoryClick(event.button.x, event.button.y, event.button.button == SDL_BUTTON_RIGHT)) break;
            // While Identify waits for an item, the right button calls it
            // off (0x445a40) and casts nothing.
            if (identifying_ && event.button.button == SDL_BUTTON_RIGHT) {
                identifying_ = false;
                showMessage("Identify cancelled");
                break;
            }
            if (areaMapClick(event.button.x, event.button.y, event.button.button == SDL_BUTTON_RIGHT)) break;
            // Tab and a click give the companion an order.
            if (SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_TAB] &&
                partnerOrder(event.button.x, event.button.y, event.button.button == SDL_BUTTON_RIGHT)) {
                break;
            }
            if (event.button.button == SDL_BUTTON_RIGHT && map_ && player_) {
                if (spells_.knows(spells_.selected)) {
                    // The right button casts the chosen spell at the cursor.
                    double wx, wy;
                    screenToWorld(pixelX(event.button.x), pixelY(event.button.y), wx, wy);
                    startCast(wx, wy);
                } else if (const auto target = clickTarget(event.button.x, event.button.y, 1u << Settings::kEnemy);
                           target.kind == Settings::kEnemy) {
                    // With the attack chosen (F9) it strikes the monster under it.
                    attackTarget_ = target.index;
                    talkTarget_.reset();
                    objectTarget_.reset();
                }
            }
            if (event.button.button == SDL_BUTTON_LEFT) {
                mouseHeld_ = true;
                // Clicking a person walks up to them to talk, unless
                // something nearer the front of the click priority is there.
                const auto target = clickTarget(event.button.x, event.button.y, ~0u);
                if (target.kind == Settings::kPerson && player_) {
                    talkTarget_ = target.index;
                    attackTarget_.reset();
                    mouseHeld_ = false;
                } else if (target.kind == Settings::kObject && player_) {
                    objectTarget_ = target.index;
                    attackTarget_.reset();
                    mouseHeld_ = false;
                }
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                mouseHeld_ = false;
                magicWindowRelease(event.button.x, event.button.y);
            }
            break;
        default: break;
    }
}

void Game::closeInventory() {
    showInventory_ = false;
    identifying_ = false;  // closing the window ends Identify (0x41e4f0)
    // Closing the panel puts a held item back, or drops it.
    if (hand_) {
        if (inventory_.add(*hand_)) {
            hand_.reset();
        } else {
            dropHand();
        }
    }
}

void Game::notYet(const char* what) { showMessage(std::format("{} is not in the game yet", what)); }

bool Game::inClickRange(double dx, double dy) const {
    const int reach = settings_.clickReach();
    return std::abs(dx) <= reach && std::abs(dy) <= reach;
}

Game::ClickTarget Game::clickTarget(float screenX, float screenY, unsigned kinds) const {
    for (const int kind : settings_.clickPriority) {
        if (!(kinds & (1u << kind))) continue;
        std::optional<std::size_t> found;
        switch (kind) {
            case Settings::kEnemy: found = enemyAt(screenX, screenY); break;
            case Settings::kItem: found = groundItemAt(screenX, screenY); break;
            case Settings::kObject: found = objectAt(screenX, screenY); break;
            case Settings::kPerson: found = personAt(screenX, screenY); break;
            case Settings::kCompanion:
                if (partnerAt(screenX, screenY)) found = 0;
                break;
            default: break;
        }
        if (found) return {kind, *found};
    }
    return {};
}

void Game::tick() {
    ++ticks_;
    tickMusic();
    if (hurtPercent_ >= 0 && ticks_ == 4 * kTicksPerSecond) playerHp_ = std::max(1, playerMaxHp() * hurtPercent_ / 100);
    if (newCharacter_) tickNewCharacter();
    if (loadData_) tickLoadData();
    if (!map_ || menuOpen_ || optionsOpen_ || newCharacter_ || loadData_) return;  // the world waits while a menu is open
    tickMissions();
    if (startScripts_) {
        startScripts_ = false;
        runEvent(ScenarioScript::kOnStart);
    }
    if (pendingScenario_ && !dialogue_) {
        const auto [number, entry] = *pendingScenario_;
        changeScenario(number, entry);
        updateCamera();
        return;
    }
    if (dialogue_ && !answers_.empty()) {
        const int answer = answers_.front();
        answers_.erase(answers_.begin());
        closeDialogue(answer);
    }
    if (dialogue_ || openShop_) return;  // and while the player talks or trades
    tickVisual();
    tickRain();
    unlockSwitch_ = false;  // SETUNLOCKSW lasts the frame a script sets it (0x443490)
    // Event 5 runs every one of the original's ticks, 30 a second, so
    // traps fire and script counters count at its pace.
    for (everyTickCarry_ += 30.0 / kTicksPerSecond; everyTickCarry_ >= 1.0 && !pendingScenario_; everyTickCarry_ -= 1.0) {
        runEvent(ScenarioScript::kEveryTick);
    }
    tickObjects();
    for (auto& actor : actors_) {
        // Scripts show and hide characters through their NET flag, by
        // SETACTIVE or by setting it themselves.
        if (const auto f = flags_.find({ScenarioScript::kNet, kStateFlagBases[0] + actor.gameId()}); f != flags_.end()) {
            actor.active = f->second != 0;
        }
        if (!actor.active) continue;
        if (actor.hostile() && player_) {
            if (actor.aiList >= 0) {
                tickEnemyAi(actor);
            } else {
                tickEnemy(actor);
            }
        }
        actor.character.tick();
        monsterCries(actor);
    }
    if (!player_) return;
    tickPlayer();
    player_->tick();
    if (power_.active()) {
        // A hum at the hero every 15 of the original's ticks, the first as
        // it starts: 60 in all (0x443490).
        if (const double hum = std::floor(power_.left / 15) * 15; hum > 0 && hum > power_.left - 1.5) {
            playSound(0x4c, player_->x, player_->y);
        }
        power_.tick(1.5);  // the original's ticks, 30 a second
        powerAuraTicks_ += 1.5;
    }
    tickPartner();
    // The bite's cue (cell flag 0x400 of its chart, 0x45fff0).
    if (partner_ && (partner_->character.takeCues() & 0x400) && partner_->state == Partner::State::Bite) {
        playSound(0x5f, partner_->character.x, partner_->character.y);
    }
    tickSpells();
    tickTransportGate();
    pickUpItems();
    for (auto& g : groundItems_) {
        if (g.sprite) g.sprite->tick();
    }
    if (messageTicks_ > 0) --messageTicks_;
    for (auto& l : labels_) --l.ticks;
    std::erase_if(labels_, [](const Label& l) { return l.ticks <= 0; });
    checkTriggers();
    if (pendingScenario_) {
        const auto [number, entry] = *pendingScenario_;
        changeScenario(number, entry);
    }
    updateCamera();
    tickAreaMap();
}

// Runs the script of each trigger area the player has just stepped into.
void Game::checkTriggers(bool fire) {
    if (!player_) return;
    for (auto& t : triggers_) {
        const bool inside = player_->x >= t.left && player_->x <= t.right && player_->y >= t.top && player_->y <= t.bottom;
        if (fire && inside && !t.inside && script_ && !pendingScenario_) {
            runScript(t.sentence);
        }
        t.inside = inside;
    }
}

// Loads another scenario's map and puts the party at one of its entry points.
void Game::changeScenario(int number, int entry) {
    pendingScenario_.reset();
    const auto position = std::exchange(pendingPosition_, std::nullopt);
    char dir[32];
    std::snprintf(dir, sizeof dir, "Scenario/%08d", number);
    ScenarioControl target;
    if (!readScenario(dir, target, nullptr)) {
        log::warn("Exit to scenario {} leads nowhere: {} is missing", number, dir);
        return;
    }
    std::string map = target.mapPath;
    if (const auto slash = map.find_last_of("\\/"); slash != std::string::npos) map = map.substr(slash + 1);
    map = stem(map);
    const auto found = std::ranges::find_if(mapNames_, [&](const std::string& m) { return equalsIgnoreCase(m, map); });
    if (found == mapNames_.end()) {
        log::warn("{}: map {} not found", dir, map);
        return;
    }
    log::info("Going to {} \"{}\", entry {}", dir, target.title, entry);
    entryPoint_ = entry;
    loadMap(static_cast<std::size_t>(found - mapNames_.begin()), dir);
    // The player is the first of the four party members.
    for (const auto& e : target.entryPoints) {
        if (e.id != entry * 4 || !player_) continue;
        player_->x = e.x;
        player_->y = e.y;
        if (e.direction >= 0 && e.direction < 8) player_->direction = e.direction;
        if (partner_) {
            partner_->character.x = e.x;
            partner_->character.y = e.y;
        }
        log::info("Player enters at world ({}, {})", e.x, e.y);
        break;
    }
    if (position && player_) {
        player_->x = position->first;
        player_->y = position->second;
        if (partner_) {
            partner_->character.x = position->first;
            partner_->character.y = position->second;
        }
        log::info("Player enters at world ({}, {})", position->first, position->second);
    }
    checkTriggers(false);
    updateCamera();
}

std::optional<std::size_t> Game::enemyAt(float screenX, float screenY) const {
    // Characters stand on their feet; aim at the middle of the body.
    std::optional<std::size_t> best;
    double bestDistance = INFINITY;
    for (std::size_t i = 0; i < actors_.size(); ++i) {
        const auto& a = actors_[i];
        if (!a.hostile() || !a.alive() || !a.active) continue;
        int sx, sy;
        worldToScreen(static_cast<int>(a.character.x), static_cast<int>(a.character.y), sx, sy);
        const SDL_FPoint at = onScreen(sx, sy - 40);
        const double dx = at.x - screenX, dy = at.y - screenY;
        const double d = std::hypot(dx, dy);
        if (inClickRange(dx, dy) && d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

// A swing of the hero's weapon, as fast as their attack speed (power mode:
// the fastest).
void Game::swing() {
    player_->restart(chart::kPlayerPunch, attackSpeedFactor(magic_, playerStats()[HeroStats::kAttackSpeed], power_.active()));
    blowLanded_ = false;
    shouted_ = false;
    swingTicks_ = 0;
    bowShot_.reset();
}

void Game::tickPlayer() {
    if (player_->chart() != chart::kWalk && player_->chart() != chart::kPlayerRun) heroSteps(false, false);
    // A trap's stun holds the hero in the hit pose (0x435870).
    if (heroStun_) {
        if (tickStun(*player_, *heroStun_)) return;
        heroStun_.reset();
        player_->play(chart::kIdle);
    }
    // A hit or a punch plays out before anything else happens.
    const int current = player_->chart();
    if (casting_) {
        // The spell leaves the hand halfway through the casting animation.
        if (!castReleased_ && player_->frame() >= player_->frameCount() / 2) releaseSpell();
        if (!player_->finished()) return;
        casting_.reset();
        player_->play(chart::kIdle);
    }
    if (current == chart::kPlayerHit && !player_->finished()) return;
    if (bowShot_ && (current == chart::kPlayerPunch || current == chart::kPlayerShoot)) {
        // The string's twang, 6 of the original's ticks in (0x437fe0,
        // 0x438a90).
        if (swingTicks_ <= 6 && swingTicks_ + 1.5 > 6) playSound(3, player_->x, player_->y);
        swingTicks_ += 1.5;
        // A bowgun shoots on its chart's shooting frame (once, as a fast
        // pull can step past it).
        if (!blowLanded_ && player_->frame() >= strikeFrame(current, current == chart::kPlayerShoot ? 3 : 4)) {
            blowLanded_ = true;
            loose();
        }
        if (!player_->finished()) return;
        bowShot_.reset();
        player_->play(chart::kIdle);
    } else if (current == chart::kPlayerPunch) {
        const auto& weapon = inventory_.equipped(EquipSlot::Weapon);
        const int subtype = weapon ? weapon->def->subtype : -1;
        // Swords, great swords and axes have their own swing, which shouts
        // as the blow falls, hit or miss (0x435e60); the rest are the plain
        // attack (0x439140).
        const bool bladed = subtype == 0 || subtype == 1 || subtype == 3;
        if (player_->frame() >= 4 && !shouted_) {
            shouted_ = true;
            if (bladed) playSound(heroSex_ == HeroSex::Female ? 0x63 : 0x60, player_->x, player_->y);
        }
        // The swing's whoosh, 6 of the original's ticks in (5 for the
        // plain attack): heavy weapons 2, light ones and bare hands 1.
        const double at = bladed ? 6 : 5;
        if (swingTicks_ <= at && swingTicks_ + 1.5 > at) {
            playSound(weapon ? itemSound(*weapon->def, kItemSwing) : 1, player_->x, player_->y);
        }
        swingTicks_ += 1.5;
        // The blow lands halfway through the swing (once, as a fast swing
        // can step past the frame).
        if (player_->frame() >= 4 && !blowLanded_ && attackTarget_) {
            blowLanded_ = true;
            Actor& target = actors_[*attackTarget_];
            if (target.alive() && distance(*player_, target.character) <= kMeleeRange * 1.2) strikeEnemy(target);
        }
        if (!player_->finished()) return;
        player_->play(chart::kIdle);
    }

    // Holding the left button walks towards the cursor, or attacks the
    // monster under it, like the original.
    // Shift holds the hero where they stand; with the button down they
    // strike in place, at whatever is in reach.
    const bool stayPut = (SDL_GetModState() & KMOD_SHIFT) != 0;
    if (mouseHeld_) {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        float lx, ly;
        SDL_RenderWindowToLogical(renderer_, mx, my, &lx, &ly);
        const auto target = clickTarget(lx, ly, (1u << Settings::kEnemy) | (1u << Settings::kItem));
        attackTarget_.reset();
        talkTarget_.reset();
        objectTarget_.reset();
        if (stayPut) {
            walkTarget_.reset();
            if (identifying_) return;  // no fighting while Identify waits
            if (target.kind == Settings::kEnemy) attackTarget_ = target.index;
            double wx, wy;
            screenToWorld(pixelX(lx), pixelY(ly), wx, wy);
            player_->faceTowards(wx - player_->x, wy - player_->y);
            if (bowInHand()) {
                shoot(attackTarget_, std::atan2(wy - player_->y, wx - player_->x));
            } else {
                swing();
            }
            return;
        }
        if (target.kind == Settings::kEnemy && !identifying_) {
            attackTarget_ = target.index;
            walkTarget_.reset();
        } else if (target.kind == Settings::kItem) {
            // Walking onto an item picks it up.
            walkTarget_.emplace(groundItems_[target.index].x, groundItems_[target.index].y);
        } else {
            double wx, wy;
            screenToWorld(pixelX(lx), pixelY(ly), wx, wy);
            walkTarget_.emplace(wx, wy);
        }
    }
    if (attackTarget_ && (!actors_[*attackTarget_].alive() || !actors_[*attackTarget_].active)) attackTarget_.reset();
    if (talkTarget_) {
        Actor& person = actors_[*talkTarget_];
        if (distance(*player_, person.character) <= kTalkRange) {
            player_->faceTowards(person.character.x - player_->x, person.character.y - player_->y);
            player_->setAction(Action::Idle);
            talkTo(person);
            return;
        }
        walkTarget_.emplace(person.character.x, person.character.y);
    }
    if (objectTarget_) {
        // Objects are used from anywhere in their rectangle or close to it.
        const auto& p = control_.placements[*objectTarget_];
        const double nx = std::clamp(player_->x, double(p.x + p.rect[0]), double(p.x + p.rect[2]));
        const double ny = std::clamp(player_->y, double(p.y + p.rect[1]), double(p.y + p.rect[3]));
        if (std::hypot(nx - player_->x, ny - player_->y) <= kTalkRange) {
            const std::int32_t id = ScenarioControl::idBase(p.group) + p.id;
            objectTarget_.reset();
            walkTarget_.reset();
            player_->setAction(Action::Idle);
            runEvent(ScenarioScript::kOnTalk, id);
            return;
        }
        walkTarget_.emplace(nx, ny);
    }
    if (autoFight_ && !attackTarget_) {
        double best = kAggroRange;
        for (std::size_t i = 0; i < actors_.size(); ++i) {
            if (!actors_[i].hostile() || !actors_[i].alive() || !actors_[i].active) continue;
            const double d = distance(*player_, actors_[i].character);
            if (d < best) {
                best = d;
                attackTarget_ = i;
            }
        }
        // With nothing to fight, collect what is lying around.
        if (!attackTarget_ && !walkTarget_) {
            double nearest = kAggroRange;
            for (const auto& g : groundItems_) {
                const double d = std::hypot(g.x - player_->x, g.y - player_->y);
                if (!g.waitForLeave && d < nearest) {
                    nearest = d;
                    walkTarget_.emplace(g.x, g.y);
                }
            }
        }
    }

    if (stayPut && !autoFight_) {
        walkTarget_.reset();
        talkTarget_.reset();
        objectTarget_.reset();
        attackTarget_.reset();
        player_->setAction(Action::Idle);
        return;
    }

    // Keyboard movement in screen directions (ours: the original walks by
    // mouse only, and its letter keys open windows).
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    double kx = 0, ky = 0;
    if (!mapOpen_) {  // the open map scrolls with them instead
        if (keys[SDL_SCANCODE_LEFT]) kx -= 1;
        if (keys[SDL_SCANCODE_RIGHT]) kx += 1;
        if (keys[SDL_SCANCODE_UP]) ky -= 1;
        if (keys[SDL_SCANCODE_DOWN]) ky += 1;
    }

    double dx = 0, dy = 0;
    if (kx != 0 || ky != 0) {
        walkTarget_.reset();
        talkTarget_.reset();
        objectTarget_.reset();
        attackTarget_.reset();
        // A screen-space step converted to world space.
        screenToWorld(kx * projection.x, ky * projection.y, dx, dy);
    } else if (attackTarget_) {
        const Character& enemy = actors_[*attackTarget_].character;
        dx = enemy.x - player_->x;
        dy = enemy.y - player_->y;
        // A bowgun shoots from wherever the hero stands (0x449240).
        if (bowInHand()) {
            player_->faceTowards(dx, dy);
            shoot(attackTarget_, std::atan2(dy, dx));
            if (!mouseHeld_ && !autoFight_) attackTarget_.reset();
            return;
        }
        if (std::hypot(dx, dy) <= kMeleeRange) {
            player_->faceTowards(dx, dy);
            swing();
            // Keep punching only while the button is held.
            if (!mouseHeld_ && !autoFight_) attackTarget_.reset();
            return;
        }
    } else if (walkTarget_) {
        dx = walkTarget_->first - player_->x;
        dy = walkTarget_->second - player_->y;
    }

    // Run when RUN is chosen (R), or while Ctrl is held.
    const bool running = runMode_ || (SDL_GetModState() & KMOD_CTRL) != 0;
    // Walking speed picks a step of the original's scale; power mode the
    // fastest.
    const double speed = (running ? kRunSpeed : kWalkSpeed) * walkSpeedFactor(playerStats()[HeroStats::kWalkSpeed], power_.active());
    const double length = std::hypot(dx, dy);
    if (length > 1.0) {
        const double step = std::min(speed, walkTarget_ ? length : speed);
        player_->faceTowards(dx, dy);
        if (tryMove(*player_, dx / length * step, dy / length * step)) {
            player_->play(running && player_->hasChart(chart::kPlayerRun) ? chart::kPlayerRun : chart::kWalk);
            heroSteps(true, running);
            if (walkTarget_ && length <= speed) walkTarget_.reset();
        } else {
            // Ran into a wall: give up rather than pushing against it.
            walkTarget_.reset();
            attackTarget_.reset();
            player_->setAction(Action::Idle);
        }
    } else {
        walkTarget_.reset();
        player_->setAction(Action::Idle);
    }
}

// Placeholder behaviour: notice the player when close, walk up to them,
// swing, and wander back home when they get away.
void Game::tickEnemy(Actor& enemy) {
    using State = Actor::State;
    Character& c = enemy.character;
    switch (enemy.state) {
        case State::Dead: return;
        case State::Hit:
            if (enemy.stunTicks > 0 && (enemy.stunTicks -= kOriginalTicksPerTick) <= 0) endStun(enemy);
            if (!c.finished() || enemy.stunTicks > 0) return;
            enemy.state = State::Chase;
            break;
        case State::Attack:
            if (c.frame() == c.frameCount() / 2 && distance(c, *player_) <= kMeleeRange * 1.2) {
                strikePlayer(enemy);
            }
            if (!c.finished()) return;
            enemy.state = State::Chase;
            break;
        default: break;
    }

    const double toPlayer = distance(c, *player_);
    const bool chasing = enemy.state == State::Chase ? toPlayer < kGiveUpRange : toPlayer < kAggroRange;
    double tx = enemy.placement.x, ty = enemy.placement.y;
    if (chasing) {
        enemy.state = State::Chase;
        if (toPlayer <= kMeleeRange && c.hasChart(chart::kEnemyAttack)) {
            c.faceTowards(player_->x - c.x, player_->y - c.y);
            c.restart(chart::kEnemyAttack);
            enemy.state = State::Attack;
            return;
        }
        tx = player_->x;
        ty = player_->y;
    } else {
        enemy.state = State::Idle;
    }

    const double dx = tx - c.x, dy = ty - c.y;
    const double length = std::hypot(dx, dy);
    const double stopAt = chasing ? kMeleeRange * 0.8 : 1.0;
    if (length > stopAt) {
        const double step = std::min(kEnemySpeed, length - stopAt);
        c.faceTowards(dx, dy);
        c.play(tryMove(c, dx / length * step, dy / length * step) ? chart::kWalk : chart::kIdle);
    } else {
        c.play(chart::kIdle);
    }
}

void Game::strikeEnemy(Actor& enemy) {
    const auto& s = enemy.placement.stats;
    if (random_.next(100) < hitChance(playerHit(), s.evasion)) {
        playSound(6, player_->x, player_->y);  // the blow lands, at the hero
        // Table 7: the hero's level in the monster's opposite element
        // strengthens the blow (0x412a40).
        hurtEnemy(enemy, heroBlowDamage(magic_, playerAttack(), playerElements(), s.element, s.defense, random_.next(3)));
    }
    // Every blow wears the weapon a little, hit or miss.
    sayBroken(inventory_.wearFromAttacking(random_));
}

void Game::sayBroken(const std::vector<EquipSlot>& slots) {
    for (EquipSlot slot : slots) {
        if (const auto& item = inventory_.equipped(slot)) {
            showMessage(std::format("{} is broken", item->shownName()));
            log::info("{} broke", item->def->name);
        }
    }
    // Broken things stop raising the companion's stats.
    if (!slots.empty()) refreshPartnerStats();
}

void Game::strikePlayer(Actor& enemy, int slot, bool magic) {
    const auto& s = enemy.placement.stats;
    const auto k = static_cast<std::size_t>(std::clamp(slot, 0, 2));
    const HeroStats stats = playerStats();
    if (magic && s.spells[k] >= 0 && s.spells[k] < kSpellCount) {
        // A monster's spell, as the cast code at 0x45b043 sets it up: the
        // spell's element and hit at the monster's spell level, its own
        // power, and its bonus to hit.
        const int spell = s.spells[k];
        const int hit = magic_.hit(spell, s.spellLevel[k]) + s.spellHit[k];
        if (random_.next(100) >= hitChance(hit, stats[HeroStats::kMagicEvasion])) return;
        const int element = magic_.spell(spell).element;
        const int damage = damageToHero(magic_, s.spellPower[k], element, playerElements(), stats[HeroStats::kMagicDefense], random_.next(3));
        showSpellHit(spell, player_->x, player_->y);
        hurtPlayer(damage, true, &enemy);
        return;
    }
    // The blow also catches an ACTIVE companion in front of the monster.
    if (partnerInReach(enemy)) strikePartner(enemy, slot);
    if (random_.next(100) >= hitChance(s.hits[k], stats[HeroStats::kEvasion])) return;
    // The blow lands: a thud at the monster, unless Counter Burst killed it
    // first (0x45a2f0).
    const auto landed = [&] {
        if (enemy.alive()) playSound(6, enemy.character.x, enemy.character.y);
    };
    // Table 11: the hero's level in the monster's element strengthens their
    // defence. Energy Shield changes the defence first (0x443cb0).
    int defense = stats[HeroStats::kDefense];
    if (power_.active()) defense = defense * 12 / 10;
    if (spellOn(kEnergyShield)) defense = defense * magic_.power(kEnergyShield, castLevel(kEnergyShield)) / 100;
    hurtPlayer(damageToHero(magic_, s.attacks[k], s.element, playerElements(), defense, random_.next(3)), false, &enemy);
    landed();
}

void Game::hurtEnemy(Actor& enemy, int damage, bool bySpell, bool byPartner) {
    if (!enemy.alive()) return;
    enemy.hp -= damage;
    if (enemy.hp <= 0) {
        enemy.hp = 0;
        enemy.state = Actor::State::Dead;
        // One killed as stone stays grey (its +0x344 is left at 2).
        enemy.stunTicks = 0;
        enemy.character.held = false;
        enemy.character.restart(chart::kEnemyDeath);
        monsterDeathCry(enemy);
        log::info("Monster {} defeated", enemy.gameId());
        dropLoot(enemy);
        // The original counts how the hero kills monsters, to offer a class
        // (0x4134a0), and every kill trains the companion (0x413640).
        const auto& weapon = inventory_.equipped(EquipSlot::Weapon);
        if (!byPartner) {
            career_.countKill(weapon ? std::optional(weapon->def->subtype) : std::nullopt, bySpell);
            ++power_.charge;  // towards increased-power mode (0x4134a0)
        }
        partnerCountKill();
        // Moon, Berserker and Energy Shield train by kills while they are
        // on (0x419c91).
        for (int spell = 0; spell < kSpellCount; ++spell) {
            if (trainsByKills(spell) && spellOn(spell)) trainSpell(spell);
        }
        runEvent(ScenarioScript::kOnEnemyDead, enemy.gameId());
        const auto& s = enemy.placement.stats;
        gainExperience(s.experience > 0 ? s.experience : defaultExperience(s.hp, s.attack()));
    } else {
        enemy.state = Actor::State::Hit;
        enemy.character.restart(chart::kEnemyHit);
        enemy.action = -1;
        enemy.event = AiControl::kHit;
    }
}

// What happens to a blow or spell that got through (0x443cb0).
void Game::hurtPlayer(int damage, bool magic, Actor* attacker, bool flinch) {
    // Shields charge MP each time they work. The original charges the
    // chosen spell's cost at the shield's level; ours the shield's own.
    auto chargeShield = [&](int shield) {
        const int charged = rules_.shieldsCostTheirOwnMp || spells_.selected < 0 ? shield : spells_.selected;
        playerMp_ -= mpCost(charged, castLevel(shield));
        if (playerMp_ <= 0) {
            playerMp_ = 0;
            setSpellOn(shield, false);
        }
    };
    if (magic && spellOn(kMagicShield)) {
        damage = std::max(1, damage * (100 - magic_.power(kMagicShield, castLevel(kMagicShield))) / 100);
        if (damage >= 20) trainSpell(kMagicShield);
        chargeShield(kMagicShield);
        spawnEffect(11000241, player_->x, player_->y).followsPlayer = true;  // it takes the blow
        playSound(0x3c, player_->x, player_->y);
    }
    // Energy Shield takes blows out of MP while there is any.
    if (!magic && spellOn(kEnergyShield) && playerMp_ > 0) {
        playerMp_ = std::max(0, playerMp_ - damage);
    } else {
        playerHp_ -= damage;
    }
    if (playerHp_ <= 0) {
        // No death animation or game over yet: start the map again.
        log::info("The player was defeated");
        playSound(heroSex_ == HeroSex::Female ? 14 : 13, player_->x, player_->y);  // 0x435b60
        for (auto& actor : actors_) {
            if (actor.alive()) {
                actor.state = Actor::State::Idle;
                actor.action = -1;
                actor.event = AiControl::kDecide;
                actor.character.x = actor.placement.x;
                actor.character.y = actor.placement.y;
            }
        }
        placePlayer();
        return;
    }
    // Blows that get through wear out the armour (0x443fa8).
    sayBroken(inventory_.wearFromBlow(random_));
    // Counter Burst sends part of a monster's blow back (0x444150): half
    // as much to monsters whose word 7 is 100, at least 1.
    if (!magic && attacker && attacker->alive() && spellOn(kCounterBurst)) {
        int back = damage * magic_.power(kCounterBurst, castLevel(kCounterBurst)) / 100;
        if (attacker->placement.stats.word7 == 100) back /= 2;
        if (damage >= 20) trainSpell(kCounterBurst);
        chargeShield(kCounterBurst);
        spawnEffect(11000251, player_->x, player_->y).followsPlayer = true;
        playSound(0x3c, player_->x, player_->y);
        hurtEnemy(*attacker, std::max(1, back), true);
    }
    // A blow does not interrupt the player's own swing or a flinch already
    // playing, or a crowd of monsters could keep them from ever fighting back.
    if (flinch && !casting_ && player_->chart() != chart::kPlayerPunch && player_->chart() != chart::kPlayerHit) {
        player_->restart(chart::kPlayerHit);
        walkTarget_.reset();
    }
}

void Game::render(double interpolation) {
    hoverOn_ = false;
    if (visual_ && !menuOpen_ && !optionsOpen_) {
        // A story picture covers the game while it shows (0x417bd0).
        renderVisual();
    } else if (map_) {
        renderMap();
    } else {
        renderPlaceholder(interpolation);
    }
    if (optionsOpen_) {
        renderOptions();
    } else if (newCharacter_) {
        renderNewCharacter();
    } else if (loadData_) {
        renderLoadData();
    } else if (menuOpen_) {
        renderMenu();
    }
    // The name under the pointer is over the windows (layer 17) and in a
    // screenshot; the frame (0x41e4f0) saves that before it draws the
    // pointer's square and the pointer (0x416bb0, 0x4087b0).
    const bool hovering = hoverOn_ && !optionsOpen_ && !newCharacter_ && !loadData_ && !menuOpen_;
    if (hovering) renderHoverName();
    if (screenshotPending_) takeScreenshot();
    if (hovering) renderClickSquare();
    renderPointer();
    SDL_RenderPresent(renderer_);
}

void Game::renderMap() {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    // Zoomed out, the world is drawn as at full size to a picture of all
    // the screen shows, then shrunk onto it; the names, bars, rain, panel
    // and windows go over that at their own size.
    SDL_Texture* screen = SDL_GetRenderTarget(renderer_);
    SDL_Texture* world = zoom() < 1 ? worldTexture() : nullptr;
    if (world) {
        SDL_SetRenderTarget(renderer_, world);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
    }

    const int camX = static_cast<int>(std::floor(cameraX_));
    const int camY = static_cast<int>(std::floor(cameraY_));
    const auto& g = map_->ground();

    if (g.isoTile > 0) {
        renderIsoGround(camX, camY);
    } else if (skin_) {
        skin_->renderGround(camX, camY);
    } else {
        renderChips(camX, camY);
    }
    renderGroundItems(camX, camY);

    // The one under the pointer is drawn brighter (0x40ee70 adds 300 to
    // its red, green and blue).
    updateHover();
    for (auto& a : actors_) a.character.glow = 0;
    if (const Actor* lit = hoveredActor()) actors_[static_cast<std::size_t>(lit - actors_.data())].character.glow = kHoverGlow;

    // Objects and characters, back to front. Shadows go first so nothing
    // stands under one; characters draw their own shadows.
    std::vector<const Character*> characters;
    if (player_) characters.push_back(&*player_);
    if (partner_ && partner_->character.tint.a > 0) characters.push_back(&partner_->character);
    for (const auto& actor : actors_) {
        if (!actor.active) continue;
        int sx, sy;
        worldToScreen(static_cast<int>(actor.character.x), static_cast<int>(actor.character.y), sx, sy);
        if (sx - camX < -256 || sy - camY < -256 || sx - camX > viewWidth() + 256 || sy - camY > viewHeight() + 256) continue;
        characters.push_back(&actor.character);
    }
    // Spell effects are sorted in with them by where they stand, as the
    // original sorts its effects, so a tree in front hides a burst behind it.
    struct Drawable {
        double x, y;
        const Character* character;
        const Effect* effect;
        SDL_Rect picture;  // where it lands, in map pixels
        // One past the last of drawOrder_ it stands in front of and
        // overlaps: it may not go under anything before that.
        std::size_t frontOf = 0;
    };
    // Effects can be large (a burst, a pillar of fire), so they take a
    // generous box rather than their exact frame.
    auto effectBox = [](const Effect& e) {
        int sx, sy;
        worldToScreen(static_cast<int>(e.x), static_cast<int>(e.y), sx, sy);
        return SDL_Rect{sx - 192, sy - 384, 384, 480};
    };
    std::vector<Drawable> drawables;
    for (const Character* c : characters) drawables.push_back({c->x, c->y, c, nullptr, c->pictureBounds(sprites_)});
    for (const Effect& e : effects_) drawables.push_back({e.x, e.y, nullptr, &e, effectBox(e)});
    const std::vector<Effect> gate = transportGateParts();
    for (const Effect& e : gate) drawables.push_back({e.x, e.y, nullptr, &e, effectBox(e)});
    std::ranges::stable_sort(drawables, [](const Drawable& a, const Drawable& b) { return a.x + a.y < b.x + b.y; });
    // Where each map object's picture lands, by place in drawOrder_.
    std::vector<SDL_Rect> pictures(drawOrder_.size());
    if (!drawables.empty()) {
        for (std::size_t k = 0; k < drawOrder_.size(); ++k) pictures[k] = objectPicture(map_->objects().objects[drawOrder_[k]]);
    }
    for (Drawable& d : drawables) {
        for (std::size_t k = drawOrder_.size(); k-- > 0;) {
            if (SDL_HasIntersection(&pictures[k], &d.picture) && map_->objects().objects[drawOrder_[k]].inFront(d.x, d.y)) {
                d.frontOf = k + 1;
                break;
            }
        }
    }

    // Semi-transparent Shadow off draws them solid black.
    const int shadowAlpha = settings_.seeThroughShadows ? 96 : 255;
    const auto& objects = map_->objects().objects;
    // The scenario's chests, doors and levers sort in among the map's
    // pieces by the same footprint.
    std::vector<std::pair<MapObject, const SceneObject*>> scene;
    for (const auto& o : sceneObjects_) {
        if (placementOf(o).look.drawn() && objectState(o, kShown)) scene.emplace_back(objectFootprint(o), &o);
    }
    std::ranges::stable_sort(scene, [](const auto& a, const auto& b) { return a.first.depth() < b.first.depth(); });
    for (int pass = 0; pass < 2; ++pass) {
        const bool shadows = pass == 0;
        // Characters and effects not drawn yet this pass, back to front.
        std::vector<Drawable> pending = shadows ? std::vector<Drawable>{} : drawables;
        auto draw = [&](const Drawable& d) {
            if (d.character) d.character->draw(renderer_, sprites_, camX, camY, shadowAlpha);
            if (!shadows && d.character && player_ && d.character == &*player_) {
                renderPowerAura(camX, camY);
                if (unlockSwitch_) renderHeroOverlay(model("Player/Common/UnlockSW"), unlockTicks_, camX, camY);
            }
            if (d.effect) renderEffect(*d.effect, camX, camY);
        };
        // Everyone an object hides goes first, even if someone nearer the
        // front is still waiting for a later object. Only an object whose
        // picture covers part of theirs counts: one that draws nothing (an
        // invisible blocker) or lands elsewhere on screen would pull them
        // under the pieces between it and where they stand, like a monster
        // in front of a palisade drawn behind it. Someone beside the end of
        // a wall, and in front of a piece still to come (the other side of
        // a fence's corner), waits for that piece.
        auto drawHidden = [&](const MapObject& o, const SDL_Rect& picture, std::size_t place) {
            for (auto it = pending.begin(); it != pending.end();) {
                if (SDL_HasIntersection(&picture, &it->picture) && o.hides(it->x, it->y) &&
                    (place >= it->frontOf || o.surelyHides(it->x, it->y))) {
                    draw(*it);
                    it = pending.erase(it);
                } else {
                    ++it;
                }
            }
        };
        std::size_t next = 0;  // into scene
        auto drawSceneUpTo = [&](long long depth, std::size_t place) {
            for (; next < scene.size() && scene[next].first.depth() <= depth; ++next) {
                if (!pending.empty()) drawHidden(scene[next].first, sceneObjectPicture(*scene[next].second), place);
                drawSceneObject(*scene[next].second, shadows, camX, camY, shadowAlpha);
            }
        };
        for (std::size_t k = 0; k < drawOrder_.size(); ++k) {
            const MapObject& o = objects[drawOrder_[k]];
            drawSceneUpTo(o.depth(), k);
            if (!pending.empty()) drawHidden(o, pictures[k], k);
            drawObject(o, shadows, camX, camY, shadowAlpha);
        }
        drawSceneUpTo(LLONG_MAX, drawOrder_.size());
        for (const Drawable& d : pending) draw(d);
    }
    // Semi-transparent Player: a faint copy on top shows the hero through
    // walls; where nothing covers them it lands on itself and changes nothing.
    if (player_ && settings_.seeThroughPlayer) player_->draw(renderer_, sprites_, camX, camY, shadowAlpha, 96);
    renderSpells(camX, camY);
    if (darknessShown()) renderDarkness(camX, camY);
    if (showJudge_) renderJudge(camX, camY);
    if (world) {
        SDL_SetRenderTarget(renderer_, screen);
        const double z = zoom();
        const SDL_FRect dst{0, 0, static_cast<float>(worldTextureW_ * z), static_cast<float>(worldTextureH_ * z)};
        SDL_RenderCopyF(renderer_, world, nullptr, &dst);
    }
    renderHealthBars();
    renderLabels();
    renderRain();
    renderTransportGateName();
    if (worldOnly_) return;  // a save's picture
    // The panel and the magic bar are under the windows, as the exe
    // layers them (11 and 13 below 14).
    renderPanel();
    renderSpellBar();
    renderPowerButton();
    renderCompass();
    renderShop();
    renderStatus();
    renderMagicWindow();
    renderInventory();
    renderMissions();
    renderAreaMap();
    renderWarpGate();
    renderHud();
    renderMissionHud();
    renderPartnerGauge();
    renderDialogue();
    renderHelp();
}

// The original's ground: pre-drawn chips on a screen-aligned grid.
// One map object, in the shadow pass (its shadow, if it is one) or the
// other (its picture, or the skin's in its place).
void Game::drawObject(const MapObject& o, bool shadows, int camX, int camY, int shadowAlpha) {
    if (const auto* pieces = skin_ ? skin_->replacement(o) : nullptr) {
        if (!shadows && !map_->isShadowBank(o.bank)) skin_->renderObject(o, *pieces, camX, camY);
        return;
    }
    if (map_->isShadowBank(o.bank) != shadows) return;
    const SpriteBank* bank = map_->bank(o.bank);
    if (!bank) return;
    int sx, sy;
    worldToScreen(o.x, o.y, sx, sy);
    sx -= camX;
    sy -= camY;
    if (sx < -1024 || sy < -1024 || sx > viewWidth() + 1024 || sy > viewHeight() + 1024) return;
    const SpriteTexture* sprite = sprites_.get(*bank, o.pattern, false);
    if (!sprite) return;
    SDL_Rect dst{sx + sprite->originX, sy + sprite->originY, sprite->width, sprite->height};
    if (dst.x + dst.w < 0 || dst.y + dst.h < 0 || dst.x >= viewWidth() || dst.y >= viewHeight()) return;
    if (shadows) {
        SDL_SetTextureColorMod(sprite->texture, 0, 0, 0);
        SDL_SetTextureAlphaMod(sprite->texture, static_cast<Uint8>(shadowAlpha));
    }
    SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
}

SDL_Rect Game::objectPicture(const MapObject& o) {
    if (map_->isShadowBank(o.bank)) return {};
    const auto* pieces = skin_ ? skin_->replacement(o) : nullptr;
    if (pieces && pieces->empty()) return {};
    const SpriteBank* bank = map_->bank(o.bank);
    const SpriteTexture* sprite = bank ? sprites_.get(*bank, o.pattern, false) : nullptr;
    int sx, sy;
    worldToScreen(o.x, o.y, sx, sy);
    if (!sprite) {
        if (!pieces) return {};
        // A prop with no original picture to measure: take it as
        // covering anyone near it.
        return {sx - 1024, sy - 1024, 2048, 2048};
    }
    SDL_Rect picture{sx + sprite->originX, sy + sprite->originY, sprite->width, sprite->height};
    if (pieces) {
        // Our props are made to fill the original's picture; allow a
        // little for where they spill over.
        constexpr int kSpill = 32;
        picture = {picture.x - kSpill, picture.y - kSpill, picture.w + 2 * kSpill, picture.h + 2 * kSpill};
    }
    return picture;
}

// The map's ground and objects with no one on them, for the map window.
void Game::renderScenery(int camX, int camY) {
    const auto& g = map_->ground();
    if (g.isoTile > 0) {
        renderIsoGround(camX, camY);
    } else if (skin_) {
        skin_->renderGround(camX, camY);
    } else {
        renderChips(camX, camY);
    }
    const int shadowAlpha = settings_.seeThroughShadows ? 96 : 255;
    for (const bool shadows : {true, false}) {
        for (std::size_t i : drawOrder_) drawObject(map_->objects().objects[i], shadows, camX, camY, shadowAlpha);
    }
}

void Game::renderChips(int camX, int camY) {
    const auto& g = map_->ground();
    const int x0 = std::max(0, camX / g.chipWidth - 1);
    const int y0 = std::max(0, camY / g.chipHeight - 1);
    const int x1 = std::min(g.width, (camX + viewWidth()) / g.chipWidth + 2);
    const int y1 = std::min(g.height, (camY + viewHeight()) / g.chipHeight + 2);
    for (int cy = y0; cy < y1; ++cy) {
        for (int cx = x0; cx < x1; ++cx) {
            const auto& cell = g.at(cx, cy);
            const SpriteBank* bank = map_->bank(cell.bank);
            if (!bank) continue;
            const SpriteTexture* sprite = sprites_.get(*bank, cell.pattern, true);
            if (!sprite) continue;
            SDL_Rect dst{cx * g.chipWidth - camX, cy * g.chipHeight - camY, sprite->width, sprite->height};
            SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
        }
    }
}

// Open content ground: isometric tiles, drawn back to front so taller
// tiles overlap the ones behind them.
void Game::renderIsoGround(int camX, int camY) {
    const auto& g = map_->ground();
    // Tiles touching the view, with a margin for sprites that stick out.
    int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
    const int w = viewWidth(), h = viewHeight();
    for (const auto& [px, py] : {std::pair{0, 0}, {w, 0}, {0, h}, {w, h}}) {
        double wx, wy;
        screenToWorld(camX + px, camY + py, wx, wy);
        minX = std::min(minX, static_cast<int>(std::floor(wx / g.isoTile)));
        maxX = std::max(maxX, static_cast<int>(std::floor(wx / g.isoTile)));
        minY = std::min(minY, static_cast<int>(std::floor(wy / g.isoTile)));
        maxY = std::max(maxY, static_cast<int>(std::floor(wy / g.isoTile)));
    }
    minX = std::max(0, minX - 2);
    minY = std::max(0, minY - 2);
    maxX = std::min(g.width - 1, maxX + 2);
    maxY = std::min(g.height - 1, maxY + 2);
    for (int sum = minX + minY; sum <= maxX + maxY; ++sum) {
        for (int tx = std::max(minX, sum - maxY); tx <= std::min(maxX, sum - minY); ++tx) {
            const int ty = sum - tx;
            const auto& cell = g.at(tx, ty);
            const SpriteBank* bank = map_->bank(cell.bank);
            if (!bank) continue;
            const SpriteTexture* sprite = sprites_.get(*bank, cell.pattern, false);
            if (!sprite) continue;
            int sx, sy;
            worldToScreen(tx * g.isoTile + g.isoTile / 2, ty * g.isoTile + g.isoTile / 2, sx, sy);
            SDL_Rect dst{sx - camX + sprite->originX, sy - camY + sprite->originY, sprite->width, sprite->height};
            SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
        }
    }
}

// A bar over each wounded monster; the hero's are on the panel (panel.cpp).
void Game::renderHealthBars() {
    if (!rules_.healthBarsOverMonsters) return;
    // The one under the pointer shows its health in its name instead.
    const Actor* named = hoverOn_ ? hoveredActor() : nullptr;
    for (const auto& a : actors_) {
        if (!a.hostile() || !a.alive() || !a.active || a.hp == a.maxHp || &a == named) continue;
        int sx, sy;
        worldToScreen(static_cast<int>(a.character.x), static_cast<int>(a.character.y), sx, sy);
        const SDL_FPoint at = onScreen(sx, sy - 100);
        SDL_Rect back{static_cast<int>(at.x) - 20, static_cast<int>(at.y), 40, 5};
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderFillRect(renderer_, &back);
        SDL_Rect fill{back.x + 1, back.y + 1, static_cast<int>(38 * std::clamp(static_cast<double>(a.hp) / a.maxHp, 0.0, 1.0)), 3};
        SDL_SetRenderDrawColor(renderer_, 200, 30, 30, 255);
        SDL_RenderFillRect(renderer_, &fill);
    }
}

// Each judge cell is a world-space square, so it shows up as a diamond.
// Blocked cells are tinted red, cells with the unknown 0x8000 bit yellow.
void Game::renderJudge(int camX, int camY) {
    const auto& g = map_->ground();
    // Judge cells touching the view: invert the view corners.
    int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
    const int w = viewWidth(), h = viewHeight();
    for (const auto& [px, py] : {std::pair{0, 0}, {w, 0}, {0, h}, {w, h}}) {
        double wx, wy;
        screenToWorld(camX + px, camY + py, wx, wy);
        minX = std::min(minX, static_cast<int>(std::floor(wx / g.magX)));
        maxX = std::max(maxX, static_cast<int>(std::floor(wx / g.magX)));
        minY = std::min(minY, static_cast<int>(std::floor(wy / g.magY)));
        maxY = std::max(maxY, static_cast<int>(std::floor(wy / g.magY)));
    }
    std::vector<SDL_Vertex> vertices;
    for (int jy = minY; jy <= maxY; ++jy) {
        for (int jx = minX; jx <= maxX; ++jx) {
            const std::int16_t v = g.judgeAt((jx + 0.5) * g.magX, (jy + 0.5) * g.magY);
            if (v == 0) continue;
            const SDL_Color color = (v & 0x8000) ? SDL_Color{255, 220, 0, 140} : SDL_Color{255, 0, 0, 80};
            SDL_FPoint corners[4];
            const int wx[4] = {jx * g.magX, (jx + 1) * g.magX, (jx + 1) * g.magX, jx * g.magX};
            const int wy[4] = {jy * g.magY, jy * g.magY, (jy + 1) * g.magY, (jy + 1) * g.magY};
            for (int k = 0; k < 4; ++k) {
                int sx, sy;
                worldToScreen(wx[k], wy[k], sx, sy);
                corners[k] = {static_cast<float>(sx - camX), static_cast<float>(sy - camY)};
            }
            for (int k : {0, 1, 2, 0, 2, 3}) vertices.push_back({corners[k], color, {0, 0}});
        }
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    if (!vertices.empty()) SDL_RenderGeometry(renderer_, nullptr, vertices.data(), static_cast<int>(vertices.size()), nullptr, 0);
}

void Game::renderPlaceholder(double interpolation) {
    // Shown when no game data is available: a slowly pulsing background.
    const double t = (static_cast<double>(ticks_) + interpolation) / kTicksPerSecond;
    const auto shade = static_cast<Uint8>(32 + 16 * std::sin(t));
    SDL_SetRenderDrawColor(renderer_, shade / 2, shade / 3, shade, 255);
    SDL_RenderClear(renderer_);
}

}  // namespace sf

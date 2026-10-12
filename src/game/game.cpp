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

Game::Game(SDL_Renderer* renderer, const FileSystem* data, const std::string& startMap, Skin* skin)
    : renderer_(renderer), data_(data), skin_(skin), sprites_(renderer) {
    // Every level has a ground file; use those to enumerate maps. An open
    // content pack has one text file per map instead.
    for (const auto& file : data_->list("Map/Ground")) mapNames_.push_back(stem(file));
    if (mapNames_.empty()) {
        for (const auto& file : data_->list("Map")) {
            if (lower(file).ends_with(".txt")) mapNames_.push_back(stem(file));
        }
    }
    if (skin_) playerModel_ = skin_->character("Player/Male/Animation00");
    if (!playerModel_) playerModel_ = CharacterModel::load(*data_, "Player/Male/Animation00");
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
    indexScenarios();
    if (playerModel_) player_.emplace(&*playerModel_);
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
    control_ = {};
    script_.reset();
    triggers_.clear();
    pendingScenario_.reset();
    // TEMP flags last for one visit; the others persist.
    std::erase_if(flags_, [](const auto& f) { return f.first.first == ScenarioScript::kTemp; });
    if (!dir.empty()) readScenario(dir, control_, &script_);
    spawnActors();
    spawnGroundItems();
    placePlayer();
    dialogue_.reset();
    openShop_.reset();
    talkTarget_.reset();
    objectTarget_.reset();
    labels_.clear();
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

const CharacterModel* Game::model(const std::string& stem) {
    auto [it, inserted] = models_.try_emplace(stem);
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
        actor.character.x = p.x;
        actor.character.y = p.y;
        actor.character.direction = p.direction >= 0 && p.direction < 8 ? p.direction : 0;
        actor.hp = actor.maxHp = std::max(1, p.stats.hp);
        if (ai_ && actor.hostile()) actor.aiList = ai_->find(p.stats.ai);
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
        cameraX_ = cx - kScreenWidth / 2.0;
        cameraY_ = cy - kScreenHeight / 2.0;
    }
}

bool Game::tryMove(Character& who, double dx, double dy) { return sf::tryMove(map_->ground(), who.x, who.y, dx, dy); }

void Game::walkBy(int screenDx, int screenDy) {
    if (!player_) return;
    double dx, dy;
    screenToWorld(screenDx, screenDy, dx, dy);
    walkTarget_.emplace(player_->x + dx, player_->y + dy);
}

void Game::updateCamera() {
    if (!player_) return;
    int sx, sy;
    worldToScreen(static_cast<int>(player_->x), static_cast<int>(player_->y), sx, sy);
    cameraX_ = sx - kScreenWidth / 2.0;
    cameraY_ = sy - kScreenHeight / 2.0;
}

// The keys follow the manual's Shortcut Keys page (see docs/controls.md).
void Game::handleEvent(const SDL_Event& event) {
    if (optionsOpen_) {
        optionsEvent(event);
        return;
    }
    if (menuOpen_) {
        menuEvent(event);
        return;
    }
    if (dialogue_ && event.type != SDL_QUIT) {
        dialogueEvent(event);
        return;
    }
    switch (event.type) {
        case SDL_QUIT: quit_ = true; break;
        case SDL_KEYDOWN:
            if (event.key.repeat) break;
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    // Esc closes a window, or opens the Settings Menu when
                    // none is open.
                    if (helpOpen_) {
                        helpOpen_ = false;
                    } else if (openShop_) {
                        if (!hand_) closeShop();
                    } else if (showInventory_) {
                        closeInventory();
                    } else if (showStatus_) {
                        showStatus_ = false;
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
                    if (!openShop_) showStatus_ = !showStatus_;
                    break;
                case SDLK_i:
                    if (openShop_) {
                        closeShop();
                    } else if (showInventory_) {
                        closeInventory();
                    } else {
                        showInventory_ = true;
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
                case SDLK_PRINTSCREEN: screenshotPending_ = true; break;
                case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4:
                case SDLK_5: case SDLK_6: case SDLK_7: case SDLK_8: {
                    const int key = event.key.keysym.sym - SDLK_1;
                    useBelt(key % Inventory::kBeltWidth, key / Inventory::kBeltWidth);
                    break;
                }
                case SDLK_b: layMine(); break;
                case SDLK_p: notYet("Increased-power mode"); break;
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
                case SDLK_m: notYet("The magic window"); break;
                case SDLK_q: notYet("The mission list"); break;
                case SDLK_n: notYet("The map"); break;
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
            if (shopClick(event.button.x, event.button.y, event.button.button == SDL_BUTTON_RIGHT)) break;
            if (statusClick(event.button.x, event.button.y)) break;
            if (event.button.button == SDL_BUTTON_LEFT && partnerGaugeClick(event.button.x, event.button.y)) break;
            if (beltClick(event.button.x, event.button.y, event.button.button == SDL_BUTTON_RIGHT)) break;
            if (event.button.button == SDL_BUTTON_LEFT && mineButtonClick(event.button.x, event.button.y)) break;
            if (inventoryClick(event.button.x, event.button.y, event.button.button == SDL_BUTTON_RIGHT)) break;
            // Tab and a click give the companion an order.
            if (SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_TAB] &&
                partnerOrder(event.button.x, event.button.y, event.button.button == SDL_BUTTON_RIGHT)) {
                break;
            }
            if (event.button.button == SDL_BUTTON_RIGHT && map_ && player_) {
                if (spells_.knows(spells_.selected)) {
                    // The right button casts the chosen spell at the cursor.
                    double wx, wy;
                    screenToWorld(cameraX_ + event.button.x, cameraY_ + event.button.y, wx, wy);
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
            if (event.button.button == SDL_BUTTON_LEFT) mouseHeld_ = false;
            break;
        default: break;
    }
}

void Game::closeInventory() {
    showInventory_ = false;
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
    if (!map_ || menuOpen_ || optionsOpen_) return;  // the world waits while a menu is open
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
    runEvent(ScenarioScript::kEveryTick);
    for (auto& actor : actors_) {
        if (!actor.active) continue;
        if (actor.hostile() && player_) {
            if (actor.aiList >= 0) {
                tickEnemyAi(actor);
            } else {
                tickEnemy(actor);
            }
        }
        actor.character.tick();
    }
    if (!player_) return;
    tickPlayer();
    player_->tick();
    tickPartner();
    tickSpells();
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
    checkTriggers(false);
    updateCamera();
}

std::optional<std::size_t> Game::enemyAt(float screenX, float screenY) const {
    // Characters stand on their feet; aim at the middle of the body.
    std::optional<std::size_t> best;
    double bestDistance = INFINITY;
    for (std::size_t i = 0; i < actors_.size(); ++i) {
        const auto& a = actors_[i];
        if (!a.hostile() || !a.alive()) continue;
        int sx, sy;
        worldToScreen(static_cast<int>(a.character.x), static_cast<int>(a.character.y), sx, sy);
        const double dx = sx - cameraX_ - screenX, dy = sy - 40 - cameraY_ - screenY;
        const double d = std::hypot(dx, dy);
        if (inClickRange(dx, dy) && d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

void Game::tickPlayer() {
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
    if (current == chart::kPlayerPunch) {
        // The blow lands halfway through the swing.
        if (player_->frame() == 4 && attackTarget_) {
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
            if (target.kind == Settings::kEnemy) attackTarget_ = target.index;
            double wx, wy;
            screenToWorld(cameraX_ + lx, cameraY_ + ly, wx, wy);
            player_->faceTowards(wx - player_->x, wy - player_->y);
            player_->restart(chart::kPlayerPunch);
            return;
        }
        if (target.kind == Settings::kEnemy) {
            attackTarget_ = target.index;
            walkTarget_.reset();
        } else if (target.kind == Settings::kItem) {
            // Walking onto an item picks it up.
            walkTarget_.emplace(groundItems_[target.index].x, groundItems_[target.index].y);
        } else {
            double wx, wy;
            screenToWorld(cameraX_ + lx, cameraY_ + ly, wx, wy);
            walkTarget_.emplace(wx, wy);
        }
    }
    if (attackTarget_ && !actors_[*attackTarget_].alive()) attackTarget_.reset();
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
            if (!actors_[i].hostile() || !actors_[i].alive()) continue;
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
    if (keys[SDL_SCANCODE_LEFT]) kx -= 1;
    if (keys[SDL_SCANCODE_RIGHT]) kx += 1;
    if (keys[SDL_SCANCODE_UP]) ky -= 1;
    if (keys[SDL_SCANCODE_DOWN]) ky += 1;

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
        if (std::hypot(dx, dy) <= kMeleeRange) {
            player_->faceTowards(dx, dy);
            player_->restart(chart::kPlayerPunch);
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
    const double speed = running ? kRunSpeed : kWalkSpeed;
    const double length = std::hypot(dx, dy);
    if (length > 1.0) {
        const double step = std::min(speed, walkTarget_ ? length : speed);
        player_->faceTowards(dx, dy);
        if (tryMove(*player_, dx / length * step, dy / length * step)) {
            player_->play(running && player_->hasChart(chart::kPlayerRun) ? chart::kPlayerRun : chart::kWalk);
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
            if (enemy.stunTicks > 0) --enemy.stunTicks;
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
    // Table 11: the hero's level in the monster's element strengthens their
    // defence. Energy Shield changes the defence first (0x443cb0).
    int defense = stats[HeroStats::kDefense];
    if (spellOn(kEnergyShield)) defense = defense * magic_.power(kEnergyShield, spells_.castLevel(kEnergyShield)) / 100;
    hurtPlayer(damageToHero(magic_, s.attacks[k], s.element, playerElements(), defense, random_.next(3)), false, &enemy);
}

void Game::hurtEnemy(Actor& enemy, int damage, bool bySpell, bool byPartner) {
    if (!enemy.alive()) return;
    enemy.hp -= damage;
    if (enemy.hp <= 0) {
        enemy.hp = 0;
        enemy.state = Actor::State::Dead;
        enemy.character.restart(chart::kEnemyDeath);
        dropLoot(enemy);
        // The original counts how the hero kills monsters, to offer a class
        // (0x4134a0), and every kill trains the companion (0x413640).
        const auto& weapon = inventory_.equipped(EquipSlot::Weapon);
        if (!byPartner) career_.countKill(weapon ? std::optional(weapon->def->subtype) : std::nullopt, bySpell);
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
void Game::hurtPlayer(int damage, bool magic, Actor* attacker) {
    // Shields charge MP each time they work. The original charges the
    // chosen spell's cost at the shield's level; ours the shield's own.
    auto chargeShield = [&](int shield) {
        const int charged = rules_.shieldsCostTheirOwnMp || spells_.selected < 0 ? shield : spells_.selected;
        playerMp_ -= spellMpCost(magic_, charged, spells_.castLevel(shield));
        if (playerMp_ <= 0) {
            playerMp_ = 0;
            setSpellOn(shield, false);
        }
    };
    if (magic && spellOn(kMagicShield)) {
        damage = std::max(1, damage * (100 - magic_.power(kMagicShield, spells_.castLevel(kMagicShield))) / 100);
        if (damage >= 20) trainSpell(kMagicShield);
        chargeShield(kMagicShield);
        spawnEffect(11000241, player_->x, player_->y).followsPlayer = true;  // it takes the blow
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
        int back = damage * magic_.power(kCounterBurst, spells_.castLevel(kCounterBurst)) / 100;
        if (attacker->placement.stats.word7 == 100) back /= 2;
        if (damage >= 20) trainSpell(kCounterBurst);
        chargeShield(kCounterBurst);
        spawnEffect(11000251, player_->x, player_->y).followsPlayer = true;
        hurtEnemy(*attacker, std::max(1, back), true);
    }
    // A blow does not interrupt the player's own swing or a flinch already
    // playing, or a crowd of monsters could keep them from ever fighting back.
    if (!casting_ && player_->chart() != chart::kPlayerPunch && player_->chart() != chart::kPlayerHit) {
        player_->restart(chart::kPlayerHit);
        walkTarget_.reset();
    }
}

void Game::render(double interpolation) {
    if (map_) {
        renderMap();
    } else {
        renderPlaceholder(interpolation);
    }
    if (optionsOpen_) {
        renderOptions();
    } else if (menuOpen_) {
        renderMenu();
    }
    if (screenshotPending_) takeScreenshot();
    SDL_RenderPresent(renderer_);
}

void Game::renderMap() {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);

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

    // Objects and characters, back to front. Shadows go first so nothing
    // stands under one; characters draw their own shadows.
    std::vector<const Character*> characters;
    if (player_) characters.push_back(&*player_);
    if (partner_ && partner_->character.tint.a > 0) characters.push_back(&partner_->character);
    for (const auto& actor : actors_) {
        if (!actor.active) continue;
        int sx, sy;
        worldToScreen(static_cast<int>(actor.character.x), static_cast<int>(actor.character.y), sx, sy);
        if (sx - camX < -256 || sy - camY < -256 || sx - camX > kScreenWidth + 256 || sy - camY > kScreenHeight + 256) continue;
        characters.push_back(&actor.character);
    }
    // Spell effects are sorted in with them by where they stand, as the
    // original sorts its effects, so a tree in front hides a burst behind it.
    struct Drawable {
        double x, y;
        const Character* character;
        const Effect* effect;
    };
    std::vector<Drawable> drawables;
    for (const Character* c : characters) drawables.push_back({c->x, c->y, c, nullptr});
    for (const Effect& e : effects_) drawables.push_back({e.x, e.y, nullptr, &e});
    std::ranges::stable_sort(drawables, [](const Drawable& a, const Drawable& b) { return a.x + a.y < b.x + b.y; });

    // Semi-transparent Shadow off draws them solid black.
    const int shadowAlpha = settings_.seeThroughShadows ? 96 : 255;
    const auto& objects = map_->objects().objects;
    for (int pass = 0; pass < 2; ++pass) {
        const bool shadows = pass == 0;
        // Characters and effects not drawn yet this pass, back to front.
        std::vector<Drawable> pending = shadows ? std::vector<Drawable>{} : drawables;
        auto draw = [&](const Drawable& d) {
            if (d.character) d.character->draw(renderer_, sprites_, camX, camY, shadowAlpha);
            if (d.effect) renderEffect(*d.effect, camX, camY);
        };
        for (std::size_t i : drawOrder_) {
            const MapObject& o = objects[i];
            // Everyone this object hides goes first, even if someone nearer
            // the front is still waiting for a later object.
            for (auto it = pending.begin(); it != pending.end();) {
                if (o.hides(it->x, it->y)) {
                    draw(*it);
                    it = pending.erase(it);
                } else {
                    ++it;
                }
            }
            if (const auto* pieces = skin_ ? skin_->replacement(o) : nullptr) {
                if (!shadows && !map_->isShadowBank(o.bank)) skin_->renderObject(o, *pieces, camX, camY);
                continue;
            }
            if (map_->isShadowBank(o.bank) != shadows) continue;
            const SpriteBank* bank = map_->bank(o.bank);
            if (!bank) continue;
            int sx, sy;
            worldToScreen(o.x, o.y, sx, sy);
            sx -= camX;
            sy -= camY;
            if (sx < -1024 || sy < -1024 || sx > kScreenWidth + 1024 || sy > kScreenHeight + 1024) continue;
            const SpriteTexture* sprite = sprites_.get(*bank, o.pattern, false);
            if (!sprite) continue;
            SDL_Rect dst{sx + sprite->originX, sy + sprite->originY, sprite->width, sprite->height};
            if (dst.x + dst.w < 0 || dst.y + dst.h < 0 || dst.x >= kScreenWidth || dst.y >= kScreenHeight) continue;
            if (shadows) {
                SDL_SetTextureColorMod(sprite->texture, 0, 0, 0);
                SDL_SetTextureAlphaMod(sprite->texture, static_cast<Uint8>(shadowAlpha));
            }
            SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
        }
        for (const Drawable& d : pending) draw(d);
    }
    // Semi-transparent Player: a faint copy on top shows the hero through
    // walls; where nothing covers them it lands on itself and changes nothing.
    if (player_ && settings_.seeThroughPlayer) player_->draw(renderer_, sprites_, camX, camY, shadowAlpha, 96);
    renderSpells(camX, camY);
    if (showJudge_) renderJudge(camX, camY);
    renderHealthBars(camX, camY);
    renderLabels(camX, camY);
    renderShop();
    renderStatus();
    renderInventory();
    renderHud();
    renderPartnerGauge();
    renderDialogue();
    renderHelp();
}

// The original's ground: pre-drawn chips on a screen-aligned grid.
void Game::renderChips(int camX, int camY) {
    const auto& g = map_->ground();
    const int x0 = std::max(0, camX / g.chipWidth - 1);
    const int y0 = std::max(0, camY / g.chipHeight - 1);
    const int x1 = std::min(g.width, (camX + kScreenWidth) / g.chipWidth + 2);
    const int y1 = std::min(g.height, (camY + kScreenHeight) / g.chipHeight + 2);
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
    for (const auto& [px, py] : {std::pair{0, 0}, {kScreenWidth, 0}, {0, kScreenHeight}, {kScreenWidth, kScreenHeight}}) {
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

// Simple bars until the original status panel is drawn: the player's
// health in the corner and a bar over each wounded monster.
void Game::renderHealthBars(int camX, int camY) {
    auto bar = [&](int x, int y, int w, int h, double fraction, SDL_Color color = {200, 30, 30, 255}) {
        SDL_Rect back{x, y, w, h};
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderFillRect(renderer_, &back);
        SDL_Rect fill{x + 1, y + 1, static_cast<int>((w - 2) * std::clamp(fraction, 0.0, 1.0)), h - 2};
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, 255);
        SDL_RenderFillRect(renderer_, &fill);
    };
    for (const auto& a : actors_) {
        if (!a.hostile() || !a.alive() || a.hp == a.maxHp) continue;
        int sx, sy;
        worldToScreen(static_cast<int>(a.character.x), static_cast<int>(a.character.y), sx, sy);
        bar(sx - camX - 20, sy - camY - 100, 40, 5, static_cast<double>(a.hp) / a.maxHp);
    }
    if (player_ && !menuOpen_) {
        bar(8, kScreenHeight - 30, 120, 10, static_cast<double>(playerHp_) / playerMaxHp());
        bar(8, kScreenHeight - 18, 120, 10, static_cast<double>(playerMp_) / std::max(1, playerMaxMp()), {40, 70, 210, 255});
        // Experience towards the next level, in a thin blue bar underneath.
        const auto need = levels_.need(playerLevel_);
        const double fraction = need > 0 ? static_cast<double>(playerExperience_) / static_cast<double>(need) : 1.0;
        SDL_Rect back{8, kScreenHeight - 7, 120, 4};
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderFillRect(renderer_, &back);
        SDL_Rect fill{9, kScreenHeight - 6, static_cast<int>(118 * std::clamp(fraction, 0.0, 1.0)), 2};
        SDL_SetRenderDrawColor(renderer_, 70, 120, 230, 255);
        SDL_RenderFillRect(renderer_, &fill);
        drawText(renderer_, 134, kScreenHeight - 30, std::format("LV {}", playerLevel_), {255, 255, 255, 255});
    }
}

// Each judge cell is a world-space square, so it shows up as a diamond.
// Blocked cells are tinted red, cells with the unknown 0x8000 bit yellow.
void Game::renderJudge(int camX, int camY) {
    const auto& g = map_->ground();
    // Judge cells touching the view: invert the view corners.
    int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
    for (const auto& [px, py] : {std::pair{0, 0}, {kScreenWidth, 0}, {0, kScreenHeight}, {kScreenWidth, kScreenHeight}}) {
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

#include "game/game.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>

#include "core/log.h"
#include "formats/binary_reader.h"
#include "formats/open_content.h"
#include "logic/movement.h"
#include "platform/filesystem.h"

namespace sf {

namespace {

// Walking speed in world units per tick. Not yet taken from the original.
constexpr double kWalkSpeed = 70.0;

// Monsters take their health and attack from the scenario file. The
// player's stats and the damage formula are still placeholders: the player
// fights bare-handed and a monster's blow takes off its attack value.
constexpr int kPlayerMaxHp = 100;
constexpr int kPlayerDamage = 10;
constexpr double kEnemySpeed = 35.0;
constexpr double kMeleeRange = 220.0;      // world units between the two characters' feet
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

Game::Game(SDL_Renderer* renderer, const FileSystem* data, const std::string& startMap)
    : renderer_(renderer), data_(data), sprites_(renderer) {
    // Every level has a ground file; use those to enumerate maps. An open
    // content pack has one text file per map instead.
    for (const auto& file : data_->list("Map/Ground")) mapNames_.push_back(stem(file));
    if (mapNames_.empty()) {
        for (const auto& file : data_->list("Map")) {
            if (lower(file).ends_with(".txt")) mapNames_.push_back(stem(file));
        }
    }
    playerModel_ = CharacterModel::load(*data_, "Player/Male/Animation00");
    try {
        if (auto raw = data_->read("System/Game/Parameter/Control.aid")) {
            ai_ = AiControl::parse(*raw);
        } else if (auto text = data_->read("ai.txt")) {
            ai_ = open::parseAi(*text);
        }
    } catch (const FormatError& e) {
        log::warn("Monster AI: {}", e.what());
    }
    indexScenarios();
    if (playerModel_) player_.emplace(&*playerModel_);
    if (mapNames_.empty()) {
        log::warn("No maps found in the game data; showing a placeholder screen");
        return;
    }
    std::size_t start = 0;
    for (std::size_t i = 0; i < mapNames_.size(); ++i) {
        if (equalsIgnoreCase(mapNames_[i], startMap)) start = i;
    }
    loadMap(start);
}

void Game::loadMap(std::size_t index, const std::string& scenarioDir) {
    mapIndex_ = index;
    sprites_.clear();
    map_ = Map::load(*data_, mapNames_[index]);
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
    placePlayer();
    if (!script_) return;
    for (const auto& [id, value] : script_->tempFlags) flags_[{ScenarioScript::kTemp, id}] = value;
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
        if (auto loaded = CharacterModel::load(*data_, stem)) it->second = std::make_unique<CharacterModel>(std::move(*loaded));
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
        playerHp_ = kPlayerMaxHp;
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

void Game::handleEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_QUIT: quit_ = true; break;
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE: quit_ = true; break;
                case SDLK_PAGEDOWN:
                case SDLK_n:
                    if (!mapNames_.empty()) loadMap((mapIndex_ + 1) % mapNames_.size());
                    break;
                case SDLK_PAGEUP:
                case SDLK_p:
                    if (!mapNames_.empty()) loadMap((mapIndex_ + mapNames_.size() - 1) % mapNames_.size());
                    break;
                case SDLK_j: showJudge_ = !showJudge_; break;
                case SDLK_HOME:
                    if (map_) placePlayer();
                    break;
                default: break;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT) mouseHeld_ = true;
            break;
        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) mouseHeld_ = false;
            break;
        default: break;
    }
}

void Game::tick() {
    ++ticks_;
    if (!map_) return;
    for (auto& actor : actors_) {
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
            if (const auto change = ScriptRunner(*script_, flags_).run(t.sentence)) pendingScenario_.emplace(change->scenario, change->entry);
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
    loadMap(static_cast<std::size_t>(found - mapNames_.begin()), dir);
    // The player is the first of the four party members.
    for (const auto& e : target.entryPoints) {
        if (e.id != entry * 4 || !player_) continue;
        player_->x = e.x;
        player_->y = e.y;
        if (e.direction >= 0 && e.direction < 8) player_->direction = e.direction;
        log::info("Player enters at world ({}, {})", e.x, e.y);
        break;
    }
    checkTriggers(false);
    updateCamera();
}

std::optional<std::size_t> Game::enemyAt(float screenX, float screenY) const {
    // Characters stand on their feet; aim at the middle of the body.
    std::optional<std::size_t> best;
    double bestDistance = 48;
    for (std::size_t i = 0; i < actors_.size(); ++i) {
        const auto& a = actors_[i];
        if (!a.hostile() || !a.alive()) continue;
        int sx, sy;
        worldToScreen(static_cast<int>(a.character.x), static_cast<int>(a.character.y), sx, sy);
        const double d = std::hypot(sx - cameraX_ - screenX, sy - 40 - cameraY_ - screenY);
        if (d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

void Game::tickPlayer() {
    // A hit or a punch plays out before anything else happens.
    const int current = player_->chart();
    if (current == chart::kPlayerHit && !player_->finished()) return;
    if (current == chart::kPlayerPunch) {
        // The blow lands halfway through the swing.
        if (player_->frame() == 4 && attackTarget_) {
            Actor& target = actors_[*attackTarget_];
            if (target.alive() && distance(*player_, target.character) <= kMeleeRange * 1.2) hurtEnemy(target, kPlayerDamage);
        }
        if (!player_->finished()) return;
        player_->play(chart::kIdle);
    }

    // Holding the left button walks towards the cursor, or attacks the
    // monster under it, like the original.
    if (mouseHeld_) {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        float lx, ly;
        SDL_RenderWindowToLogical(renderer_, mx, my, &lx, &ly);
        attackTarget_ = enemyAt(lx, ly);
        if (attackTarget_) {
            walkTarget_.reset();
        } else {
            double wx, wy;
            screenToWorld(cameraX_ + lx, cameraY_ + ly, wx, wy);
            walkTarget_.emplace(wx, wy);
        }
    }
    if (attackTarget_ && !actors_[*attackTarget_].alive()) attackTarget_.reset();
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
    }

    // Keyboard movement in screen directions.
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    double kx = 0, ky = 0;
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) kx -= 1;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) kx += 1;
    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) ky -= 1;
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) ky += 1;

    double dx = 0, dy = 0;
    if (kx != 0 || ky != 0) {
        walkTarget_.reset();
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

    const double length = std::hypot(dx, dy);
    if (length > 1.0) {
        const double step = std::min(kWalkSpeed, walkTarget_ ? length : kWalkSpeed);
        player_->faceTowards(dx, dy);
        if (tryMove(*player_, dx / length * step, dy / length * step)) {
            player_->setAction(Action::Walk);
            if (walkTarget_ && length <= kWalkSpeed) walkTarget_.reset();
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
            if (!c.finished()) return;
            enemy.state = State::Chase;
            break;
        case State::Attack:
            if (c.frame() == c.frameCount() / 2 && distance(c, *player_) <= kMeleeRange * 1.2) {
                hurtPlayer(std::max(1, enemy.placement.stats.attack));
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

void Game::hurtEnemy(Actor& enemy, int damage) {
    if (!enemy.alive()) return;
    enemy.hp -= damage;
    if (enemy.hp <= 0) {
        enemy.hp = 0;
        enemy.state = Actor::State::Dead;
        enemy.character.restart(chart::kEnemyDeath);
    } else {
        enemy.state = Actor::State::Hit;
        enemy.character.restart(chart::kEnemyHit);
        enemy.action = -1;
        enemy.event = AiControl::kHit;
    }
}

void Game::hurtPlayer(int damage) {
    playerHp_ -= damage;
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
    player_->restart(chart::kPlayerHit);
    walkTarget_.reset();
}

void Game::render(double interpolation) {
    if (map_) {
        renderMap();
    } else {
        renderPlaceholder(interpolation);
    }
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
    } else {
        renderChips(camX, camY);
    }

    // Objects and characters, back to front. Shadows go first so nothing
    // stands under one; characters draw their own shadows.
    std::vector<const Character*> characters;
    if (player_) characters.push_back(&*player_);
    for (const auto& actor : actors_) {
        int sx, sy;
        worldToScreen(static_cast<int>(actor.character.x), static_cast<int>(actor.character.y), sx, sy);
        if (sx - camX < -256 || sy - camY < -256 || sx - camX > kScreenWidth + 256 || sy - camY > kScreenHeight + 256) continue;
        characters.push_back(&actor.character);
    }
    std::ranges::stable_sort(characters, [](const Character* a, const Character* b) { return a->x + a->y < b->x + b->y; });

    const auto& objects = map_->objects().objects;
    for (int pass = 0; pass < 2; ++pass) {
        const bool shadows = pass == 0;
        // Characters not drawn yet this pass, back to front.
        std::vector<const Character*> pending = shadows ? std::vector<const Character*>{} : characters;
        for (std::size_t i : drawOrder_) {
            const MapObject& o = objects[i];
            // Everyone this object hides goes first, even if someone nearer
            // the front is still waiting for a later object.
            for (auto it = pending.begin(); it != pending.end();) {
                if (o.hides((*it)->x, (*it)->y)) {
                    (*it)->draw(renderer_, sprites_, camX, camY);
                    it = pending.erase(it);
                } else {
                    ++it;
                }
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
                SDL_SetTextureAlphaMod(sprite->texture, 96);
            }
            SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
        }
        for (const Character* c : pending) c->draw(renderer_, sprites_, camX, camY);
    }
    if (showJudge_) renderJudge(camX, camY);
    renderHealthBars(camX, camY);
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
    auto bar = [&](int x, int y, int w, int h, double fraction) {
        SDL_Rect back{x, y, w, h};
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderFillRect(renderer_, &back);
        SDL_Rect fill{x + 1, y + 1, static_cast<int>((w - 2) * std::clamp(fraction, 0.0, 1.0)), h - 2};
        SDL_SetRenderDrawColor(renderer_, 200, 30, 30, 255);
        SDL_RenderFillRect(renderer_, &fill);
    };
    for (const auto& a : actors_) {
        if (!a.hostile() || !a.alive() || a.hp == a.maxHp) continue;
        int sx, sy;
        worldToScreen(static_cast<int>(a.character.x), static_cast<int>(a.character.y), sx, sy);
        bar(sx - camX - 20, sy - camY - 100, 40, 5, static_cast<double>(a.hp) / a.maxHp);
    }
    if (player_) bar(8, kScreenHeight - 18, 120, 10, static_cast<double>(playerHp_) / kPlayerMaxHp);
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

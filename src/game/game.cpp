#include "game/game.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>

#include "core/log.h"
#include "formats/binary_reader.h"
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
    // Every level has a ground file; use those to enumerate maps.
    for (const auto& file : data_->list("Map/Ground")) mapNames_.push_back(stem(file));
    playerModel_ = CharacterModel::load(*data_, "Player/Male/Animation00");
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

void Game::loadMap(std::size_t index) {
    mapIndex_ = index;
    sprites_.clear();
    map_ = Map::load(*data_, mapNames_[index]);
    drawOrder_.clear();
    if (!map_) return;

    const auto& objects = map_->objects().objects;
    drawOrder_.resize(objects.size());
    for (std::size_t i = 0; i < objects.size(); ++i) drawOrder_[i] = i;
    // Painter's order: things lower on screen are drawn later.
    std::ranges::stable_sort(drawOrder_, [&](std::size_t a, std::size_t b) {
        return objects[a].x + objects[a].y < objects[b].x + objects[b].y;
    });
    placePlayer();
    spawnActors();
}

// Several scenarios can share a map (the same place at different points in
// the story); until story progress exists, use the first one.
void Game::indexScenarios() {
    for (const auto& dir : data_->list("Scenario")) {
        auto raw = data_->read("Scenario/" + dir + "/Scenario.Mct");
        if (!raw) continue;
        try {
            const auto control = ScenarioControl::parse(*raw);
            std::string map = control.mapPath;
            const auto slash = map.find_last_of("\\/");
            if (slash != std::string::npos) map = map.substr(slash + 1);
            scenarioByMap_.emplace(lower(stem(map)), "Scenario/" + dir);
        } catch (const FormatError& e) {
            log::warn("Scenario {}: {}", dir, e.what());
        }
    }
    log::info("Indexed {} scenario maps", scenarioByMap_.size());
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
    const auto found = scenarioByMap_.find(lower(mapNames_[mapIndex_]));
    if (found == scenarioByMap_.end()) return;
    auto raw = data_->read(found->second + "/Scenario.Mct");
    if (!raw) return;
    ScenarioControl control;
    try {
        control = ScenarioControl::parse(*raw);
    } catch (const FormatError& e) {
        log::warn("{}: {}", found->second, e.what());
        return;
    }
    for (const auto& p : control.placements) {
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
        // Non-player characters wear what their animation file shows.
        actor.character.visibleLayers.clear();
        if (!m->animations.charts.empty() && !m->animations.charts[0].directions.empty()) {
            for (std::size_t i = 0; i < m->animations.charts[0].directions[0].layers.size(); ++i)
                actor.character.visibleLayers.push_back(static_cast<int>(i));
        }
        actors_.push_back(std::move(actor));
    }
    log::info("{}: {} \"{}\" with {} characters", mapNames_[mapIndex_], found->second, control.title, actors_.size());
}

// Puts the player in the middle of the map's scenery and points the camera at them.
void Game::placePlayer() {
    const auto& objects = map_->objects().objects;
    const auto& g = map_->ground();
    double cx = g.width * g.chipWidth / 2.0, cy = g.height * g.chipHeight / 2.0;
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
    const double cx = std::clamp(screenX, 0.0, static_cast<double>(g.width * g.chipWidth));
    const double cy = std::clamp(screenY, 0.0, static_cast<double>(g.height * g.chipHeight));
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

// Moves the player by a world-space step unless that would leave walkable
// ground. A blocked diagonal step slides along whichever axis is still free,
// so walking into a wall at an angle follows the wall.
bool Game::tryMove(Character& who, double dx, double dy) {
    const auto& g = map_->ground();
    const double x = who.x, y = who.y;
    const bool xFree = g.walkable(x + dx, y), yFree = g.walkable(x, y + dy);
    // The diagonal needs both sides free, or it could slip between two
    // blocked cells that only touch at a corner.
    if (xFree && yFree && g.walkable(x + dx, y + dy)) {
        who.x += dx;
        who.y += dy;
        return true;
    }
    if (xFree && dx != 0) {
        who.x += dx;
        return true;
    }
    if (yFree && dy != 0) {
        who.y += dy;
        return true;
    }
    return false;
}

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
        if (actor.hostile() && player_) tickEnemy(actor);
        actor.character.tick();
    }
    if (!player_) return;
    tickPlayer();
    player_->tick();
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
        screenToWorld(kx * 0.15, ky * 0.10, dx, dy);
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

    // Ground chips: only the ones overlapping the view.
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
        std::size_t nextCharacter = shadows ? characters.size() : 0;
        for (std::size_t i : drawOrder_) {
            const MapObject& o = objects[i];
            while (nextCharacter < characters.size() && characters[nextCharacter]->x + characters[nextCharacter]->y < o.x + o.y) {
                characters[nextCharacter++]->draw(renderer_, sprites_, camX, camY);
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
        while (nextCharacter < characters.size()) characters[nextCharacter++]->draw(renderer_, sprites_, camX, camY);
    }
    if (showJudge_) renderJudge(camX, camY);
    renderHealthBars(camX, camY);
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
// Blocked cells are tinted red, exits yellow.
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

// Traps, effects, rain and story pictures that scenario scripts start, as
// ShadowFlare.exe runs them (see docs/traps.md).
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include "core/log.h"
#include "game/game.h"

namespace sf {

namespace {

// The companion's judge rectangle (+0x138, as partner.cpp's box).
constexpr JudgeRect kPartnerJudge{-80, -80, 80, 80};
// What a trap's hit chance is set to (0x43309b); the hero's evasion is
// taken off it, and hitChance keeps it within 20-98%.
constexpr int kTrapHit = 9999;

bool overlaps(double x, double y, const JudgeRect& a, double bx, double by, const JudgeRect& b) {
    return x + a.left <= bx + b.right && bx + b.left <= x + a.right && y + a.top <= by + b.bottom && by + b.top <= y + a.bottom;
}

}  // namespace

// Where a character stands: 0-3 are the players (the hero is 0 in single
// play), anything else a scenario id.
std::optional<std::pair<double, double>> Game::characterAt(std::int32_t id) const {
    if (id >= 0 && id < 4) {
        if (id == 0 && player_) return std::pair(player_->x, player_->y);
        return std::nullopt;
    }
    return positionOf(id);
}

// The frames of an effect's first chart in one direction (0x42b860 and
// 0x42a300 ask GetMaxFrameCount for the direction it faces).
int Game::directionFrames(int option, int direction) {
    const CharacterModel* model = effectModel(option);
    if (!model || model->animations.charts.empty()) return effectLength(option);
    const auto& dirs = model->animations.charts[0].directions;
    for (int d : {direction, 8, 0}) {
        if (d >= 0 && static_cast<std::size_t>(d) < dirs.size() && dirs[static_cast<std::size_t>(d)].frameCount > 0) {
            return dirs[static_cast<std::size_t>(d)].frameCount;
        }
    }
    return effectLength(option);
}

void Game::fireTrap(const ScriptHost::TrapFire& f) {
    const auto shot = trapShot(f.kind, f.direction, f.angle);
    if (!shot) {
        log::warn("CREATEOPTIONANGLE kind {} is not carried out", f.kind);
        return;
    }
    Effect& e = spawnEffect(shot->option, f.x, f.y);
    e.facing = shot->direction;
    e.height = f.height;
    // The engine's angles have world y down.
    e.angle = -f.angle;
    e.speed = f.speed;
    e.life = shot->lastsItsAnimation ? directionFrames(shot->option, shot->direction) : -1;
    e.trap = Effect::Trap{*shot, f.kind, f.power, f.fixedDamage};
}

// One of the original's ticks of a trap's shot (0x45e1e0): it hurts where
// it is, then moves on, stops at a wall, and ages.
void Game::stepTrap(Effect& e) {
    const TrapShot& shot = e.trap->shot;
    const bool hurts = e.age >= shot.hitFrom && (shot.hitTo < 0 || e.age <= shot.hitTo);
    if (hurts && player_) {
        const bool heroIn = playerHp_ > 0 && overlaps(e.x, e.y, shot.box, player_->x, player_->y, kHeroJudge);
        const bool partnerIn = partnerAlive() && partnerRecord_.active &&
                               overlaps(e.x, e.y, shot.box, partner_->character.x, partner_->character.y, kPartnerJudge);
        if (shot.hitsAll) {
            if (heroIn) trapHitsPlayer(e);
            if (partnerIn) trapHitsPartner(e);
        } else if (heroIn || partnerIn) {
            // Only the nearest one in the box is hurt, and the shot is spent.
            const double heroGap = heroIn ? std::hypot(player_->x - e.x, player_->y - e.y) : INFINITY;
            const double partnerGap = partnerIn ? std::hypot(partner_->character.x - e.x, partner_->character.y - e.y) : INFINITY;
            if (heroGap <= partnerGap) {
                trapHitsPlayer(e);
            } else {
                trapHitsPartner(e);
            }
            if (shot.diesOnHit) e.dead = true;
        }
    }
    if (e.speed > 0) {
        e.x += std::cos(e.angle) * e.speed;
        e.y += std::sin(e.angle) * e.speed;
    }
    if (shot.diesOnWall && (!map_ || !map_->ground().walkable(e.x, e.y))) e.dead = true;
    ++e.age;
    if (e.life >= 0 && e.age >= e.life) e.dead = true;
}

// A trap's blow on the hero (0x443cb0 by way of 0x45e1e0): it must beat
// their evasion, then deals its power outright or, for traps that are not
// fixed, as a monster's blow with no element would. The sparks of the hit
// stay on the hero.
void Game::trapHitsPlayer(const Effect& e) {
    const HeroStats stats = playerStats();
    if (random_.next(100) >= hitChance(kTrapHit, stats[HeroStats::kEvasion])) return;
    int damage = e.trap->power;
    if (!e.trap->fixedDamage) {
        int defense = stats[HeroStats::kDefense];
        if (power_.active()) defense = defense * 12 / 10;
        damage = damageToHero(magic_, e.trap->power, -1, playerElements(), defense, random_.next(3));
    }
    if (const int option = trapHitOption(e.trap->kind, random_.next(4)); option > 0) {
        spawnEffect(option, player_->x, player_->y).followsPlayer = true;
    }
    hurtPlayer(std::max(1, damage), false, nullptr, false);
}

// The same blow on an ACTIVE companion, against its own evasion and
// defence (inferred: the exe hands the hit to the companion's own code).
void Game::trapHitsPartner(const Effect& e) {
    const PartnerStats& st = partner_->stats;
    if (random_.next(100) >= hitChance(kTrapHit, st.evasion)) return;
    const int damage = e.trap->fixedDamage ? e.trap->power : meleeDamage(e.trap->power, st.defense, random_.next(3));
    hurtPartner(std::max(1, damage));
}

// CREATEEFFECT and CREATEEFFECTCHAR (0x42b860): the effect plays once
// through in its direction, standing or staying on a character.
void Game::createScriptEffect(int number, double x, double y, int height, int direction, std::int32_t follows) {
    const int option = scriptEffectOption(number);
    if (option <= 0) {
        log::warn("Script effect {} is not carried out", number);
        return;
    }
    Effect& e = spawnEffect(option, x, y);
    e.facing = direction;
    e.height = height;
    e.follows = follows;
    e.life = directionFrames(option, direction);
}

// RESSURECT (0x45a140): a dead enemy is back where the script says, at
// full health, facing `direction`, and counts as alive again.
void Game::resurrect(std::int32_t enemy, int x, int y, int direction) {
    Actor* a = actorById(enemy);
    if (!a || !a->hostile() || a->alive()) return;
    a->hp = a->maxHp;
    a->state = Actor::State::Idle;
    a->action = -1;
    a->actionTicks = 0;
    a->event = AiControl::kDecide;
    a->stunTicks = 0;
    a->struck = false;
    a->character.x = x;
    a->character.y = y;
    if (direction >= 0 && direction < 8) a->character.direction = direction;
    a->character.restart(chart::kIdle);
    log::info("Monster {} is back", enemy);
}

// SETTINGRAIN r g b count (0x41fe20): `count` new drops, each a short
// line falling from above the top of the screen, a little to the left of
// straight down.
void Game::addRain(int r, int g, int b, int count) {
    for (int i = 0; i < count; ++i) {
        RainDrop d;
        d.x = random_.next(1000) - 180;
        d.y = -30;
        d.speed = random_.next(1000) + 2000;
        d.length = random_.next(10) + 5;
        d.brightness = random_.next(700) + 300;
        d.angle = 4.712388000000001 - 3.141592 / (random_.next(10) + 10);
        d.r = static_cast<Uint8>(r);
        d.g = static_cast<Uint8>(g);
        d.b = static_cast<Uint8>(b);
        rain_.push_back(d);
    }
}

void Game::tickRain() {
    // A drop moves speed / 100 pixels an original tick, 30 a second.
    rainCarry_ += 30.0 / kTicksPerSecond;
    const int steps = static_cast<int>(rainCarry_);
    rainCarry_ -= steps;
    for (auto& d : rain_) d.age += steps;
    std::erase_if(rain_, [](const RainDrop& d) { return d.y - std::sin(d.angle) * d.speed * d.age / 100 >= 479; });
}

void Game::renderRain() {
    if (rain_.empty()) return;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    for (const auto& d : rain_) {
        const double c = std::cos(d.angle), s = std::sin(d.angle);
        const double travelled = static_cast<double>(d.speed) * d.age / 100;
        const int x0 = d.x + static_cast<int>(c * travelled), y0 = d.y - static_cast<int>(s * travelled);
        const int x1 = x0 + static_cast<int>(c * d.length), y1 = y0 - static_cast<int>(s * d.length);
        SDL_SetRenderDrawColor(renderer_, d.r, d.g, d.b, static_cast<Uint8>(std::clamp(d.brightness, 0, 1000) * 255 / 1000));
        SDL_RenderDrawLine(renderer_, x0, y0, x1, y1);
    }
}

namespace {

std::string visualPath(int picture) {
    return picture == 0 ? std::string("System/Common/Pattern/Waiting") : std::format("System/Common/Pattern/Visual{:02d}", picture);
}

constexpr double kVisualFadeTicks = 120;  // original ticks
constexpr double kVisualTicks = 300;

}  // namespace

// A picture the content does not have (the open pack has none yet) is
// passed over rather than leaving a black screen to click through.
void Game::startVisual(int picture) {
    if (!itemBank(visualPath(picture))) {
        log::warn("Story picture {} is not in the content", picture);
        return;
    }
    visual_ = Visual{picture, 0, 0};
    walkTarget_.reset();
}

void Game::tickVisual() {
    if (!visual_) return;
    if (visual_->ticks < kVisualTicks) visual_->ticks = std::min(kVisualTicks, visual_->ticks + 30.0 / kTicksPerSecond);
}

// Once a page has shown its 300 ticks, a click or a key turns to the next;
// after the last, the game comes back. Until then the input waits.
bool Game::visualEvent(const SDL_Event& event) {
    if (event.type == SDL_QUIT) return false;
    const bool press = event.type == SDL_MOUSEBUTTONDOWN || (event.type == SDL_KEYDOWN && !event.key.repeat);
    if (!press && event.type != SDL_MOUSEBUTTONUP && event.type != SDL_KEYUP && event.type != SDL_MOUSEMOTION) return false;
    if (!press || visual_->ticks < kVisualTicks) return true;
    const SpriteBank* bank = itemBank(visualPath(visual_->picture));
    const int pages = bank ? static_cast<int>(bank->patterns.size()) : 0;
    if (visual_->picture != 0 && visual_->page + 1 < pages) {
        ++visual_->page;
        visual_->ticks = 0;
    } else {
        visual_.reset();
    }
    return true;
}

void Game::renderVisual() {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    const SpriteBank* bank = itemBank(visualPath(visual_->picture));
    // The loading picture is Waiting.njp's pattern 4.
    const int pattern = visual_->picture == 0 ? 4 : visual_->page;
    const SpriteTexture* sprite = bank ? sprites_.get(*bank, pattern, true) : nullptr;
    const int bright = static_cast<int>(std::min(1.0, visual_->ticks / kVisualFadeTicks) * 255);
    if (sprite) {
        SDL_SetTextureColorMod(sprite->texture, static_cast<Uint8>(bright), static_cast<Uint8>(bright), static_cast<Uint8>(bright));
        SDL_SetTextureAlphaMod(sprite->texture, 255);
        const SDL_Rect dst{sprite->originX, sprite->originY, sprite->width, sprite->height};
        SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
        SDL_SetTextureColorMod(sprite->texture, 255, 255, 255);
    }
    // The waiting arrows at the bottom right step along every 15 ticks.
    if (visual_->ticks >= kVisualTicks) {
        const int step = static_cast<int>(ticks_ * 30 / kTicksPerSecond) % 15;
        const int dx = step < 5 ? 0 : step < 10 ? 8 : 16;
        if (const SpriteBank* icon = itemBank("System/Common/Pattern/WaitIcon")) {
            if (const SpriteTexture* s = sprites_.get(*icon, 0, false)) {
                const SDL_Rect dst{590 + dx + s->originX, 440 + s->originY, s->width, s->height};
                SDL_RenderCopy(renderer_, s->texture, nullptr, &dst);
            }
        }
    }
}

}  // namespace sf

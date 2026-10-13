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
constexpr int kPartnerHitChart = 3;  // the companion's hit chart (partner.cpp)

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
    e.trap = Effect::Trap{*shot, f.kind, f.power, f.fixedDamage, f.stun};
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
    // Shots that stop at walls (0x45e1e0 by way of 0x4145b0 and
    // 0x414990): one whose box already meets a wall is spent; otherwise
    // it moves on and is spent where its path first meets one.
    const bool walls = shot.diesOnWall && map_;
    const auto blocked = [&](double x, double y) {
        return shotMeetsWall(map_->ground(), static_cast<int>(x), static_cast<int>(y), shot.box) || boxMeetsObject(x, y, shot.box);
    };
    if (walls && blocked(e.x, e.y)) e.dead = true;
    if (e.speed > 0) {
        const double dx = std::cos(e.angle) * e.speed, dy = std::sin(e.angle) * e.speed;
        // Steps no longer than a quarter of a judge cell, so a thin wall is
        // not jumped.
        const int steps = walls ? std::max(1, static_cast<int>(std::ceil(e.speed / 40.0))) : 1;
        for (int i = 1; i <= steps; ++i) {
            const double nx = e.x + dx / steps, ny = e.y + dy / steps;
            if (walls && !e.dead && blocked(nx, ny)) {
                e.dead = true;
                break;
            }
            e.x = nx;
            e.y = ny;
        }
    }
    ++e.age;
    if (e.life >= 0 && e.age >= e.life) e.dead = true;
}

// A trap's blow on the hero (0x443cb0 by way of 0x45e1e0): it must beat
// their evasion, then deals its power outright or, for traps that are not
// fixed, as a monster's blow with no element would. The sparks of the hit
// stay on the hero, and the blow may stun them.
void Game::trapHitsPlayer(const Effect& e) {
    const HeroStats stats = playerStats();
    if (random_.next(100) >= hitChance(kTrapHit, stats[HeroStats::kEvasion])) return;
    playSound(e.trap->shot.hitSound, e.x, e.y);
    int damage = e.trap->power;
    if (!e.trap->fixedDamage) {
        int defense = stats[HeroStats::kDefense];
        if (power_.active()) defense = defense * 12 / 10;
        damage = damageToHero(magic_, e.trap->power, -1, playerElements(), defense, random_.next(3));
    }
    damage = std::max(1, damage);
    if (const int option = trapHitOption(e.trap->kind, random_.next(4)); option > 0) {
        spawnEffect(option, player_->x, player_->y).followsPlayer = true;
    }
    hurtPlayer(damage, false, nullptr, false);
    if (playerHp_ <= 0 || !player_) return;
    // The armour and accessories worn resist it (0x450d90); modifier 16
    // keeps the hero from being pushed.
    const auto stun = trapStun(e, player_->x, player_->y, inventory_.guardModifier(ItemDef::kModStunChance),
                               inventory_.guardModifier(ItemDef::kModStunLength), inventory_.guardModifier(ItemDef::kModHighDamage) != 0,
                               damage, playerMaxHp());
    if (stun) {
        log::info("The hero is stunned for {} ticks{}", stun->total, stun->pushed ? " and pushed back" : "");
        heroStun_ = stun;
        casting_.reset();
        walkTarget_.reset();
        attackTarget_.reset();
        if (stun->pushed) player_->direction = directionOfAngle(stun->angle - 3.141592);
        player_->restart(chart::kPlayerHit, static_cast<double>(std::max(1, player_->frameCount())) / stun->total * 30 / kTicksPerSecond);
    }
    trapSplash(e, player_->x, player_->y, true);
}

// The same blow on an ACTIVE companion (its own hit code, 0x45f9f0): the
// same hit roll and damage rule against its evasion and defense, and the
// same stun, which its gear does not resist.
void Game::trapHitsPartner(const Effect& e) {
    Partner& p = *partner_;
    const PartnerStats& st = p.stats;
    if (random_.next(100) >= hitChance(kTrapHit, st.evasion)) return;
    playSound(e.trap->shot.hitSound, e.x, e.y);
    const int damage = std::max(1, e.trap->fixedDamage ? e.trap->power : meleeDamage(e.trap->power, st.defense, random_.next(3)));
    hurtPartner(damage);
    if (!partnerAlive()) return;
    if (const auto stun = trapStun(e, p.character.x, p.character.y, 0, 0, false, damage, st.maxHp)) {
        p.stun = stun;
        p.state = Partner::State::Hit;
        Character& c = p.character;
        if (stun->pushed) c.direction = directionOfAngle(stun->angle - 3.141592);
        const int hit = c.hasChart(kPartnerHitChart) ? kPartnerHitChart : chart::kIdle;
        c.restart(hit);
        c.restart(hit, static_cast<double>(std::max(1, c.frameCount())) / stun->total * 30 / kTicksPerSecond);
    }
    trapSplash(e, p.character.x, p.character.y, false);
}

// The stun a trap's blow rolls (0x443cb0, 0x45f9f0). A chance or length
// of -1 comes from table 25, by how much of their health the blow took.
std::optional<Game::Stun> Game::trapStun(const Effect& e, double x, double y, int chanceResist, int lengthResist, bool gearSteady,
                                         int damage, int maxHp) {
    const auto& s = e.trap->stun;
    int chance = s.chance, length = s.length;
    if (chance == -1 || length == -1) {
        const int row = std::min(damage * 50 / std::max(1, maxHp), 49);
        const auto* t = tables_.find(25);
        const bool has = t && t->rows > row && t->cols > 1;
        if (chance == -1) chance = has ? t->value(row, 0) : 0;
        if (length == -1) length = has ? t->value(row, 1) : 0;
    }
    const auto ticks = stunTicks(chance, length, s.steady, chanceResist, lengthResist, random_.next(100));
    if (!ticks) return std::nullopt;
    Stun stun;
    stun.total = *ticks;
    stun.angle = std::atan2(-(y - e.y), x - e.x);
    stun.pushed = !s.steady && !gearSteady;
    return stun;
}

// One engine tick of a stun: the original's ticks it covers each push the
// one hit away from the blow, less each time, as far as the ground lets
// them. False once it is over.
bool Game::tickStun(Character& who, Stun& stun) {
    for (stun.carry += 30.0 / kTicksPerSecond; stun.carry >= 1.0 && stun.tick < stun.total; stun.carry -= 1.0) {
        if (const int push = stunPush(stun.tick, stun.total); stun.pushed && push > 0) {
            tryMove(who, std::cos(stun.angle) * push, -std::sin(stun.angle) * push);
        }
        ++stun.tick;
    }
    return stun.tick < stun.total;
}

// Hit word 0x48: one hit in five splashes 11000024 or 11000025 on the one
// hit, facing a random direction (0x443cb0, 0x45f9f0).
void Game::trapSplash(const Effect& e, double x, double y, bool onHero) {
    if (!e.trap->stun.splash || random_.next(100) >= 20) return;
    const int option = 11000024 + random_.next(2);
    Effect& splash = spawnEffect(option, x, y);
    splash.facing = random_.next(8);
    splash.followsPlayer = onHero;
    splash.life = directionFrames(option, splash.facing);
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

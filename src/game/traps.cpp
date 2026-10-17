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

// What a trap's hit chance is set to (0x43309b); the hero's evasion is
// taken off it, and hitChance keeps it within 20-98%.
constexpr int kTrapHit = 9999;
constexpr int kPartnerHitChart = 3;  // the companion's hit chart (partner.cpp)

}  // namespace

// Where a character stands: 0-3 are the players (the hero is 0 in single
// play), anything else a scenario id.
// The frames of an effect's first chart in one direction (0x42b860 and
// 0x42a300 ask GetMaxFrameCount for the direction it faces).
void Game::fireTrap(const ScriptHost::TrapFire& f) {
    const auto shot = trapShot(f.kind, f.direction, f.angle);
    if (!shot) {
        log::warn("CREATEOPTIONANGLE kind {} is not carried out", f.kind);
        return;
    }
    Effect& e = world_.spawnEffect(shot->option, f.x, f.y);
    e.facing = shot->direction;
    e.height = f.height;
    // The engine's angles have world y down.
    e.angle = -f.angle;
    e.speed = f.speed;
    e.life = shot->lastsItsAnimation ? world_.directionFrames(shot->option, shot->direction) : -1;
    e.trap = Effect::Trap{*shot, f.kind, f.power, f.fixedDamage, f.stun};
    // Kind 0 is the plain arrow: its flame and frost layers are turned off
    // once it is made (0x42a300), as for a bowgun's.
    e.hiddenLayers = shotHiddenLayers(f.kind);
}

// One of the original's ticks of a trap's shot (0x45e1e0): it hurts where
// it is, then moves on, stops at a wall, and ages.
void Game::stepTrap(Effect& e) {
    const TrapShot& shot = e.trap->shot;
    const bool hurts = e.age >= shot.hitFrom && (shot.hitTo < 0 || e.age <= shot.hitTo);
    if (hurts && player_) {
        const bool heroIn = playerHp_ > 0 && judgesOverlap(e.x, e.y, shot.box, player_->x, player_->y, kHeroJudge);
        const bool partnerIn = partnerAlive() && partnerRecord_.active &&
                               judgesOverlap(e.x, e.y, shot.box, partner_->character.x, partner_->character.y, kPartnerJudge);
        const bool thrown = e.trap->thrower >= 0;
        const auto hitHero = [&] { thrown ? shotHitsPlayer(e) : trapHitsPlayer(e); };
        const auto hitPartner = [&] { thrown ? shotHitsPartner(e) : trapHitsPartner(e); };
        if (shot.hitsAll) {
            if (heroIn) hitHero();
            if (partnerIn) hitPartner();
        } else if (heroIn || partnerIn) {
            // Only the nearest one in the box is hurt, and the shot is spent.
            const double heroGap = heroIn ? std::hypot(player_->x - e.x, player_->y - e.y) : INFINITY;
            const double partnerGap = partnerIn ? std::hypot(partner_->character.x - e.x, partner_->character.y - e.y) : INFINITY;
            if (heroGap <= partnerGap) {
                hitHero();
            } else {
                hitPartner();
            }
            if (shot.diesOnHit) e.dead = true;
        }
    }
    // Shots that stop at walls: one whose box already meets a wall is
    // spent; otherwise it moves on and is spent where its path first meets
    // one.
    moveShot(e, shot.box, shot.diesOnWall);
    ++e.age;
    if (e.life >= 0 && e.age >= e.life) e.dead = true;
    // Ours: a monster's shot that misses is gone once it leaves the map's
    // judge grid (the original would fly it on into nothing).
    if (e.trap->thrower >= 0 && world_.map) {
        const auto& g = world_.map->ground();
        const double left = (g.judgeX - 1) * g.magX, top = (g.judgeY - 1) * g.magY;
        const double right = (g.judgeX + g.judgeWidth + 1) * g.magX, bottom = (g.judgeY + g.judgeHeight + 1) * g.magY;
        if (e.x < left || e.x > right || e.y < top || e.y > bottom) e.dead = true;
    }
}

// The stone or arrow leaves the monster's hand on its attack's striking
// frame (0x45a2f0, handed to 0x42fdc0 and the shot controllers 0x429ec0
// and 0x42a300): it starts `shotStart` out from the monster along the
// line to where the hero stood as the attack began, `shotHeight` up, and
// flies `shotSpeed` a tick until it hits the hero, the companion or a wall.
// It carries the attack's power, hit and the monster's element.
void Game::throwShot(Actor& enemy, int slot) {
    const auto& st = enemy.placement.stats;
    const auto k = static_cast<std::size_t>(std::clamp(slot, 0, 2));
    // The original's angles have world y up, the engine's down.
    const double up = -enemy.aim;
    const auto shot = trapShot(st.shotKind, 8, up);
    if (!shot) {
        log::warn("Monster {} shoots kind {}, which is not carried out", enemy.gameId(), st.shotKind);
        return;
    }
    const double x = enemy.character.x + std::trunc(std::cos(up) * st.shotStart);
    const double y = enemy.character.y - std::trunc(std::sin(up) * st.shotStart);
    Effect& e = world_.spawnEffect(shot->option, x, y);
    e.facing = shot->direction;
    e.height = st.shotHeight;
    e.angle = enemy.aim;
    e.speed = st.shotSpeed;
    e.life = -1;
    e.hiddenLayers = shotHiddenLayers(st.shotKind);
    Effect::Trap t;
    t.shot = *shot;
    t.kind = st.shotKind;
    t.power = st.attacks[k];
    t.thrower = static_cast<int>(&enemy - world_.actors.data());
    t.hit = st.hits[k];
    t.element = st.element;
    e.trap = t;
    log::info("Monster {} throws a kind {} shot", enemy.gameId(), st.shotKind);
}

// A monster's shot on the hero: the blow of the attack that threw it, as
// strikePlayer deals one (0x443cb0 by way of 0x45e1e0), with the shot's
// spark and sound where it lands, and a fire or ice arrow's effect.
void Game::shotHitsPlayer(const Effect& e) {
    const HeroStats stats = playerStats();
    if (world_.random.next(100) >= hitChance(e.trap->hit, stats[HeroStats::kEvasion])) {
        world_.showMiss(player_->x, player_->y);
        return;
    }
    playSound(e.trap->shot.hitSound, e.x, e.y);
    int defense = stats[HeroStats::kDefense];
    if (power_.active()) defense = defense * 12 / 10;
    if (spellOn(kEnergyShield)) defense = defense * magic_.power(kEnergyShield, castLevel(kEnergyShield)) / 100;
    if (const int option = scriptEffectOption(monsterShotSpark(e.trap->kind, world_.random.next(4))); option > 0) {
        world_.spawnEffect(option, player_->x, player_->y).followsPlayer = true;
    }
    if (const int option = scriptEffectOption(monsterShotEffect(e.trap->kind)); option > 0) {
        world_.spawnEffect(option, player_->x, player_->y).followsPlayer = true;
    }
    Actor* thrower = static_cast<std::size_t>(e.trap->thrower) < world_.actors.size() ? &world_.actors[static_cast<std::size_t>(e.trap->thrower)] : nullptr;
    const int damage = damageToHero(magic_, e.trap->power, e.trap->element, playerElements(), defense, world_.random.next(3));
    hurtPlayer(damage, false, thrower);
    // An ice arrow (kind 5) freezes the hero it stuns and a stone shot
    // (kind 7) turns them to stone (hit word 3 set to 1 or 2, 0x45a2f0):
    // the stun from table 25 by the damage, which the worn gear resists,
    // capped at 15 ticks and pushing them back unless the gear is steady
    // (0x443cb0). A Warrior's combo or spin is not stopped.
    const int kind = e.trap->kind;
    if ((kind != 5 && kind != 7) || playerHp_ <= 0 || !player_ || heroSteadyInCombo()) return;
    const int row = std::min(damage * 50 / std::max(1, playerMaxHp()), 49);
    const auto* t = tables_.find(25);
    if (!t || t->rows <= row || t->cols < 2) return;
    const auto ticks = stunTicks(t->value(row, 0), t->value(row, 1), false, inventory_.guardModifier(ItemDef::kModStunChance),
                                 inventory_.guardModifier(ItemDef::kModStunLength), world_.random.next(100));
    if (!ticks) return;
    Stun stun;
    stun.total = *ticks;
    stun.angle = std::atan2(-(player_->y - e.y), player_->x - e.x);
    stun.pushed = inventory_.guardModifier(ItemDef::kModHighDamage) == 0;
    stunHero(stun);
    if (kind == 5) {
        iceOnHero(stun.total);
    } else {
        // Stone: grey, and held on the hit pose's first frame till the
        // stun ends (flag 0x4000).
        player_->pose(chart::kPlayerHit, 0);
        player_->grey = player_->held = true;
    }
}

// The same shot on an ACTIVE companion, as strikePartner's blow.
void Game::shotHitsPartner(const Effect& e) {
    const PartnerStats& st = partner_->stats;
    if (world_.random.next(100) >= hitChance(e.trap->hit, st.evasion)) {
        world_.showMiss(partner_->character.x, partner_->character.y);
        return;
    }
    playSound(e.trap->shot.hitSound, e.x, e.y);
    hurtPartner(spellDamage(e.trap->power, e.trap->element, st.element, st.defense, world_.random.next(3), world_.random.next(4)));
}

// A trap's blow on the hero (0x443cb0 by way of 0x45e1e0): it must beat
// their evasion, then deals its power outright or, for traps that are not
// fixed, as a monster's blow with no element would. The sparks of the hit
// stay on the hero, and the blow may stun them.
void Game::trapHitsPlayer(const Effect& e) {
    const HeroStats stats = playerStats();
    if (world_.random.next(100) >= hitChance(kTrapHit, stats[HeroStats::kEvasion])) {
        world_.showMiss(player_->x, player_->y);
        return;
    }
    playSound(e.trap->shot.hitSound, e.x, e.y);
    int damage = e.trap->power;
    if (!e.trap->fixedDamage) {
        int defense = stats[HeroStats::kDefense];
        if (power_.active()) defense = defense * 12 / 10;
        damage = damageToHero(magic_, e.trap->power, -1, playerElements(), defense, world_.random.next(3));
    }
    damage = std::max(1, damage);
    if (const int option = trapHitOption(e.trap->kind, world_.random.next(4)); option > 0) {
        world_.spawnEffect(option, player_->x, player_->y).followsPlayer = true;
    }
    hurtPlayer(damage, false, nullptr, false);
    if (playerHp_ <= 0 || !player_) return;
    // Nothing but a spell stops a Warrior's combo blows or spin (states
    // 11-14 ignore the stun, 0x443cb0).
    if (heroSteadyInCombo()) return;
    // The armour and accessories worn resist it (0x450d90); modifier 16
    // keeps the hero from being pushed.
    const auto stun = trapStun(e, player_->x, player_->y, inventory_.guardModifier(ItemDef::kModStunChance),
                               inventory_.guardModifier(ItemDef::kModStunLength), inventory_.guardModifier(ItemDef::kModHighDamage) != 0,
                               damage, playerMaxHp());
    if (stun) stunHero(*stun);
    trapSplash(e, player_->x, player_->y, true);
}

void Game::stunHero(const Stun& stun) {
    log::info("The hero is stunned for {} ticks{}", stun.total, stun.pushed ? " and pushed back" : "");
    heroStun_ = stun;
    casting_.reset();
    combo_.reset();
    player_->held = player_->grey = false;
    walkTarget_.reset();
    attackTarget_.reset();
    if (stun.pushed) player_->direction = directionOfAngle(stun.angle - 3.141592);
    player_->restart(chart::kPlayerHit, static_cast<double>(std::max(1, player_->frameCount())) / stun.total * 30 / kTicksPerSecond);
}

// The same blow on an ACTIVE companion (its own hit code, 0x45f9f0): the
// same hit roll and damage rule against its evasion and defense, and the
// same stun, which its gear does not resist.
void Game::trapHitsPartner(const Effect& e) {
    Partner& p = *partner_;
    const PartnerStats& st = p.stats;
    if (world_.random.next(100) >= hitChance(kTrapHit, st.evasion)) {
        world_.showMiss(p.character.x, p.character.y);
        return;
    }
    playSound(e.trap->shot.hitSound, e.x, e.y);
    const int damage = std::max(1, e.trap->fixedDamage ? e.trap->power : meleeDamage(e.trap->power, st.defense, world_.random.next(3)));
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
    const auto ticks = stunTicks(chance, length, s.steady, chanceResist, lengthResist, world_.random.next(100));
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
    if (!e.trap->stun.splash || world_.random.next(100) >= 20) return;
    const int option = 11000024 + world_.random.next(2);
    Effect& splash = world_.spawnEffect(option, x, y);
    splash.facing = world_.random.next(8);
    splash.followsPlayer = onHero;
    splash.life = world_.directionFrames(option, splash.facing);
}

// CREATEEFFECT and CREATEEFFECTCHAR (0x42b860): the effect plays once
// through in its direction, standing or staying on a character.
void Game::createScriptEffect(int number, double x, double y, int height, int direction, std::int32_t follows) {
    const int option = scriptEffectOption(number);
    if (option <= 0) {
        log::warn("Script effect {} is not carried out", number);
        return;
    }
    Effect& e = world_.spawnEffect(option, x, y);
    e.facing = direction;
    e.height = height;
    e.follows = follows;
    e.life = world_.directionFrames(option, direction);
}

// SETTINGRAIN r g b count (0x41fe20): `count` new drops, each a short
// line falling from above the top of the screen, a little to the left of
// straight down.
void Game::addRain(int r, int g, int b, int count) {
    for (int i = 0; i < count; ++i) {
        RainDrop d;
        d.x = world_.random.next(1000) - 180;
        d.y = -30;
        d.speed = world_.random.next(1000) + 2000;
        d.length = world_.random.next(10) + 5;
        d.brightness = world_.random.next(700) + 300;
        d.angle = 4.712388000000001 - 3.141592 / (world_.random.next(10) + 10);
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

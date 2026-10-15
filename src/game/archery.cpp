// Bowguns: the hero's shots and the hunter's volley in increased-power
// mode, as ShadowFlare.exe shoots them (see docs/archery.md).
#include <algorithm>
#include <cmath>
#include <format>

#include "core/log.h"
#include "game/game.h"
#include "logic/archery.h"

namespace sf {

namespace {

// Half the side of the square a monster can be hit in (spells.cpp uses the
// same; the original reads each one's judge rect).
constexpr double kTargetHalf = 50.0;
// Shots start 200 out from the hero (the spawn's word 0xe7) and fly 350 up
// (its word 9, the object's height +0x124).
constexpr double kShotStart = 200.0;
constexpr double kShotHeight = 350.0;
constexpr int kShotHitSound = 7;  // Voice00.Voc, as the trap arrows (0x42a300)

}  // namespace

bool Game::bowInHand() const {
    const auto& weapon = inventory_.equipped(EquipSlot::Weapon);
    return weapon && weapon->def->cls == ItemDef::Class::Weapon && weapon->def->subtype == ItemDef::kBowgun;
}

int Game::strikeFrame(int chart, int fallback) const {
    const CharacterModel* m = player_ ? player_->model() : nullptr;
    if (!m || chart < 0 || static_cast<std::size_t>(chart) >= m->animations.charts.size()) return fallback;
    const auto& dirs = m->animations.charts[static_cast<std::size_t>(chart)].directions;
    for (int d : {player_->direction, 0}) {
        if (d < 0 || static_cast<std::size_t>(d) >= dirs.size() || dirs[static_cast<std::size_t>(d)].layers.empty()) continue;
        const auto& cells = dirs[static_cast<std::size_t>(d)].layers[0];
        for (std::size_t f = 0; f < cells.size(); ++f) {
            if (cells[f].status & 0x40) return static_cast<int>(f);
        }
    }
    return fallback;
}

// The hero pulls the trigger (0x437fe0, state 0x14). A hunter in power mode
// sets off the volley one time in three (0x438a90) when any monster is near
// enough; it plays the same chart on a faster scale. The original turns to
// face the target and shoots from wherever the hero stands.
void Game::shoot(std::optional<std::size_t> target, double angle) {
    BowShot shot;
    shot.target = target;
    shot.angle = angle;
    const HeroStats stats = playerStats();
    const int attackSpeed = stats[HeroStats::kAttackSpeed];
    double speed = attackSpeedFactor(magic_, attackSpeed, power_.active(), overloaded(stats));
    if (power_.active() && career_.current == HeroClass::Hunter && random_.next(100) < kHunterVolleyChance) {
        // Every living monster whose position lies within 2000 either way.
        for (std::size_t i = 0; i < actors_.size() && shot.volleyTargets.size() < kHunterVolleyMost; ++i) {
            const Actor& a = actors_[i];
            if (!a.active || !a.hostile() || !a.alive()) continue;
            if (std::abs(a.character.x - player_->x) <= kHunterVolleyReach && std::abs(a.character.y - player_->y) <= kHunterVolleyReach) {
                shot.volleyTargets.push_back(i);
            }
        }
        // With none, it is an ordinary shot after all.
        if (!shot.volleyTargets.empty()) {
            shot.volley = true;
            speed = volleySpeedFactor(magic_, attackSpeed, true);
            log::info("The hunter's volley: {} arrows", shot.volleyTargets.size());
        }
    }
    bowShot_ = std::move(shot);
    // The open content's hero has no shooting chart yet: it swings instead.
    player_->restart(player_->hasChart(chart::kPlayerShoot) ? chart::kPlayerShoot : chart::kPlayerPunch, speed);
    blowLanded_ = false;
    swingTicks_ = 0;
}

// The shooting frame: the shots leave the bow. A bowgun with no durability
// left shoots nothing, though the hero still goes through the motion. One
// pull uses one point, whatever flies.
void Game::loose() {
    if (!bowShot_ || !player_) return;
    const auto& weapon = inventory_.equipped(EquipSlot::Weapon);
    if (!weapon || !inventory_.canShoot()) return;
    const ItemDef& def = *weapon->def;
    const HeroStats stats = playerStats();
    const BowShot& b = *bowShot_;

    if (b.volley) {
        // An arrow on each monster, each after its own wait, with the full
        // attack and hit rate (0x438a90, 0x42b9e0).
        for (std::size_t i : b.volleyTargets) {
            if (i >= actors_.size() || !actors_[i].alive()) continue;
            const Actor& a = actors_[i];
            Effect& e = spawnEffect(9000, a.character.x, a.character.y);
            e.life = -1;
            Effect::Shot s;
            s.power = std::max(1, stats[HeroStats::kAttack]);
            s.hit = stats[HeroStats::kHit];
            s.hitOption = kHunterVolleyHitOption;
            s.box = 5;
            s.target = static_cast<int>(i);
            s.targetX = a.character.x;
            s.targetY = a.character.y;
            s.wait = random_.next(kHunterVolleyWait);
            e.shot = s;
        }
    } else {
        // Aimed at the target where it stands now, else along the way the
        // hero faces. The original's angles have world y up.
        double angle = -b.angle;
        if (b.target && *b.target < actors_.size()) {
            const Character& t = actors_[*b.target].character;
            angle = std::atan2(-(t.y - player_->y), t.x - player_->x);
        }
        const BowVolley volley = bowVolley(def.shotKind, def.shotSpread, angle);
        Effect::Shot s;
        // Anyone but a hunter shoots with part of the attack (0x437fe0).
        s.power = std::max(1, stats[HeroStats::kAttack] * bowAttackPercent(career_.current, career_.history) / 100);
        s.hit = stats[HeroStats::kHit];
        // One spark for the whole pull.
        s.hitOption = bowHitOption(def.shotKind, random_.next(def.shotKind == static_cast<int>(ShotKind::Stone) ? 4 : 3));
        s.pierces = def.shotPierces;
        for (const BowArrow& arrow : volley.arrows) {
            const double x = player_->x + std::trunc(std::cos(arrow.angle) * kShotStart);
            const double y = player_->y - std::trunc(std::sin(arrow.angle) * kShotStart);
            Effect& e = spawnEffect(volley.option, x, y);
            e.life = -1;
            e.angle = -arrow.angle;  // the engine's angles have world y down
            e.speed = def.shotSpeed;
            e.facing = arrow.facing;
            e.height = kShotHeight;
            if (volley.option == 0) e.hiddenLayers = (volley.flame ? 0u : 1u << 2) | (volley.frost ? 0u : 1u << 3);
            e.hitSound = kShotHitSound;
            e.shot = s;
        }
    }
    sayBroken(inventory_.wearFromShot());
}

// One of the original's ticks of a shot (0x45e1e0): it strikes what is in
// its box, then stops at a wall, moves on and ages. The volley's arrows
// wait, then fall on their target over ten ticks and strike it as they
// land (0x42b9e0).
void Game::stepShot(Effect& e) {
    Effect::Shot& s = *e.shot;
    if (s.target >= 0) {
        if (s.wait > 0) {
            --s.wait;
            return;
        }
        ++s.fall;
        // It follows its target as it falls; one that is gone leaves the
        // arrow falling where it was.
        if (static_cast<std::size_t>(s.target) < actors_.size()) {
            const Actor& a = actors_[static_cast<std::size_t>(s.target)];
            if (a.alive() && a.active) {
                s.targetX = a.character.x;
                s.targetY = a.character.y;
            }
        }
        const FallingArrow f = fallingArrow(s.fall);
        e.x = s.targetX + f.dx;
        e.y = s.targetY + f.dy;
        e.height = f.height;
        if (s.fall == kFallTicks && static_cast<std::size_t>(s.target) < actors_.size()) {
            // It strikes only the monster it was loosed at.
            Actor& a = actors_[static_cast<std::size_t>(s.target)];
            if (a.alive() && a.active && std::abs(a.character.x - e.x) <= s.box + kTargetHalf &&
                std::abs(a.character.y - e.y) <= s.box + kTargetHalf) {
                shotStrikes(e, a);
            }
        }
        if (s.fall > kFallTicks) e.dead = true;
        ++e.age;
        return;
    }

    // Each monster in its box once; one that does not fly on is spent on
    // the nearest.
    std::size_t nearest = actors_.size();
    double best = 0;
    for (std::size_t i = 0; i < actors_.size(); ++i) {
        Actor& a = actors_[i];
        if (!a.active || !a.hostile() || !a.alive()) continue;
        if (std::ranges::find(e.struck, i) != e.struck.end()) continue;
        const double reach = s.box + kTargetHalf;
        if (std::abs(a.character.x - e.x) > reach || std::abs(a.character.y - e.y) > reach) continue;
        if (s.pierces) {
            e.struck.push_back(i);
            shotStrikes(e, a);
            continue;
        }
        const double d = std::hypot(a.character.x - e.x, a.character.y - e.y);
        if (nearest == actors_.size() || d < best) {
            nearest = i;
            best = d;
        }
    }
    if (nearest < actors_.size()) {
        shotStrikes(e, actors_[nearest]);
        e.dead = true;
    }
    const JudgeRect box{-s.box, -s.box, s.box, s.box};
    if (!e.dead) moveShot(e, box, true);
    // Ours: one that leaves the map's judge grid is gone (the original
    // would fly it on into nothing).
    if (map_) {
        const auto& g = map_->ground();
        const double left = (g.judgeX - 1) * g.magX, top = (g.judgeY - 1) * g.magY;
        const double right = (g.judgeX + g.judgeWidth + 1) * g.magX, bottom = (g.judgeY + g.judgeHeight + 1) * g.magY;
        if (e.x < left || e.x > right || e.y < top || e.y > bottom) e.dead = true;
    }
    ++e.age;
}

// A shot's blow on a monster: it must beat the monster's evasion, then does
// the hero's blow damage with the shot's attack (the monster's hit code by
// way of 0x45e1e0). The spark shows and the arrow's sound plays where it
// lands.
bool Game::shotStrikes(Effect& e, Actor& enemy) {
    const Effect::Shot& s = *e.shot;
    const auto& st = enemy.placement.stats;
    if (random_.next(100) >= hitChance(s.hit, st.evasion)) {
        showMiss(enemy.character.x, enemy.character.y);
        return false;
    }
    playSound(e.hitSound, e.x, e.y);
    if (s.hitOption > 0) spawnEffect(s.hitOption, enemy.character.x, enemy.character.y);
    const int damage = heroBlowDamage(magic_, s.power, playerElements(), st.element, st.defense, random_.next(3));
    absorbFrom(enemy, damage);
    hurtEnemy(enemy, damage);
    return true;
}

void Game::moveShot(Effect& e, const JudgeRect& box, bool walls) {
    walls = walls && map_;
    const auto blocked = [&](double x, double y) {
        return shotMeetsWall(map_->ground(), static_cast<int>(x), static_cast<int>(y), box) || boxMeetsObject(x, y, box);
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
}

}  // namespace sf

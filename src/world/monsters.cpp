// People and monsters on a tick: monster behaviour driven by the game's
// own AI lists (Control.aid), a simple chase for monsters without one,
// people's scripted actions and the cries monsters' animations call for.
//
// Each tick, the monster's latest event (an action finishing, being hit, a
// timer running) picks a new action from its AI list (selectAiEntry), and
// the action runs. Actions follow ShadowFlare.exe (dispatched at
// 0x459340); the movement and attack details are simplified.
#include <algorithm>
#include <array>
#include <cmath>
#include <climits>
#include <iterator>

#include "logic/ai_select.h"
#include "world/world.h"

namespace sf {

namespace {

constexpr double kPlayerBox = 80.0;  // half-size of the player's collision box, world units
// Monsters take their health, attack and defense from the scenario file.
// One with no AI list walks at 20 an original tick, the original's commonest
// approach speed in Control.aid (the hero walks at 20 too).
constexpr double kEnemySpeed = 20.0 * kOriginalTicksPerTick;

bool isAttack(int action) { return action >= AiControl::kAttack && action < AiControl::kStop; }

double distance(const Character& a, const Character& b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Each monster kind's (Character/ENEMY folder's) death cry, -1 for none
// (0x45bec0 reads it from DAT_004815d8).
constexpr int kDeathCry[] = {-1,  105, 104, 107, 106, 104, -1, -1,  114, 111, 118, 118, 117,
                             114, 133, 130, 148, 148, 142, -1, -1, 151, 111, 114, 136};

// The sounds monsters' animations call for (0x45a2a0): a frame with cue
// flag 0x400, 0x800 or 0x1000 (slot 0-2) plays the kind's sound for that
// slot and animation (DAT_00480a20), or else the one every kind shares
// (DAT_004809a8: 86 on the death animation, nothing else). Animations: 0
// stand, 1 walk, 2 hit, 3 death, 4-6 blows, 7-9 spells. Only the kinds
// that have any are listed.
struct MonsterCue {
    int kind, slot;
    std::array<int, 10> voice;
};
constexpr int N = -1;
constexpr MonsterCue kMonsterCues[] = {
    {3, 0, {N, N, N, N, N, N, N, 91, 91, N}},
    {3, 1, {90, 90, 90, 90, 90, 90, 90, 90, 90, N}},
    {4, 0, {N, 92, N, 87, N, N, N, N, N, N}},
    {4, 1, {89, 89, 89, 89, 89, 89, 89, 89, 89, N}},
    {7, 0, {N, 108, N, N, N, N, N, N, N, N}},
    {8, 0, {N, N, N, N, 113, 112, N, 112, N, 110}},
    {9, 0, {N, N, N, N, N, N, N, N, N, 110}},
    {12, 0, {N, N, N, N, 116, N, N, 115, 115, N}},
    {13, 0, {N, N, N, N, 113, 112, N, N, N, N}},
    {14, 0, {N, N, N, N, 131, N, N, N, N, N}},
    {15, 0, {N, N, N, N, 129, N, N, 129, N, N}},
    {16, 0, {N, N, N, N, 146, 146, N, N, N, N}},
    {16, 1, {N, N, N, N, N, 147, N, N, N, N}},
    {17, 0, {N, N, N, N, 123, N, N, 124, 124, N}},
    {18, 0, {N, N, N, N, 140, N, N, 141, N, N}},
    {19, 0, {N, N, N, N, 3, 3, N, N, N, N}},
    {20, 0, {N, 108, N, N, 3, 3, N, N, N, N}},
    {21, 0, {N, N, N, N, 155, N, N, 155, N, N}},
    {22, 0, {N, N, N, N, N, N, N, N, N, 110}},
    {23, 0, {N, N, N, N, 147, N, N, 106, N, N}},
    {24, 0, {90, 90, N, N, 134, N, N, 134, 135, N}},
};
constexpr int kSharedDeathCue = 86;

int monsterCue(int kind, int slot, int anim) {
    if (anim < 0 || anim >= 10) return -1;
    for (const auto& c : kMonsterCues) {
        if (c.kind == kind && c.slot == slot && c.voice[static_cast<std::size_t>(anim)] >= 0) return c.voice[static_cast<std::size_t>(anim)];
    }
    return anim == chart::kEnemyDeath ? kSharedDeathCue : -1;
}

}  // namespace

double monsterChartSpeed(int step) {
    // 0x45a2f0's table.
    static constexpr double kSteps[10] = {0.3, 0.4, 0.6, 0.8, 1.0, 1.5, 2.0, 2.5, 3.0, 4.0};
    return kSteps[std::clamp(step, 0, 9)];
}

void World::tickActors() {
    for (auto& actor : actors) {
        // Scripts show and hide characters through their NET flag, by
        // SETACTIVE or by setting it themselves.
        if (const auto f = flags.find({ScenarioScript::kNet, kStateFlagBases[0] + actor.gameId()}); f != flags.end()) {
            actor.active = f->second != 0;
        }
        if (!actor.active) continue;
        if (actor.hostile() && host_ && host_->hero()) {
            if (actor.placement.kind < 0) {
                // An Enemy Hole never moves or strikes; a blow only stops it a moment.
                if (actor.state == Actor::State::Hit && (actor.stunTicks -= kOriginalTicksPerTick) <= 0) {
                    actor.endStun();
                    actor.state = Actor::State::Idle;
                }
            } else if (actor.aiList >= 0) {
                tickMonsterAi(actor);
            } else {
                tickChase(actor);
            }
        }
        if (actor.scripted) {
            tickScriptedAction(actor);
        } else {
            actor.character.tick();
        }
        monsterCries(actor);
    }
}

// Distance between the two collision boxes, 0 when they touch.
double World::gapToHero(const Actor& enemy) const {
    const auto& r = enemy.placement.rect;
    const double ex = enemy.character.x, ey = enemy.character.y;
    const Character& hero = *host_->hero();
    const double px = hero.x, py = hero.y;
    const double gx = std::max({px - kPlayerBox - (ex + r[2]), ex + r[0] - (px + kPlayerBox), 0.0});
    const double gy = std::max({py - kPlayerBox - (ey + r[3]), ey + r[1] - (py + kPlayerBox), 0.0});
    return std::max(gx, gy);
}

bool World::chooseAiAction(Actor& enemy, int event) {
    const auto& list = ai->lists[static_cast<std::size_t>(enemy.aiList)];
    const AiSituation situation{gapToHero(enemy), enemy.hp * 100 / std::max(1, enemy.maxHp)};
    const AiControl::Entry* e = selectAiEntry(list, event, situation, random);
    if (!e) return false;
    enemy.action = e->action;
    enemy.entry = *e;
    enemy.actionTicks = 0;
    enemy.struck = false;
    return true;
}

void World::tickMonsterAi(Actor& enemy) {
    using State = Actor::State;
    if (enemy.state == State::Dead) return;
    if (enemy.state == State::Hit) {
        // A stun holds it there (the companion's jump and dash).
        if (enemy.stunTicks > 0 && (enemy.stunTicks -= kOriginalTicksPerTick) <= 0) enemy.endStun();
        if (!enemy.character.finished() || enemy.stunTicks > 0) return;
        enemy.state = State::Idle;
        enemy.character.play(chart::kIdle);
    }
    // Attacks and spells play out before anything new is chosen.
    if (!(isAttack(enemy.action) && enemy.actionTicks > 0) && enemy.event >= 0) {
        bool chosen = chooseAiAction(enemy, enemy.event);
        // Most events fall back to the "decide" list when they have no entry.
        const int e = enemy.event;
        if (!chosen && ((e >= 1 && e <= 10) || e == AiControl::kAnimationDone || e == AiControl::kHit)) {
            chosen = chooseAiAction(enemy, AiControl::kDecide);
        }
        if (chosen) enemy.event = -1;
    }
    runAiAction(enemy);
}

void World::runAiAction(Actor& enemy) {
    Character& c = enemy.character;
    const Character& hero = *host_->hero();
    const auto& list = ai->lists[static_cast<std::size_t>(enemy.aiList)];
    const auto& p = enemy.entry.param;
    // Control.aid's speeds and durations count the original's ticks, 30 a
    // second (0x45c900), so each of ours moves 1.5 of its steps and counts
    // 1.5 of its ticks.
    const double scale = enemy.placement.stats.speed / 1000.0 * kOriginalTicksPerTick;
    const bool starting = enemy.actionTicks == 0;
    enemy.actionTicks += kOriginalTicksPerTick;

    auto moveTowards = [&](double tx, double ty, double speed) {
        const double dx = tx - c.x, dy = ty - c.y;
        const double length = std::hypot(dx, dy);
        if (length < 1.0 || speed <= 0) {
            c.play(chart::kIdle);
            return true;
        }
        const double step = std::min(speed, length);
        c.faceTowards(dx, dy);
        c.play(tryMove(c, dx / length * step, dy / length * step) ? chart::kWalk : chart::kIdle);
        return step >= length;
    };
    // The target has to stay within the entry's range for moves that need one.
    auto inRange = [&] { return targetInRange(enemy.entry, gapToHero(enemy)); };

    switch (enemy.action) {
        case AiControl::kWait:
            c.play(chart::kIdle);
            enemy.event = enemy.actionTicks >= p[1] ? AiControl::kDecide : AiControl::kWaiting;
            break;
        case AiControl::kWander: {
            if (starting) enemy.wanderPause = 0;
            if (enemy.wanderPause > 0) {
                enemy.wanderPause -= kOriginalTicksPerTick;
                c.play(chart::kIdle);
                if (enemy.wanderPause <= 0) {
                    enemy.wanderPause = 0;
                    const auto& a = enemy.placement.stats.area;
                    enemy.wanderX = enemy.placement.x + a[0] + random.next(std::max(1, a[2] - a[0]));
                    enemy.wanderY = enemy.placement.y + a[1] + random.next(std::max(1, a[3] - a[1]));
                }
            } else if (starting || moveTowards(enemy.wanderX, enemy.wanderY, p[3] * scale)) {
                enemy.wanderPause = std::max(1, p[4] + random.next(p[5] + 1));
            }
            enemy.event = enemy.actionTicks >= p[1] ? AiControl::kWanderDone : AiControl::kWandering;
            break;
        }
        case AiControl::kStop:
            c.play(chart::kIdle);
            enemy.event = AiControl::kDecide;
            break;
        case AiControl::kRetreat:
            if (!inRange()) {
                c.play(chart::kIdle);
                enemy.event = AiControl::kRetreatDone;
                break;
            }
            moveTowards(2 * c.x - hero.x, 2 * c.y - hero.y, p[3] * scale);
            enemy.event = enemy.actionTicks >= p[1] ? AiControl::kRetreatDone : AiControl::kRetreating;
            break;
        case AiControl::kApproach:
            if (!inRange()) {
                c.play(chart::kIdle);
                enemy.event = AiControl::kApproachDone;
                break;
            }
            if (gapToHero(enemy) > 0) {
                moveTowards(hero.x, hero.y, p[3] * scale);
            } else {
                c.faceTowards(hero.x - c.x, hero.y - c.y);
                c.play(chart::kIdle);
            }
            enemy.event = enemy.actionTicks >= p[1] ? AiControl::kApproachDone : AiControl::kApproaching;
            break;
        case AiControl::kGoHome:
            moveTowards(enemy.placement.x, enemy.placement.y, list.walkSpeed * scale);
            enemy.event = enemy.actionTicks > 90 ? AiControl::kDecide : -1;
            break;
        default: {
            if (!isAttack(enemy.action)) break;
            const bool magic = enemy.action >= AiControl::kMagic;
            const int slot = enemy.action - (magic ? AiControl::kMagic : AiControl::kAttack);
            const auto k = static_cast<std::size_t>(slot);
            const auto& s = enemy.placement.stats;
            if (starting) {
                // Attacks play chart 4 plus their slot's word 64-66 and
                // spells chart 7 plus word 67-69 (0x45a749, 0x45ac90),
                // aimed at the player as the action starts.
                const int wanted = magic ? 7 + s.spellCharts[k] : chart::kEnemyAttack + s.attackCharts[k];
                c.faceTowards(hero.x - c.x, hero.y - c.y);
                enemy.aim = std::atan2(hero.y - c.y, hero.x - c.x);
                c.restart(c.hasChart(wanted) ? wanted : chart::kEnemyAttack,
                          monsterChartSpeed(magic ? s.spellSpeeds[k] : s.attackSpeeds[k]));
            }
            // A blow lands, or a spell leaves the hand, at each frame whose
            // cell has status 0x40, so some do it two or three times.
            // Charts without one (the open content's) do it once, halfway
            // through.
            int strikes = c.takeStrikes();
            if (!c.hasStrikes() && !enemy.struck && c.frame() >= c.frameCount() / 2) {
                enemy.struck = true;
                strikes = 1;
            }
            for (; strikes > 0; --strikes) {
                if (magic) {
                    host_->monsterCasts(enemy, slot);
                } else if (s.shotKind != -1 && s.shotSlot == slot) {
                    // This attack throws a stone or shoots an arrow instead
                    // of striking (words 44 and 48).
                    host_->monsterThrows(enemy, slot);
                } else {
                    const double reach = enemy.entry.cond[3] == 1 && enemy.entry.cond[5] > 0 ? enemy.entry.cond[5] + 60.0 : 100.0;
                    if (gapToHero(enemy) <= reach) host_->monsterStrikes(enemy, slot);
                }
            }
            if (c.finished()) {
                c.play(chart::kIdle);
                enemy.action = -1;
                enemy.event = AiControl::kAnimationDone;
            }
            break;
        }
    }
}

// Placeholder behaviour: notice the player when close, walk up to them,
// swing, and wander back home when they get away.
void World::tickChase(Actor& enemy) {
    using State = Actor::State;
    Character& c = enemy.character;
    const Character& hero = *host_->hero();
    switch (enemy.state) {
        case State::Dead: return;
        case State::Hit:
            if (enemy.stunTicks > 0 && (enemy.stunTicks -= kOriginalTicksPerTick) <= 0) enemy.endStun();
            if (!c.finished() || enemy.stunTicks > 0) return;
            enemy.state = State::Chase;
            break;
        case State::Attack:
            if (c.frame() == c.frameCount() / 2 && distance(c, hero) <= kMeleeRange * 1.2) {
                host_->monsterStrikes(enemy, 0);
            }
            if (!c.finished()) return;
            enemy.state = State::Chase;
            break;
        default: break;
    }

    const double toPlayer = distance(c, hero);
    const bool chasing = enemy.state == State::Chase ? toPlayer < kGiveUpRange : toPlayer < kAggroRange;
    double tx = enemy.placement.x, ty = enemy.placement.y;
    if (chasing) {
        enemy.state = State::Chase;
        if (toPlayer <= kMeleeRange && c.hasChart(chart::kEnemyAttack)) {
            c.faceTowards(hero.x - c.x, hero.y - c.y);
            c.restart(chart::kEnemyAttack, monsterChartSpeed(enemy.placement.stats.attackSpeeds[0]));
            enemy.state = State::Attack;
            return;
        }
        tx = hero.x;
        ty = hero.y;
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

// A scripted action's frame: played once it ends on its last frame and the
// person stands; looping, the frame after `to` (or the last) is `from` (or
// the first) again (0x45d850).
void World::tickScriptedAction(Actor& a) {
    auto& s = *a.scripted;
    Character& c = a.character;
    if (c.chart() != s.chart) {
        a.scripted.reset();
        return;
    }
    const int last = c.frameCount() - 1;
    if (s.loop) {
        const int end = s.to == -1 ? last : std::min(s.to, last);
        if (c.frame() >= end) {
            c.setFrame(s.from == -1 ? 0 : s.from);
            return;
        }
        c.tick();
        return;
    }
    if (c.frame() >= last) {
        a.scripted.reset();
        c.play(chart::kIdle);
        return;
    }
    c.tick();
}

void World::monsterCries(Actor& enemy) {
    const int cues = enemy.character.takeCues();
    if (cues == 0) return;
    for (int slot = 0; slot < 3; ++slot) {
        if (cues & (0x400 << slot)) sound(monsterCue(enemy.placement.kind, slot, enemy.character.chart()), enemy.character.x, enemy.character.y);
    }
}

void World::monsterDeathCry(const Actor& enemy) {
    const int kind = enemy.placement.kind;
    if (kind >= 0 && kind < static_cast<int>(std::size(kDeathCry))) sound(kDeathCry[kind], enemy.character.x, enemy.character.y);
}

}  // namespace sf

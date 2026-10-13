// Monster behaviour driven by the game's own AI lists (Control.aid).
//
// Each tick, the monster's latest event (an action finishing, being hit, a
// timer running) picks a new action from its AI list (selectAiEntry), and
// the action runs. Actions follow ShadowFlare.exe (dispatched at
// 0x459340); the movement and attack details are simplified.
#include <algorithm>
#include <cmath>
#include <climits>

#include "game/game.h"

namespace sf {

namespace {

constexpr double kPlayerBox = 80.0;  // half-size of the player's collision box, world units

bool isAttack(int action) { return action >= AiControl::kAttack && action < AiControl::kStop; }

}  // namespace

// Distance between the two collision boxes, 0 when they touch.
double Game::gapToPlayer(const Actor& enemy) const {
    const auto& r = enemy.placement.rect;
    const double ex = enemy.character.x, ey = enemy.character.y;
    const double px = player_->x, py = player_->y;
    const double gx = std::max({px - kPlayerBox - (ex + r[2]), ex + r[0] - (px + kPlayerBox), 0.0});
    const double gy = std::max({py - kPlayerBox - (ey + r[3]), ey + r[1] - (py + kPlayerBox), 0.0});
    return std::max(gx, gy);
}

bool Game::chooseAiAction(Actor& enemy, int event) {
    const auto& list = ai_->lists[static_cast<std::size_t>(enemy.aiList)];
    const AiSituation situation{gapToPlayer(enemy), enemy.hp * 100 / std::max(1, enemy.maxHp)};
    const AiControl::Entry* e = selectAiEntry(list, event, situation, random_);
    if (!e) return false;
    enemy.action = e->action;
    enemy.entry = *e;
    enemy.actionTicks = 0;
    enemy.struck = false;
    return true;
}

void Game::tickEnemyAi(Actor& enemy) {
    using State = Actor::State;
    if (enemy.state == State::Dead) return;
    if (enemy.state == State::Hit) {
        // A stun holds it there (the companion's jump and dash).
        if (enemy.stunTicks > 0 && (enemy.stunTicks -= kOriginalTicksPerTick) <= 0) endStun(enemy);
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

void Game::runAiAction(Actor& enemy) {
    Character& c = enemy.character;
    const auto& list = ai_->lists[static_cast<std::size_t>(enemy.aiList)];
    const auto& p = enemy.entry.param;
    const double scale = enemy.placement.stats.speed / 1000.0;
    ++enemy.actionTicks;

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
    auto inRange = [&] { return targetInRange(enemy.entry, gapToPlayer(enemy)); };

    switch (enemy.action) {
        case AiControl::kWait:
            c.play(chart::kIdle);
            enemy.event = enemy.actionTicks >= p[1] ? AiControl::kDecide : AiControl::kWaiting;
            break;
        case AiControl::kWander: {
            if (enemy.actionTicks == 1) enemy.wanderPause = 0;
            if (enemy.wanderPause > 0) {
                --enemy.wanderPause;
                c.play(chart::kIdle);
                if (enemy.wanderPause == 0) {
                    const auto& a = enemy.placement.stats.area;
                    enemy.wanderX = enemy.placement.x + a[0] + random_.next(std::max(1, a[2] - a[0]));
                    enemy.wanderY = enemy.placement.y + a[1] + random_.next(std::max(1, a[3] - a[1]));
                }
            } else if (enemy.actionTicks == 1 || moveTowards(enemy.wanderX, enemy.wanderY, p[3] * scale)) {
                enemy.wanderPause = std::max(1, p[4] + random_.next(p[5] + 1));
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
            moveTowards(2 * c.x - player_->x, 2 * c.y - player_->y, p[3] * scale);
            enemy.event = enemy.actionTicks >= p[1] ? AiControl::kRetreatDone : AiControl::kRetreating;
            break;
        case AiControl::kApproach:
            if (!inRange()) {
                c.play(chart::kIdle);
                enemy.event = AiControl::kApproachDone;
                break;
            }
            if (gapToPlayer(enemy) > 0) {
                moveTowards(player_->x, player_->y, p[3] * scale);
            } else {
                c.faceTowards(player_->x - c.x, player_->y - c.y);
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
            if (enemy.actionTicks == 1) {
                // Attacks play chart 4 plus their slot's word 64-66 and
                // spells chart 7 plus word 67-69 (0x45a749, 0x45ac90),
                // aimed at the player as the action starts.
                const int wanted = magic ? 7 + s.spellCharts[k] : chart::kEnemyAttack + s.attackCharts[k];
                c.faceTowards(player_->x - c.x, player_->y - c.y);
                enemy.aim = std::atan2(player_->y - c.y, player_->x - c.x);
                c.restart(c.hasChart(wanted) ? wanted : chart::kEnemyAttack);
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
                    castMonsterSpell(enemy, slot);
                } else {
                    const double reach = enemy.entry.cond[3] == 1 && enemy.entry.cond[5] > 0 ? enemy.entry.cond[5] + 60.0 : 100.0;
                    if (gapToPlayer(enemy) <= reach) strikePlayer(enemy, slot);
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

}  // namespace sf

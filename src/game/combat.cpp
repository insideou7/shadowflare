// The Warrior's combos and axe spin (ShadowFlare.exe player states 11-18,
// 0x4364e0-0x437910), and the Elixir that brings a fallen hero back
// (0x443cb0). See docs/combat.md.

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "game/game.h"
#include "logic/combat.h"

namespace sf {

namespace {

// How far a blow reaches, as the plain swing's (game.cpp).
constexpr double kBlowReach = 220.0 * 1.2;
constexpr int kElixir = 98000000;     // special item (class 4)
constexpr int kElixirEffect = 21020;  // the controller Heal plays (0x521c)

}  // namespace

bool Game::startCombo(double wx, double wy) {
    if (!player_ || career_.current != HeroClass::Warrior) return false;
    const auto& weapon = inventory_.equipped(EquipSlot::Weapon);
    if (!weapon) return false;
    const WarriorMove move = warriorMove(weapon->def->subtype);
    if (move == WarriorMove::None) return false;
    // Busy with another move (+0x2dc): the click does nothing.
    if (combo_ || casting_ || heroStun_ || bowShot_) return true;
    const int current = player_->chart();
    if ((current == chart::kPlayerPunch || current == chart::kPlayerHit) && !player_->finished()) return true;
    Combo c;
    c.move = move;
    c.count = -1;
    const double dx = wx - player_->x, dy = wy - player_->y;
    if (const double length = std::hypot(dx, dy); length > 0) {
        c.dx = dx / length;
        c.dy = dy / length;
        player_->faceTowards(dx, dy);
    }
    c.facing = player_->direction;
    combo_ = c;
    walkTarget_.reset();
    attackTarget_.reset();
    talkTarget_.reset();
    objectTarget_.reset();
    log::info("The warrior's {}", move == WarriorMove::Spin ? "spin" : "combo");
    tickCombo();
    return true;
}

void Game::comboBy(int screenDx, int screenDy) {
    if (!player_ || !world_.map) return;
    int px, py;
    worldToScreen(static_cast<int>(player_->x), static_cast<int>(player_->y), px, py);
    double wx, wy;
    screenToWorld(px + screenDx, py + screenDy, wx, wy);
    if (!startCombo(wx, wy)) log::warn("--combo: the hero is not a Warrior with a sword, great sword, axe or mace in hand");
}

bool Game::heroSteadyInCombo() const { return combo_ && (combo_->move == WarriorMove::Spin || combo_->blow < 3); }

void Game::tickCombo() {
    if (!combo_ || !player_) return;
    // A combo runs on the original's ticks, 30 a second; the first goes at
    // once.
    if (combo_->count < 0) {
        comboTick(*combo_);
        return;
    }
    combo_->carry += kOriginalTicksPerTick;
    while (combo_ && combo_->carry >= 1.0) {
        combo_->carry -= 1.0;
        comboTick(*combo_);
    }
}

// One of the original's ticks of the combo or spin.
void Game::comboTick(Combo& c) {
    const bool heroine = heroSex_ == HeroSex::Female;
    const int step = attackSpeedStep(magic_.chantSteps[static_cast<std::size_t>(
                                         std::clamp(playerStats()[HeroStats::kAttackSpeed], 0, 255) / 32)],
                                     power_.active());
    const bool spin = c.move == WarriorMove::Spin;
    if (c.count < 0) {
        c.count = 0;
        c.frame = 0;
    } else {
        ++c.count;
        c.frame = static_cast<int>(c.count * (spin ? spinSpeed(step) : comboSpeed(step)));
    }
    const auto& weapon = inventory_.equipped(EquipSlot::Weapon);
    const int swingSound = weapon ? itemSound(*weapon->def, kItemSwing) : 1;
    auto march = [&](double stride) {
        if (stride > 0) tryMove(*player_, c.dx * stride, c.dy * stride);
    };

    if (spin) {
        // Without the spin's charts (the open pack's hero), the attack chart
        // stands in: up to its blow, turning round on the blow's frame, and
        // the rest of it after.
        const bool own = player_->hasChart(21) && player_->hasChart(23);
        int windUp, turn = 8, recover;
        if (own) {
            windUp = player_->frameCountOf(21, c.facing);
            turn = std::max(1, player_->frameCountOf(22, 8));
            recover = player_->frameCountOf(23, c.facing);
        } else {
            const int n = std::max(1, player_->frameCountOf(chart::kPlayerPunch, c.facing));
            windUp = std::clamp(strikeFrame(chart::kPlayerPunch, n / 2), 0, n - 1);
            recover = n - windUp;
        }
        const SpinFrame f = spinFrame(c.frame, c.facing, windUp, turn, recover);
        if (f.turning) {
            if (c.count % 8 == 0) playSound(swingSound, player_->x, player_->y);
            march(kSpinStride);
            if (c.count % 6 == 0) comboBlow(c);
        }
        if (own) {
            if (f.chart == 22) player_->direction = 8;
            player_->pose(f.chart, f.frame);
        } else if (f.chart == 22) {
            player_->direction = (c.facing + f.frame) % 8;
            player_->pose(chart::kPlayerPunch, windUp);
        } else {
            player_->pose(chart::kPlayerPunch, f.chart == 21 ? f.frame : windUp + f.frame);
        }
        c.lastFrame = c.frame;
        if (f.done) {
            player_->direction = c.facing;
            player_->play(chart::kIdle);
            combo_.reset();
        }
        return;
    }

    // A blow of the sword's or the great sword's combo. Without its own
    // chart (the open pack's hero) each blow plays the attack chart.
    int chart = comboChart(c.move, c.blow);
    if (!player_->hasChart(chart)) chart = chart::kPlayerPunch;
    player_->direction = c.facing;
    const int n = std::max(1, player_->frameCountOf(chart, c.facing));
    static constexpr int kStrikes[3] = {7, 10, 9};
    const int strike = strikeFrame(chart, chart == chart::kPlayerPunch ? n * 7 / 10 : kStrikes[c.blow - 1]);
    const int frame = std::min(c.frame, n - 1);
    bool falls = false;
    if (c.frame < n && c.frame != c.lastFrame && c.lastFrame < strike && strike <= c.frame) {
        falls = true;
        c.struck = true;
    }
    // Until the blow falls the hero lunges where they aimed, 10 units
    // further each tick, up to 60 (210 in all).
    if (!c.struck && c.lungeTick * 10 <= 60) march(comboLunge(c.lungeTick++));
    if (falls) {
        playSound(comboShout(c.blow, heroine), player_->x, player_->y);
        comboBlow(c);
    }
    if (c.count == 6) playSound(swingSound, player_->x, player_->y);
    player_->pose(chart, frame);
    c.lastFrame = c.frame;
    if (frame < n - 1) return;
    if (c.blow < 3) {
        // The next blow follows straight on.
        ++c.blow;
        c.count = 0;
        c.frame = 0;
        c.lastFrame = -1;
        c.struck = false;
        c.lungeTick = 0;
        return;
    }
    player_->play(chart::kIdle);
    combo_.reset();
}

// The blow lands on every living monster in reach: ahead and two
// directions either side for the combo, all round for the spin (0x4417f0).
// Each must be hit (hit against evasion); the combo's third blow strikes
// harder (10-12 tenths of the attack instead of 9-11, 0x412a40). Each one
// hit may be stunned, may send back life (absorption), wears the weapon
// and thuds.
void Game::comboBlow(const Combo& c) {
    const bool spin = c.move == WarriorMove::Spin;
    const auto& weapon = inventory_.equipped(EquipSlot::Weapon);
    const int weaponChance = weapon ? weapon->modifiers[ItemDef::kModStunChance] : 0;
    const int weaponLength = weapon ? weapon->modifiers[ItemDef::kModStunLength] : 0;
    const bool weaponSteady = weapon && weapon->modifiers[ItemDef::kModHighDamage] != 0;
    for (auto& enemy : world_.actors) {
        if (!enemy.hostile() || !enemy.alive() || !enemy.active) continue;
        const double dx = enemy.character.x - player_->x, dy = enemy.character.y - player_->y;
        if (std::hypot(dx, dy) > kBlowReach) continue;
        if (!inSwing(c.facing, facingOf(dx, dy), spin ? kAllRound : kComboSpread)) continue;
        const auto& s = enemy.placement.stats;
        if (world_.random.next(100) >= hitChance(playerHit(), s.evasion)) {
            showMiss(enemy.character.x, enemy.character.y);
            continue;
        }
        const int roll = world_.random.next(3) + (!spin && c.blow == 3 ? 1 : 0);
        const int damage = heroBlowDamage(magic_, playerAttack(), playerElements(), s.element, s.defense, roll);
        absorbFrom(enemy, damage);
        hurtEnemy(enemy, damage);
        // The combo's first two blows stun for as long as they come out,
        // past the usual 15 ticks; the third as the weapon says. The spin
        // nearly always stuns, 10 ticks longer than the weapon would.
        if (spin) {
            heroBlowStun(enemy, damage, 100, weaponLength + 10, false);
        } else {
            heroBlowStun(enemy, damage, weaponChance, weaponLength, c.blow < 3 || weaponSteady);
        }
        sayBroken(inventory_.wearFromAttacking(world_.random));
        playSound(6, player_->x, player_->y);
    }
}

void Game::heroBlowStun(Actor& enemy, int damage, int chance, int length, bool uncapped) {
    if (!enemy.alive()) return;
    const int maxHp = std::max(1, enemy.maxHp);
    const int row = std::min(damage * 50 / maxHp, 49);
    if (const auto* t = tables_.find(25); t && t->rows > row && t->cols > 1) {
        chance += t->value(row, 0);
        length += t->value(row, 1);
    }
    // Table 24: the hero's level in the monster's opposite element, 1-10.
    if (const int element = enemy.placement.stats.element; element >= 0 && element < 8) {
        const int level = playerElements()[static_cast<std::size_t>(element ^ 1)];
        if (const auto* t = tables_.find(24); level >= 1 && level <= 10 && t && t->rows >= level && t->cols > 1) {
            chance += t->value(level - 1, 0);
            length += t->value(level - 1, 1);
        }
    }
    stunEnemy(enemy, chance, length, uncapped);
}

bool Game::reviveWithElixir() {
    if (!player_ || !inventory_.carries(ItemDef::Class::Special, kElixir)) return false;
    inventory_.removeOne(ItemDef::Class::Special, kElixir);
    playerHp_ = playerMaxHp();
    playerMp_ = playerMaxMp();
    playSound(0x11, player_->x, player_->y);
    if (const int option = scriptEffectOption(kElixirEffect); option > 0) {
        spawnEffect(option, player_->x, player_->y).followsPlayer = true;
    }
    log::info("The Elixir brings the hero back");
    return true;
}

}  // namespace sf

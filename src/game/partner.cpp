// The hero's animal companion, as ShadowFlare.exe runs it (per-tick update
// 0x45f670, following 0x4622b0, fighting 0x462610). It follows the hero;
// when ACTIVE it bites monsters near them and can be hurt, and when
// INACTIVE it just follows and heals. The jump, dash and fetch orders are
// not in yet.
#include <algorithm>
#include <cmath>
#include <format>

#include "game/game.h"
#include "logic/magic.h"
#include "render/tiny_font.h"
#include "core/log.h"

namespace sf {

namespace {

// Partner charts (Character/PARTNER/*/Animation.Caf).
constexpr int kRun = 2, kHit = 3, kDeath = 4, kBite = 5;
constexpr int kNoDirection = 8;  // the death chart's only direction

constexpr double kSeekRange = 1200;   // looks for monsters this close to the hero
constexpr double kLeashRange = 1500;  // stops fighting this far from the hero
constexpr double kTeleportRange = 4000;
constexpr double kRunRange = 600;
constexpr double kIdleRange = 159;
constexpr double kBiteReach = 150;    // gap between the two boxes (+0x128)
constexpr double kBox = 80;           // half the partner's collision box (+0x138)
constexpr double kHeroWalkStat = 128;  // the hero's walking speed stat, which moves them kWalkSpeed
constexpr int kFadeTicks = 60;
constexpr std::int32_t kCompanionStone = 98000002;  // special item

// r is the enemy's box around (ex, ey).
template <class Rect>
double boxGap(const Character& c, const Rect& r, double ex, double ey) {
    const double gx = std::max({c.x - kBox - (ex + r[2]), ex + r[0] - (c.x + kBox), 0.0});
    const double gy = std::max({c.y - kBox - (ey + r[3]), ey + r[1] - (c.y + kBox), 0.0});
    return std::max(gx, gy);
}

}  // namespace

// The original moves both at stat / 5 a tick (0x454210), so the partner
// keeps the same ratio to the hero's step here.
double Game::partnerSpeed(int stat) const { return kPlayerWalkSpeed * stat / kHeroWalkStat; }

void Game::spawnPartner() {
    const int carried = partner_ ? partner_->hp : partnerHpCarried_;
    partner_.reset();
    if (!partnerTables_ || !player_) return;
    const int kind = partnerRecord_.kind;
    if (kind < 0 || kind >= kPartnerKinds) return;
    const PartnerKind& k = partnerTables_->kind(kind);
    const CharacterModel* m = model(std::format("Character/PARTNER/{:08d}/Animation", k.folder));
    if (!m) return;
    Partner p{Character(m)};
    p.character.x = player_->x;
    p.character.y = player_->y;
    p.character.direction = player_->direction;
    p.character.visibleLayers.clear();
    if (!m->animations.charts.empty() && !m->animations.charts[0].directions.empty()) {
        for (std::size_t i = 0; i < m->animations.charts[0].directions[0].layers.size(); ++i)
            p.character.visibleLayers.push_back(static_cast<int>(i));
    }
    auto tint = [](int perMille) { return static_cast<Uint8>(std::clamp(perMille * 255 / 1000, 0, 255)); };
    p.character.tint = {tint(k.tint[0]), tint(k.tint[1]), tint(k.tint[2]), 255};
    p.stats = partnerTables_->stats(kind, partnerRecord_.level());
    // HP carries over from the last map; 0 or less means full (0x450395).
    p.hp = carried > 0 ? std::min(carried, p.stats.maxHp) : p.stats.maxHp;
    if (partnerRecord_.reviveTicks > 0) {
        p.hp = 0;
        p.state = Partner::State::Dead;
        p.deadTicks = kFadeTicks;
        p.character.tint.a = 0;
    }
    partner_ = std::move(p);
}

void Game::changePartner(int kind) {
    if (kind < 0 || kind >= kPartnerKinds) return;
    // A swapped-in partner comes at full health (0x450500).
    partnerRecord_.kind = kind;
    partnerRecord_.reviveTicks = 0;
    partner_.reset();
    partnerHpCarried_ = 0;
    spawnPartner();
    if (partner_) log::info("{} joins the hero", partnerTables_->kind(kind).name);
}

void Game::bringPartner(int kind) {
    changePartner(kind);
    partnerRecord_.active = true;
}

void Game::refreshPartnerStats() {
    if (!partner_ || !partnerTables_) return;
    partner_->stats = partnerTables_->stats(partnerRecord_.kind, partnerRecord_.level());
    partner_->hp = std::min(partner_->hp, partner_->stats.maxHp);
}

void Game::togglePartnerMode() {
    partnerRecord_.active = !partnerRecord_.active;
    if (partner_ && partner_->state != Partner::State::Dead) {
        partner_->target.reset();
        if (partner_->state != Partner::State::Bite && partner_->state != Partner::State::Hit) {
            partner_->state = Partner::State::Idle;
        }
    }
    showMessage(partnerRecord_.active ? "Companion: ACTIVE" : "Companion: INACTIVE");
}

bool Game::partnerAlive() const { return partner_ && partner_->state != Partner::State::Dead; }

void Game::tickPartner() {
    if (!partner_ || !player_) return;
    Partner& p = *partner_;
    Character& c = p.character;
    using State = Partner::State;

    if (p.state == State::Dead) {
        // Fade out, then come back at the hero when the countdown ends.
        if (p.deadTicks < kFadeTicks) {
            ++p.deadTicks;
            c.tint.a = static_cast<Uint8>(255 * (kFadeTicks - p.deadTicks) / kFadeTicks);
        }
        if (partnerRecord_.reviveTicks > 0 && --partnerRecord_.reviveTicks == 0) {
            p.state = State::Idle;
            p.hp = p.stats.maxHp;
            c.tint.a = 255;
            c.x = player_->x;
            c.y = player_->y;
            c.direction = player_->direction;
            c.restart(chart::kIdle);
            log::info("The companion is back");
        }
        c.tick();
        return;
    }

    // INACTIVE partners heal a thousandth of their health each tick.
    if (!partnerRecord_.active && p.hp < p.stats.maxHp) {
        p.regenCarry += p.stats.maxHp % 1000;
        p.hp += p.stats.maxHp / 1000 + p.regenCarry / 1000;
        p.regenCarry %= 1000;
        p.hp = std::min(p.hp, p.stats.maxHp);
    }

    // A bite or a flinch plays out first.
    if (p.state == State::Bite) {
        if (!p.struck && c.frame() >= c.frameCount() / 2) {
            p.struck = true;
            if (p.target && *p.target < actors_.size()) {
                Actor& enemy = actors_[*p.target];
                if (enemy.alive() && boxGap(c, enemy.placement.rect, enemy.character.x, enemy.character.y) <= kBiteReach + 9) partnerBites(enemy);
            }
        }
        if (!c.finished()) {
            c.tick();
            return;
        }
        p.state = State::Idle;
    }
    if (p.state == State::Hit) {
        if (!c.finished()) {
            c.tick();
            return;
        }
        p.state = State::Idle;
    }

    const double toHero = std::hypot(player_->x - c.x, player_->y - c.y);
    auto moveTowards = [&](double tx, double ty, double speed, double stopAt) {
        const double dx = tx - c.x, dy = ty - c.y;
        const double length = std::hypot(dx, dy);
        if (length <= stopAt) return false;
        const double step = std::min(speed, length - stopAt);
        c.faceTowards(dx, dy);
        if (!tryMove(c, dx / length * step, dy / length * step)) {
            // Stuck on a wall: try each axis on its own.
            if (!tryMove(c, dx / length * step, 0)) tryMove(c, 0, dy / length * step);
        }
        return true;
    };

    // Fighting: the nearest living monster within reach of the hero.
    if (partnerRecord_.active && toHero < kSeekRange && !p.target) {
        double best = kSeekRange;
        for (std::size_t i = 0; i < actors_.size(); ++i) {
            const Actor& a = actors_[i];
            if (!a.hostile() || !a.alive() || !a.active) continue;
            const double d = std::hypot(a.character.x - c.x, a.character.y - c.y);
            if (d < best) {
                best = d;
                p.target = i;
            }
        }
    }
    if (p.target) {
        const bool valid = *p.target < actors_.size() && actors_[*p.target].alive() && partnerRecord_.active;
        if (!valid || toHero >= kLeashRange ||
            std::hypot(actors_[*p.target].character.x - player_->x, actors_[*p.target].character.y - player_->y) > kSeekRange * 1.5) {
            p.target.reset();
        }
    }
    if (p.target) {
        Actor& enemy = actors_[*p.target];
        if (boxGap(c, enemy.placement.rect, enemy.character.x, enemy.character.y) <= kBiteReach) {
            c.faceTowards(enemy.character.x - c.x, enemy.character.y - c.y);
            // Attack speed sets how fast the bite plays (0x460015): 0.2 to
            // 1.1 of the original's frames a tick, 0.6 at 128, which plays
            // here at the usual speed.
            const double speed = 0.2 + 0.1 * std::clamp(p.stats.attackSpeed / 32, 0, 9);
            c.restart(c.hasChart(kBite) ? kBite : chart::kIdle, speed / 0.6);
            p.state = State::Bite;
            p.struck = false;
        } else {
            moveTowards(enemy.character.x, enemy.character.y, partnerSpeed(p.stats.runSpeed), kBiteReach * 0.5);
            c.play(kRun);
            p.state = State::Run;
        }
        c.tick();
        return;
    }

    // Following, by how far away the hero is.
    if (toHero >= kTeleportRange) {
        c.x = player_->x + 200;
        c.y = player_->y + 200;
        if (!map_->ground().walkable(c.x, c.y)) {
            c.x = player_->x;
            c.y = player_->y;
        }
        p.state = State::Idle;
        c.play(chart::kIdle);
    } else if (toHero >= kRunRange) {
        moveTowards(player_->x, player_->y, partnerSpeed(p.stats.runSpeed), kIdleRange);
        c.play(kRun);
        p.state = State::Run;
        p.walkDelay = 5;
    } else if (toHero > kIdleRange) {
        if (p.walkDelay > 0) {
            --p.walkDelay;
            c.play(chart::kIdle);
        } else {
            moveTowards(player_->x, player_->y, partnerSpeed(p.stats.walkSpeed), kIdleRange);
            c.play(chart::kWalk);
            p.state = State::Walk;
        }
    } else {
        p.walkDelay = 5;
        p.state = State::Idle;
        c.play(chart::kIdle);
    }
    c.tick();
}

// A bite that came off (0x460251): the partner's hit against the monster's
// evasion, then its attack in its element (0x412d6f).
void Game::partnerBites(Actor& enemy) {
    const auto& s = enemy.placement.stats;
    const PartnerStats& st = partner_->stats;
    if (random_.next(100) >= hitChance(st.hit, s.evasion)) return;
    const int damage = spellDamage(st.attack, st.element, s.element, s.defense, random_.next(3), random_.next(4));
    hurtEnemy(enemy, damage, false, true);
}

// A monster's blow reaching an ACTIVE partner (0x45aa3f): its hit against
// the partner's evasion, the same damage rule against its defence.
void Game::strikePartner(Actor& enemy, int slot) {
    if (!partnerAlive() || !partnerRecord_.active) return;
    const auto& s = enemy.placement.stats;
    const auto k = static_cast<std::size_t>(std::clamp(slot, 0, 2));
    const PartnerStats& st = partner_->stats;
    if (random_.next(100) >= hitChance(s.hits[k], st.evasion)) return;
    hurtPartner(spellDamage(s.attacks[k], s.element, st.element, st.defense, random_.next(3), random_.next(4)));
}

bool Game::partnerInReach(const Actor& enemy) const {
    return partnerAlive() && partnerRecord_.active && boxGap(partner_->character, enemy.placement.rect, enemy.character.x, enemy.character.y) <= kBiteReach;
}

void Game::hurtPartner(int damage) {
    if (!partnerAlive()) return;
    Partner& p = *partner_;
    p.hp -= damage;
    if (p.hp <= 0) {
        p.hp = 0;
        p.state = Partner::State::Dead;
        p.deadTicks = 0;
        p.target.reset();
        p.character.direction = kNoDirection;
        p.character.restart(p.character.hasChart(kDeath) ? kDeath : chart::kIdle);
        partnerRecord_.reviveTicks = inventory_.carries(ItemDef::Class::Special, kCompanionStone) ? kPartnerReviveTicksWithStone
                                                                                              : kPartnerReviveTicks;
        log::info("The companion is down (revives in {} ticks)", partnerRecord_.reviveTicks);
        return;
    }
    // Hard blows make it flinch (the original rolls on table 25; here,
    // blows of a tenth of its health or more).
    if (damage * 10 >= p.stats.maxHp && p.state != Partner::State::Bite) {
        p.state = Partner::State::Hit;
        p.character.restart(p.character.hasChart(kHit) ? kHit : chart::kIdle);
    }
}

void Game::partnerCountKill() {
    if (!partnerTables_ || !partnerAlive()) return;
    if (partnerRecord_.countKill(*partnerTables_, playerLevel_)) {
        // A new level rebuilds the stats and heals fully (0x412f34).
        refreshPartnerStats();
        partner_->hp = partner_->stats.maxHp;
        showMessage(std::format("{} reached level {}", partnerTables_->kind(partnerRecord_.kind).name, partnerRecord_.level()));
    }
}

// The status text, as TALKPARTNERSTATUS and the field window show it
// (0x43167d, 0x4507a0).
std::string Game::partnerStatusText(int kind) const {
    if (!partnerTables_ || kind < 0 || kind >= kPartnerKinds) return {};
    const int level = partnerRecord_.levels[static_cast<std::size_t>(kind)];
    const PartnerStats s = partnerTables_->stats(kind, level);
    const std::string_view name = elementName(s.element);
    const std::string_view element = name.empty() ? "-" : name;
    const int cap = partnerLevelCap(playerLevel_);
    const int exp = partnerRecord_.experience[static_cast<std::size_t>(kind)];
    std::string expLine = level >= kPartnerMaxLevel ? "Experience   Max"
                          : level >= cap           ? "Experience Limit"
                                                   : std::format("Experience     {:5d}", exp);
    // The original prints defence on the magic evasion line.
    const int magicEvasion = rules_.partnerStatusShowsMagicEvasion ? s.magicEvasion : s.defense;
    return std::format(
        "{}\n"
        "Level          {:5d}\n"
        "HP             {:5d}\n"
        "Attribute      {:>5}\n"
        "Attack         {:5d}  Defense        {:5d}\n"
        "Hit Rate       {:5d}  Evasion Rate   {:5d}\n"
        "M Defense      {:5d}  M Evasion Rate {:5d}\n"
        "Attack Speed   {:5d}  Walking Speed  {:5d}\n"
        "{}\n",
        partnerTables_->kind(kind).name, level, s.maxHp, element, s.attack, s.defense, s.hit, s.evasion, s.magicDefense,
        magicEvasion, s.attackSpeed, s.walkSpeed, expLine);
}

// The companion's health gauge at the bottom left (0x403a5f): Bar.njp
// pattern 30 is its frame, 29 the bar and 31 / 32 the ACTIVE / INACTIVE
// label. Without the original's pictures, plain shapes stand in.
void Game::renderPartnerGauge() {
    if (!partner_ || menuOpen_) return;
    const int hp = partner_->hp, max = std::max(1, partner_->stats.maxHp);
    const bool blink = hp * 100 < max * 30 && (ticks_ / 5) % 2 == 0;
    const SpriteBank* bar = itemBank("System/Game/Pattern/Bar");
    auto pattern = [&](int n, int x, int y, int width = -1) {
        if (!bar) return false;
        const SpriteTexture* t = sprites_.get(*bar, n, false);
        if (!t) return false;
        SDL_Rect src{0, 0, width >= 0 ? std::min(width, t->width) : t->width, t->height};
        SDL_Rect dst{x, y, src.w, src.h};
        SDL_RenderCopy(renderer_, t->texture, &src, &dst);
        return true;
    };
    if (!pattern(30, 0, kPartnerGaugeY)) {
        SDL_Rect back{0, kPartnerGaugeY, 112, 12};
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 200);
        SDL_RenderFillRect(renderer_, &back);
    }
    const int width = hp * 109 / max;
    if (!blink && !pattern(29, 1, kPartnerGaugeY + 2, width)) {
        SDL_Rect fill{1, kPartnerGaugeY + 2, width, 6};
        SDL_SetRenderDrawColor(renderer_, 220, 150, 40, 255);
        SDL_RenderFillRect(renderer_, &fill);
    }
    if (!pattern(partnerRecord_.active ? 31 : 32, 56, kPartnerGaugeY - 3)) {
        drawText(renderer_, 58, kPartnerGaugeY - 12, partnerRecord_.active ? "ACTIVE" : "INACTIVE", {255, 255, 255, 255});
    }
}

bool Game::partnerGaugeClick(int x, int y) {
    if (!partner_ || x >= 112 || y < kPartnerGaugeY - 2 || y >= kPartnerGaugeY + 14) return false;
    togglePartnerMode();
    return true;
}

}  // namespace sf

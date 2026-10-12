// The hero's animal companion, as ShadowFlare.exe runs it (per-tick update
// 0x45f670, following 0x4622b0, fighting 0x462610). It follows the hero;
// when ACTIVE it bites monsters near them and can be hurt, and when
// INACTIVE it just follows and heals. Tab and a click order it to jump
// (0x460420), dash (0x460a00) or fetch an item (0x4624e0).
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
constexpr int kRun = 2, kHit = 3, kDeath = 4, kBite = 5, kJumpUp = 6, kJumpDown = 7;
constexpr int kNoDirection = 8;  // the death and jump charts' only direction

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

// Orders (0x441c00, 0x448b50, 0x4490b0).
constexpr int kOrderCooldown = 45;     // ticks before it takes another order on open ground (+0x210)
constexpr double kDashFromHero = 160;  // it only dashes from the hero's side
constexpr double kFetchGiveUp = 4000;  // gives up fetching this far from the hero
// The dash (0x460a00): 10 ticks to set off, then it runs at 30 a tick,
// 8 faster each tick, until tick 27, biting in a box 1.9 times its own
// every 4th tick; a bite stuns for sure (chance 100, 10 ticks) and holds
// the dash for 5 ticks.
constexpr int kDashWindUp = 10, kDashLastTick = 27, kDashHitEvery = 4, kDashHitPause = 5;
constexpr int kDashStartSpeed = 30, kDashSpeedUp = 8;
constexpr double kDashBoxScale = 1.9;
constexpr int kDashStunChance = 100, kDashStunLength = 10;
constexpr int kStunCap = 15;  // ticks, unless the blow or the monster says otherwise

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

// Its stats at its level, with what the hero wears (0x4136f0, 0x4138b0).
PartnerStats Game::partnerStats() const {
    PartnerStats s = partnerTables_->stats(partnerRecord_.kind, partnerRecord_.level());
    addItemBonuses(s, inventory_);
    return s;
}

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
    p.stats = partnerStats();
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
    partner_->stats = partnerStats();
    partner_->hp = std::min(partner_->hp, partner_->stats.maxHp);
}

void Game::togglePartnerMode() {
    partnerRecord_.active = !partnerRecord_.active;
    if (partner_ && partner_->state != Partner::State::Dead) {
        partner_->target.reset();
        using State = Partner::State;
        if (partner_->state == State::Walk || partner_->state == State::Run) partner_->state = State::Idle;
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

    if (p.orderCooldown > 0) --p.orderCooldown;
    if (p.state == State::Jump) {
        tickPartnerJump();
        return;
    }
    if (p.state == State::Dash) {
        tickPartnerDash();
        return;
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

    // Then the hero's order, if there is one.
    if (p.order == Partner::Order::Jump) {
        startPartnerJump();
        return;
    }
    if (p.order == Partner::Order::Dash) {
        p.order = Partner::Order::None;
        p.state = State::Dash;
        p.target.reset();
        p.skillTick = 0;
        p.hitPause = 0;
        p.dashSpeed = kDashStartSpeed;
        p.dashAngle = std::atan2(p.orderY - c.y, p.orderX - c.x);
        p.orderCooldown = kOrderCooldown;
        c.faceTowards(p.orderX - c.x, p.orderY - c.y);
        c.restart(kRun);
        log::info("The companion dashes");
        return;
    }

    const double toHero = std::hypot(player_->x - c.x, player_->y - c.y);
    if (p.order == Partner::Order::Fetch && tickPartnerFetch(toHero)) {
        c.tick();
        return;
    }
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
    // blows of a tenth of its health or more), but not out of a jump or a
    // dash.
    using State = Partner::State;
    if (damage * 10 >= p.stats.maxHp && p.state != State::Bite && p.state != State::Jump && p.state != State::Dash) {
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
std::string Game::partnerStatusText(int kind, bool withItems) const {
    if (!partnerTables_ || kind < 0 || kind >= kPartnerKinds) return {};
    const int level = partnerRecord_.levels[static_cast<std::size_t>(kind)];
    PartnerStats s = partnerTables_->stats(kind, level);
    // The field window counts what the hero wears; the kennel's sheet
    // does not (0x4507a0, 0x43167d).
    if (withItems) addItemBonuses(s, inventory_);
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

// ---- Orders: Tab and a click ----

bool Game::partnerAt(float screenX, float screenY) const {
    if (!partner_ || partner_->state == Partner::State::Dead) return false;
    int sx, sy;
    worldToScreen(static_cast<int>(partner_->character.x), static_cast<int>(partner_->character.y), sx, sy);
    return inClickRange(sx - cameraX_ - screenX, sy - 20 - cameraY_ - screenY);
}

bool Game::partnerCanStand(double x, double y) const { return map_ && map_->ground().walkable(x, y); }

// With Tab held, the left button sends the companion to jump on a monster,
// a switch or open ground, to fetch an item, or shows its status when
// clicked itself; the right button dashes at a monster or towards the
// cursor, from the hero's side only (0x441c00, 0x448b50, 0x4490b0).
bool Game::partnerOrder(float screenX, float screenY, bool right) {
    if (!partner_ || !player_ || !map_) return false;
    Partner& p = *partner_;
    using State = Partner::State;
    using Order = Partner::Order;
    const bool alive = p.state != State::Dead;
    const bool leaping = p.state == State::Jump || p.state == State::Dash;
    double wx, wy;
    screenToWorld(cameraX_ + screenX, cameraY_ + screenY, wx, wy);
    const auto target = clickTarget(screenX, screenY, ~0u);
    auto order = [&](Order o, double x, double y) {
        p.order = o;
        p.orderX = x;
        p.orderY = y;
        p.orderActor.reset();
        p.orderObject.reset();
    };

    if (right) {
        // Dashing needs it next to the hero and not already dashing.
        const Character& c = p.character;
        if (!alive || p.state == State::Dash || std::hypot(player_->x - c.x, player_->y - c.y) >= kDashFromHero) return true;
        if (target.kind == Settings::kEnemy) {
            const auto& a = actors_[target.index].character;
            order(Order::Dash, a.x, a.y);
        } else if (p.orderCooldown == 0) {
            order(Order::Dash, wx, wy);
        }
        return true;
    }

    switch (target.kind) {
        case Settings::kEnemy: {
            const auto& a = actors_[target.index].character;
            if (alive && !leaping && partnerCanStand(a.x, a.y)) {
                order(Order::Jump, a.x, a.y);
                p.orderActor = target.index;
            }
            return true;
        }
        case Settings::kItem:
            if (alive) order(Order::Fetch, groundItems_[target.index].x, groundItems_[target.index].y);
            return true;
        case Settings::kObject: {
            const auto& o = control_.placements[target.index];
            if (o.companionSwitch && alive && !leaping && partnerCanStand(o.x, o.y)) {
                order(Order::Jump, o.x, o.y);
                p.orderObject = target.index;
            }
            return true;
        }
        case Settings::kCompanion:
            // Its status, while it stands still (0x4507a0).
            if (p.state == State::Idle) {
                ScriptHost::Talk talk;
                talk.text = partnerStatusText(partnerRecord_.kind, true);
                openDialogue(talk);
                if (dialogue_) dialogue_->scripted = false;
            }
            return true;
        case Settings::kPerson: return true;
        default: break;
    }
    if (alive && !leaping && p.orderCooldown == 0 && partnerCanStand(wx, wy)) order(Order::Jump, wx, wy);
    return true;
}

void Game::orderPartnerBy(const std::string& kind, int dx, int dy) {
    if (!partner_ || !player_) return;
    Partner& p = *partner_;
    p.orderX = player_->x + dx;
    p.orderY = player_->y + dy;
    p.orderActor.reset();
    p.orderObject.reset();
    if (kind == "jump") {
        p.order = Partner::Order::Jump;
        // Aimed at a monster, as clicking one does: it lands where the
        // monster is by then.
        for (std::size_t i = 0; i < actors_.size(); ++i) {
            const auto& a = actors_[i];
            if (a.hostile() && a.alive() && std::hypot(a.character.x - p.orderX, a.character.y - p.orderY) < 200) p.orderActor = i;
        }
    } else if (kind == "dash") {
        p.order = Partner::Order::Dash;
    } else if (kind == "fetch") {
        double best = INFINITY;
        for (const auto& g : groundItems_) {
            const double d = std::hypot(g.x - player_->x, g.y - player_->y);
            if (d < best) {
                best = d;
                p.orderX = g.x;
                p.orderY = g.y;
                p.order = Partner::Order::Fetch;
            }
        }
    } else {
        log::warn("--companion expects jump:dx,dy, dash:dx,dy or fetch");
    }
}

void Game::startPartnerJump() {
    Partner& p = *partner_;
    Character& c = p.character;
    p.order = Partner::Order::None;
    p.state = Partner::State::Jump;
    p.target.reset();
    p.landed = false;
    p.struck = false;
    p.orderCooldown = kOrderCooldown;
    p.restDirection = c.direction;
    c.direction = kNoDirection;
    c.restart(c.hasChart(kJumpUp) ? kJumpUp : chart::kIdle);
    log::info("The companion jumps");
}

// Up, across to where it was sent (or to the monster it was sent at,
// wherever that is now), then down: the landing hits what is under it.
void Game::tickPartnerJump() {
    Partner& p = *partner_;
    Character& c = p.character;
    if (!p.landed) {
        if (!c.finished()) {
            c.tick();
            return;
        }
        double x = p.orderX, y = p.orderY;
        if (p.orderActor && *p.orderActor < actors_.size()) {
            const Actor& a = actors_[*p.orderActor];
            if (a.alive() && partnerCanStand(a.character.x, a.character.y)) {
                x = a.character.x;
                y = a.character.y;
            }
        }
        c.x = x;
        c.y = y;
        p.landed = true;
        log::info("The companion lands at {:.0f},{:.0f}", x, y);
        c.restart(c.hasChart(kJumpDown) ? kJumpDown : chart::kIdle);
        return;
    }
    if (!p.struck && c.frame() >= c.frameCount() / 2) {
        p.struck = true;
        const double half = p.stats.jumpArea / 2.0;
        partnerLands(c.x - half, c.y - half, c.x + half, c.y + half, false);
    }
    if (!c.finished()) {
        c.tick();
        return;
    }
    p.state = Partner::State::Idle;
    p.orderActor.reset();
    p.orderObject.reset();
    c.direction = p.restDirection;
    c.restart(chart::kIdle);
}

void Game::tickPartnerDash() {
    Partner& p = *partner_;
    Character& c = p.character;
    if (p.hitPause == 0) {
        if (p.skillTick >= kDashWindUp) {
            const double step = partnerSpeed(static_cast<int>(p.dashSpeed * 5));
            const double dx = std::cos(p.dashAngle) * step, dy = std::sin(p.dashAngle) * step;
            if (!tryMove(c, dx, dy) && !tryMove(c, dx, 0)) tryMove(c, 0, dy);
            p.dashSpeed += kDashSpeedUp;
        }
        if (p.skillTick > kDashWindUp - 1 && p.skillTick % kDashHitEvery == 0) {
            const double half = kBox * kDashBoxScale;
            partnerLands(c.x - half, c.y - half, c.x + half, c.y + half, true);
        }
    }
    if (p.skillTick > kDashLastTick) {
        p.state = Partner::State::Idle;
        c.restart(chart::kIdle);
        return;
    }
    if (p.hitPause > 0) --p.hitPause;
    if (p.hitPause == 0) {
        ++p.skillTick;
        c.tick();
        // The legs go faster once it is off (0x460a87).
        if (p.skillTick > kDashWindUp) c.tick();
    }
}

// A jump's landing or a dash's bite: every living monster whose box
// meets the square is bitten, with the partner's hit against its
// evasion, and may be stunned; switches in it are pressed. A dash holds
// for a moment after it bites.
void Game::partnerLands(double left, double top, double right, double bottom, bool dash) {
    Partner& p = *partner_;
    const PartnerStats& st = p.stats;
    bool bit = false;
    for (auto& enemy : actors_) {
        if (!enemy.hostile() || !enemy.alive() || !enemy.active) continue;
        const auto& r = enemy.placement.rect;
        const double ex = enemy.character.x, ey = enemy.character.y;
        if (ex + r[2] < left || ex + r[0] > right || ey + r[3] < top || ey + r[1] > bottom) continue;
        const auto& s = enemy.placement.stats;
        if (random_.next(100) >= hitChance(st.hit, s.evasion)) continue;
        bit = true;
        const int damage = spellDamage(st.attack, st.element, s.element, s.defense, random_.next(3), random_.next(4));
        log::info("The companion's {} hits {} for {}", dash ? "dash" : "jump", enemy.gameId(), damage);
        hurtEnemy(enemy, damage, false, true);
        if (dash) {
            stunEnemy(enemy, kDashStunChance, kDashStunLength, false, st.element);
        } else {
            stunEnemy(enemy, st.jumpStunChance, st.jumpStunLength, st.jumpStunUncapped, st.element);
        }
    }
    for (const auto& o : control_.placements) {
        if (o.group != ScenarioControl::Group::Object || !o.companionSwitch) continue;
        if (o.x + o.rect[2] < left || o.x + o.rect[0] > right || o.y + o.rect[3] < top || o.y + o.rect[1] > bottom) continue;
        log::info("The companion presses object {}", o.id);
        runEvent(ScenarioScript::kOnTalk, ScenarioControl::idBase(o.group) + o.id);
    }
    if (dash && bit) p.hitPause = kDashHitPause;
}

// How a blow stuns a monster (0x459690): `chance` less its resistance in
// percent, for `length` less its resistance in ticks, at least 1 and at
// most 15 unless the blow or the monster lifts that. A companion of the
// monster's opposite element stuns more readily and for longer (Table.Tbd
// table 24 row 5).
void Game::stunEnemy(Actor& enemy, int chance, int length, bool uncapped, int attackerElement) {
    if (!enemy.alive()) return;
    const auto& s = enemy.placement.stats;
    if (attackerElement >= 0 && s.element >= 0 && attackerElement == (s.element ^ 1)) {
        if (const auto* t = tables_.find(24); t && t->rows > 5 && t->cols > 1) {
            chance += t->value(5, 0);
            length += t->value(5, 1);
        }
    }
    if (random_.next(100) >= chance - s.stunChanceResist) return;
    int ticks = std::max(1, length - s.stunLengthResist);
    if (!uncapped && !s.stunUncapped) ticks = std::min(ticks, kStunCap);
    enemy.stunTicks = ticks;
    log::info("{} is stunned for {} ticks", enemy.gameId(), ticks);
    enemy.state = Actor::State::Hit;
    enemy.character.restart(chart::kEnemyHit);
    enemy.action = -1;
    enemy.event = AiControl::kHit;
}

// Running to an item it was sent for, then handing it to the hero
// (0x4624e0): it gives up when the item is gone or the hero is far away.
bool Game::tickPartnerFetch(double toHero) {
    Partner& p = *partner_;
    Character& c = p.character;
    auto it = std::ranges::find_if(groundItems_, [&](const GroundItem& g) { return std::hypot(g.x - p.orderX, g.y - p.orderY) < 1; });
    if (it == groundItems_.end() || toHero >= kFetchGiveUp) {
        p.order = Partner::Order::None;
        return false;
    }
    p.target.reset();
    const double dx = it->x - c.x, dy = it->y - c.y;
    const double length = std::hypot(dx, dy);
    if (length - kBox > kBiteReach) {
        const double step = std::min(partnerSpeed(p.stats.runSpeed), length - kBox - kBiteReach * 0.5);
        c.faceTowards(dx, dy);
        if (!tryMove(c, dx / length * step, dy / length * step)) {
            if (!tryMove(c, dx / length * step, 0)) tryMove(c, 0, dy / length * step);
        }
        c.play(kRun);
        p.state = Partner::State::Run;
        return true;
    }
    p.order = Partner::Order::None;
    p.state = Partner::State::Idle;
    c.play(chart::kIdle);
    const Item item = it->item;
    if (!inventory_.add(item)) {
        showMessage(std::format("No room for {}", item.shownName()));
        return true;
    }
    showMessage(item.def->isGold() ? std::format("{} gold", item.count) : item.shownName());
    log::info("The companion fetched {}", item.def->name);
    groundItems_.erase(it);
    return true;
}

}  // namespace sf


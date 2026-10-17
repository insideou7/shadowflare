// The hero's animal companion, as ShadowFlare.exe runs it (per-tick update
// 0x45f670, following 0x4622b0, fighting 0x462610). It follows the hero;
// when ACTIVE it bites monsters near them and can be hurt, and when
// INACTIVE it just follows and heals. Tab and a click order it to jump
// (0x460420), dash (0x460a00) or fetch an item (0x4624e0); the hero's
// Explosion spell sends it to blow up (0x43fcc0, 0x461c40).
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
// The run chart's third layer is a speed streak, drawn additively. It is
// hidden (+0x3c layer 2, cleared every tick by 0x45ff50) except during a
// dash once it is going flat out (0x460a00).
constexpr int kStreakLayer = 2;
constexpr int kDashStreakFrom = 18;  // dash tick (+0x158 > 0x11)

constexpr double kSeekRange = 1200;   // looks for monsters this close to the hero
constexpr double kLeashRange = 1500;  // stops fighting this far from the hero
constexpr double kTeleportRange = 4000;
constexpr double kRunRange = 600;
constexpr double kIdleRange = 159;
constexpr double kBiteReach = 150;    // gap between the two boxes (+0x128)
constexpr double kBox = 80;           // half the partner's collision box (+0x138)
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
// Explosion (0x461c40): what is in a square 320 each way around where it
// lands is caught; the blow's power is 1, so it is the stun that counts.
constexpr double kExplosionReach = 320;
constexpr int kExplosionPower = 1;
constexpr int kExplosionSpray = 10000000;  // Character/OPTION
// A freezing stun's ice (Character/OPTION): closing in, the four blocks
// (one chosen at random) and breaking away.
constexpr int kFreezeStart = 11000028, kFreezeHold = 11000029, kFreezeEnd = 11000033;
// Standing by the hero (0x461230): after 180 of the original's ticks a
// kind whose table 60 column 6 is set plays its idle chart 9 (a dog
// scratches or yawns) with voice 0x58, and at 210 goes back to standing.
constexpr int kIdleChart = 9, kIdleStart = 180, kIdleEnd = 210, kIdleVoice = 0x58;

// r is the enemy's box around (ex, ey).
template <class Rect>
double boxGap(const Character& c, const Rect& r, double ex, double ey) {
    const double gx = std::max({c.x - kBox - (ex + r[2]), ex + r[0] - (c.x + kBox), 0.0});
    const double gy = std::max({c.y - kBox - (ey + r[3]), ey + r[1] - (c.y + kBox), 0.0});
    return std::max(gx, gy);
}

void showStreak(Character& c, bool on) {
    auto& v = c.visibleLayers;
    const auto at = std::ranges::find(v, kStreakLayer);
    if (on && at == v.end()) v.push_back(kStreakLayer);
    if (!on && at != v.end()) v.erase(at);
}

}  // namespace

// The original moves it at stat / 5 an original tick (0x454210).
double Game::partnerSpeed(int stat) const { return stat / 5.0 * kOriginalTicksPerTick; }

// Its stats at its level, with what the hero wears (0x4136f0, 0x4138b0).
PartnerStats Game::partnerStats() const {
    PartnerStats s = partnerTables_->stats(partnerRecord_.kind, partnerRecord_.level());
    addItemBonuses(s, inventory_);
    // Moon raises them by table 200 (0x4138b0).
    if (spellOn(kMoon)) applyMoon(s, magic_, moonLevel_);
    return s;
}

std::vector<int> Game::partnerLayers(const CharacterModel& m, const std::vector<int>& before) {
    std::vector<int> layers = allLayers(m);
    if (std::ranges::find(before, kStreakLayer) == before.end()) std::erase(layers, kStreakLayer);
    return layers;
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
    p.character.visibleLayers = partnerLayers(*m);
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
    showStreak(c, false);  // only a dash shows it, for the tick it draws

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
        // The teeth meet on the frame whose cell has status 0x40 (0x46019b);
        // a chart without one bites halfway through.
        const bool strikes = c.takeStrikes() > 0;
        if (!p.struck && (c.hasStrikes() ? strikes : c.frame() >= c.frameCount() / 2)) {
            p.struck = true;
            if (p.target && *p.target < world_.actors.size()) {
                Actor& enemy = world_.actors[*p.target];
                if (enemy.alive() && boxGap(c, enemy.placement.rect, enemy.character.x, enemy.character.y) <= kBiteReach + 9) partnerBites(enemy);
            }
        }
        if (!c.finished()) {
            c.tick();
            return;
        }
        p.state = State::Idle;
    }
    if (p.state == State::Hit && p.stun) {
        if (tickStun(c, *p.stun)) {
            c.tick();
            return;
        }
        p.stun.reset();
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
    if (p.order == Partner::Order::Jump || p.order == Partner::Order::Explode) {
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
        // Only kinds whose table 60 column 6 is set make the dash's cry (0x460a00).
        if (const auto* t = tables_.find(60); t && partnerRecord_.kind < t->rows && t->cols > 6 && t->value(partnerRecord_.kind, 6) != 0) {
            playSound(0x5f, c.x, c.y);
        }
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
        for (std::size_t i = 0; i < world_.actors.size(); ++i) {
            const Actor& a = world_.actors[i];
            if (!a.hostile() || !a.alive() || !a.active) continue;
            const double d = std::hypot(a.character.x - c.x, a.character.y - c.y);
            if (d < best) {
                best = d;
                p.target = i;
            }
        }
    }
    if (p.target) {
        const bool valid = *p.target < world_.actors.size() && world_.actors[*p.target].alive() && partnerRecord_.active;
        if (!valid || toHero >= kLeashRange ||
            std::hypot(world_.actors[*p.target].character.x - player_->x, world_.actors[*p.target].character.y - player_->y) > kSeekRange * 1.5) {
            p.target.reset();
        }
    }
    if (p.target) {
        Actor& enemy = world_.actors[*p.target];
        if (boxGap(c, enemy.placement.rect, enemy.character.x, enemy.character.y) <= kBiteReach) {
            c.faceTowards(enemy.character.x - c.x, enemy.character.y - c.y);
            // Attack speed sets how fast the bite plays (0x460015): 0.2 to
            // 1.1 of the original's frames a tick, 0.6 at 128.
            const double speed = 0.2 + 0.1 * std::clamp(p.stats.attackSpeed / 32, 0, 9);
            c.restart(c.hasChart(kBite) ? kBite : chart::kIdle, speed);
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
        if (!world_.map->ground().walkable(c.x, c.y)) {
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
        tickPartnerIdle();
        c.tick();
        return;
    }
    p.idleTicks = 0;
    c.tick();
}

void Game::tickPartnerIdle() {
    Partner& p = *partner_;
    Character& c = p.character;
    const double before = p.idleTicks;
    p.idleTicks += kOriginalTicksPerTick;
    const auto* t = tables_.find(60);
    const bool fidgets = t && partnerRecord_.kind < t->rows && t->cols > 6 && t->value(partnerRecord_.kind, 6) != 0;
    // The open pack's dog has no chart 9: it just stands.
    if (!fidgets || !c.hasChart(kIdleChart) || p.idleTicks >= kIdleEnd) {
        if (!fidgets || p.idleTicks >= kIdleEnd) p.idleTicks = 0;
        c.play(chart::kIdle);
        return;
    }
    if (p.idleTicks < kIdleStart) {
        c.play(chart::kIdle);
        return;
    }
    if (before < kIdleStart) playSound(kIdleVoice, c.x, c.y);
    c.play(kIdleChart);
}

// A bite that came off (0x460251): the partner's hit against the monster's
// evasion, then its attack in its element (0x412d6f).
void Game::partnerBites(Actor& enemy) {
    const auto& s = enemy.placement.stats;
    const PartnerStats& st = partner_->stats;
    if (world_.random.next(100) >= hitChance(st.hit, s.evasion)) return;
    playSound(0x2c, partner_->character.x, partner_->character.y);  // 0x45fff0
    const int damage = spellDamage(st.attack, st.element, s.element, s.defense, world_.random.next(3), world_.random.next(4));
    hurtEnemy(enemy, damage, false, true);
}

// A monster's blow reaching an ACTIVE partner (0x45aa3f): its hit against
// the partner's evasion, the same damage rule against its defence.
void Game::strikePartner(Actor& enemy, int slot) {
    if (!partnerAlive() || !partnerRecord_.active) return;
    const auto& s = enemy.placement.stats;
    const auto k = static_cast<std::size_t>(std::clamp(slot, 0, 2));
    const PartnerStats& st = partner_->stats;
    if (world_.random.next(100) >= hitChance(s.hits[k], st.evasion)) {
        world_.showMiss(partner_->character.x, partner_->character.y);
        return;
    }
    hurtPartner(spellDamage(s.attacks[k], s.element, st.element, st.defense, world_.random.next(3), world_.random.next(4)));
    playSound(6, enemy.character.x, enemy.character.y);  // the blow lands (0x45a2f0)
}

bool Game::partnerInReach(const Actor& enemy) const {
    return partnerAlive() && partnerRecord_.active && boxGap(partner_->character, enemy.placement.rect, enemy.character.x, enemy.character.y) <= kBiteReach;
}

void Game::hurtPartner(int damage) {
    // The Invincible Companion cheat: blows land but do nothing.
    if (!partnerAlive() || invincibleCompanion()) return;
    Partner& p = *partner_;
    p.hp -= damage;
    p.stun.reset();
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
    // The Experience Rate cheat counts a kill as rate times one (always
    // one in original mode).
    if (partnerRecord_.countKill(*partnerTables_, playerLevel_, xpRate() / Settings::kXpRateNormal)) {
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
    const int magicEvasion = world_.rules.partnerStatusShowsMagicEvasion ? s.magicEvasion : s.defense;
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

// The companion's health gauge at the bottom left (0x4039f0): Bar.njp
// pattern 30 is its frame, 29 the bar and 31 / 32 the ACTIVE / INACTIVE
// label, each where its picture puts it. The bar is 109 pixels at full
// health and empties from the left: what is shown is its right-hand part,
// x 110 - width to 109. Under 30% it flickers bright, two frames in four.
// Without the pictures, plain shapes stand in.
void Game::renderPartnerGauge() {
    if (!partner_ || menuOpen_ || missionsOpen_) return;
    const int hp = partner_->hp, max = std::max(1, partner_->stats.maxHp);
    const SpriteBank* bar = itemBank("System/Game/Pattern/Bar");
    auto texture = [&](int n) { return bar ? sprites_.get(*bar, n, false) : nullptr; };
    if (const SpriteTexture* t = texture(30)) {
        const SDL_Rect dst{t->originX, t->originY, t->width, t->height};
        SDL_RenderCopy(renderer_, t->texture, nullptr, &dst);
    } else {
        SDL_Rect back{0, kPartnerGaugeY, 112, 12};
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 200);
        SDL_RenderFillRect(renderer_, &back);
    }
    const int width = hp >= max ? 109 : std::max(0, hp * 109 / max);
    if (width > 0) {
        if (const SpriteTexture* t = texture(29)) {
            // The exe's rectangle, x 110 - width to 109, cut from the picture.
            const int x0 = std::max(t->originX, 110 - width), x1 = std::min(t->originX + t->width, 109);
            if (x1 > x0) {
                const SDL_Rect src{x0 - t->originX, 0, x1 - x0, t->height};
                const SDL_Rect dst{x0, t->originY, x1 - x0, t->height};
                SDL_RenderCopy(renderer_, t->texture, &src, &dst);
                if (width * 100 / 109 < 30 && (ticks_ * 3 / 2) % 4 < 2) {
                    SDL_SetTextureBlendMode(t->texture, SDL_BLENDMODE_ADD);
                    SDL_SetTextureAlphaMod(t->texture, 128);
                    SDL_RenderCopy(renderer_, t->texture, &src, &dst);
                    SDL_SetTextureAlphaMod(t->texture, 255);
                    SDL_SetTextureBlendMode(t->texture, SDL_BLENDMODE_BLEND);
                }
            }
        } else {
            SDL_Rect fill{110 - width, kPartnerGaugeY + 2, width - 1, 6};
            SDL_SetRenderDrawColor(renderer_, 220, 150, 40, 255);
            SDL_RenderFillRect(renderer_, &fill);
        }
    }
    if (const SpriteTexture* t = texture(partnerRecord_.active ? 31 : 32)) {
        const SDL_Rect dst{t->originX, t->originY, t->width, t->height};
        SDL_RenderCopy(renderer_, t->texture, nullptr, &dst);
    } else {
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
    const SDL_FPoint at = onScreen(sx, sy - 20);
    return inClickRange(at.x - screenX, at.y - screenY);
}

bool Game::partnerCanStand(double x, double y) const { return world_.map && world_.map->ground().walkable(x, y); }

// With Tab held, the left button sends the companion to jump on a monster,
// a switch or open ground, to fetch an item, or shows its status when
// clicked itself; the right button dashes at a monster or towards the
// cursor, from the hero's side only (0x441c00, 0x448b50, 0x4490b0).
bool Game::partnerOrder(float screenX, float screenY, bool right) {
    if (!partner_ || !player_ || !world_.map) return false;
    Partner& p = *partner_;
    using State = Partner::State;
    using Order = Partner::Order;
    const bool alive = p.state != State::Dead;
    const bool leaping = p.state == State::Jump || p.state == State::Dash;
    double wx, wy;
    screenToWorld(pixelX(screenX), pixelY(screenY), wx, wy);
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
            const auto& a = world_.actors[target.index].character;
            order(Order::Dash, a.x, a.y);
        } else if (p.orderCooldown == 0) {
            order(Order::Dash, wx, wy);
        }
        return true;
    }

    switch (target.kind) {
        case Settings::kEnemy: {
            const auto& a = world_.actors[target.index].character;
            if (alive && !leaping && partnerCanStand(a.x, a.y)) {
                order(Order::Jump, a.x, a.y);
                p.orderActor = target.index;
            }
            return true;
        }
        case Settings::kItem:
            if (alive) order(Order::Fetch, world_.groundItems[target.index].x, world_.groundItems[target.index].y);
            return true;
        case Settings::kObject: {
            const auto& o = world_.control.placements[target.index];
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
        for (std::size_t i = 0; i < world_.actors.size(); ++i) {
            const auto& a = world_.actors[i];
            if (a.hostile() && a.alive() && std::hypot(a.character.x - p.orderX, a.character.y - p.orderY) < 200) p.orderActor = i;
        }
    } else if (kind == "explode") {
        // As the Explosion spell cast at that point, or at the monster
        // aimed at.
        for (const auto& a : world_.actors) {
            if (a.hostile() && a.alive() && std::hypot(a.character.x - p.orderX, a.character.y - p.orderY) < 200) {
                p.orderX = a.character.x;
                p.orderY = a.character.y;
            }
        }
        orderPartnerExplosion(p.orderX, p.orderY);
    } else if (kind == "dash") {
        p.order = Partner::Order::Dash;
    } else if (kind == "fetch") {
        double best = INFINITY;
        for (const auto& g : world_.groundItems) {
            const double d = std::hypot(g.x - player_->x, g.y - player_->y);
            if (d < best) {
                best = d;
                p.orderX = g.x;
                p.orderY = g.y;
                p.order = Partner::Order::Fetch;
            }
        }
    } else {
        log::warn("--companion expects jump:dx,dy, dash:dx,dy, explode:dx,dy or fetch");
    }
}

void Game::startPartnerJump() {
    Partner& p = *partner_;
    Character& c = p.character;
    p.explosion = p.order == Partner::Order::Explode;
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
        if (p.orderActor && *p.orderActor < world_.actors.size()) {
            const Actor& a = world_.actors[*p.orderActor];
            if (a.alive() && partnerCanStand(a.character.x, a.character.y)) {
                x = a.character.x;
                y = a.character.y;
            }
        }
        c.x = x;
        c.y = y;
        p.landed = true;
        // Two calls in the same tick, so two copies sound at once (0x460420).
        playSound(0x2d, c.x, c.y);
        playSound(0x2d, c.x, c.y);
        log::info("The companion lands at {:.0f},{:.0f}", x, y);
        c.restart(c.hasChart(kJumpDown) ? kJumpDown : chart::kIdle);
        return;
    }
    if (!p.struck && c.frame() >= c.frameCount() / 2) {
        p.struck = true;
        playSound(0x2e, c.x, c.y);  // the landing, whatever it hits
        if (p.explosion) {
            partnerExplodes();
        } else {
            const double half = p.stats.jumpArea / 2.0;
            partnerLands(c.x - half, c.y - half, c.x + half, c.y + half, false);
        }
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
    // Flat out, it trails a streak, but not while a bite holds it.
    showStreak(c, p.hitPause == 0 && p.skillTick >= kDashStreakFrom);
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

// The living monsters whose boxes meet the square, then those linked to
// objects in it (0x460420 hands a blow on such an object to its monster).
std::vector<std::size_t> Game::monstersIn(double left, double top, double right, double bottom) const {
    std::vector<std::size_t> found;
    auto meets = [&](double x, double y, const std::int32_t (&r)[4]) {
        return x + r[2] >= left && x + r[0] <= right && y + r[3] >= top && y + r[1] <= bottom;
    };
    for (std::size_t i = 0; i < world_.actors.size(); ++i) {
        const Actor& a = world_.actors[i];
        if (a.hostile() && a.alive() && a.active && meets(a.character.x, a.character.y, a.placement.rect)) found.push_back(i);
    }
    for (const auto& o : world_.control.placements) {
        if (o.group != ScenarioControl::Group::Object || o.linkedEnemy < 0 || !meets(o.x, o.y, o.rect)) continue;
        for (std::size_t i = 0; i < world_.actors.size(); ++i) {
            const Actor& a = world_.actors[i];
            if (a.placement.group == ScenarioControl::Group::Enemy && a.placement.id == o.linkedEnemy && a.hostile() &&
                a.alive() && a.active) {
                found.push_back(i);
            }
        }
    }
    return found;
}

// A jump's landing or a dash's bite: every monster in the square is
// tried, with the partner's hit against its evasion, and may be stunned;
// switches in it are pressed. Only the dash does damage: the jump's blow
// has no power (0x460420), the dash's is the partner's attack (0x460a00).
// A dash holds for a moment after it bites.
void Game::partnerLands(double left, double top, double right, double bottom, bool dash) {
    Partner& p = *partner_;
    const PartnerStats& st = p.stats;
    bool bit = false;
    for (std::size_t i : monstersIn(left, top, right, bottom)) {
        Actor& enemy = world_.actors[i];
        if (!enemy.alive()) continue;
        const auto& s = enemy.placement.stats;
        if (world_.random.next(100) >= hitChance(st.hit, s.evasion)) continue;
        bit = true;
        if (dash) {
            const int damage = spellDamage(st.attack, st.element, s.element, s.defense, world_.random.next(3), world_.random.next(4));
            log::info("The companion's dash hits {} for {}", enemy.gameId(), damage);
            hurtEnemy(enemy, damage, false, true);
            stunEnemy(enemy, kDashStunChance, kDashStunLength, false, st.element);
        } else {
            log::info("The companion's jump hits {}", enemy.gameId());
            stunEnemy(enemy, st.jumpStunChance, st.jumpStunLength, st.jumpStunUncapped, st.element);
        }
    }
    for (const auto& o : world_.control.placements) {
        if (o.group != ScenarioControl::Group::Object || !o.companionSwitch) continue;
        if (o.x + o.rect[2] < left || o.x + o.rect[0] > right || o.y + o.rect[3] < top || o.y + o.rect[1] > bottom) continue;
        log::info("The companion presses object {}", o.id);
        runEvent(ScenarioScript::kOnTalk, ScenarioControl::idBase(o.group) + o.id);
    }
    if (dash && bit) {
        p.hitPause = kDashHitPause;
        playSound(6, p.character.x, p.character.y);  // once a tick (0x460a00)
    }
}

// The hero's Explosion spell: once it is cast the companion jumps to the
// point (0x43fcc0), if it is up, free of its last order and not already
// jumping or dashing, and the spot is open ground.
void Game::orderPartnerExplosion(double x, double y) {
    if (!partner_) return;
    Partner& p = *partner_;
    using State = Partner::State;
    if (p.state == State::Dead || p.state == State::Jump || p.state == State::Dash || p.orderCooldown > 0) return;
    if (!partnerCanStand(x, y)) return;
    p.order = Partner::Order::Explode;
    p.orderX = x;
    p.orderY = y;
    p.orderActor.reset();
    p.orderObject.reset();
}

// Where it lands it blows up (0x461c40): every monster in reach it hits
// (its hit against their evasion) takes the spell's blow, power 1, and is
// stunned as tables 70-78 say for a monster of its element at the spell's
// level. Each one hit trains the spell.
void Game::partnerExplodes() {
    Partner& p = *partner_;
    const Character& c = p.character;
    const int ofSpell = world_.rules.explosionUsesItsOwnLevel ? kExplosion : kElementalStrike;
    const int level = std::max(1, castLevel(ofSpell));
    const int element = magic_.spell(kExplosion).element;
    log::info("The companion explodes at {:.0f},{:.0f} (level {})", c.x, c.y, level);
    // Its controller (21031, 0x42f890) plays the fire spray, both charts.
    world_.spawnEffect(kExplosionSpray, c.x, c.y);
    world_.spawnEffect(kExplosionSpray, c.x, c.y, 1);
    playSound(0x1d, c.x, c.y);
    playSound(0x17, c.x, c.y);
    for (std::size_t i : monstersIn(c.x - kExplosionReach, c.y - kExplosionReach, c.x + kExplosionReach, c.y + kExplosionReach)) {
        Actor& enemy = world_.actors[i];
        if (!enemy.alive()) continue;
        const auto& s = enemy.placement.stats;
        if (world_.random.next(100) >= hitChance(p.stats.hit, s.evasion)) continue;
        trainSpell(kExplosion);
        const int damage = spellDamage(kExplosionPower, element, s.element, s.magicDefense, world_.random.next(3), world_.random.next(4));
        log::info("The companion's explosion hits {} for {}", enemy.gameId(), damage);
        hurtEnemy(enemy, damage, true);
        const auto& stun = magic_.stun(s.element, kExplosion, level);
        stunEnemy(enemy, stun.chance, stun.length, stun.uncapped, element);
    }
}

// How a blow stuns a monster (0x459690): `chance` less its resistance in
// percent, for `length` less its resistance in ticks, at least 1 and at
// most 15 unless the blow or the monster lifts that. A companion of the
// monster's opposite element stuns more readily and for longer (Table.Tbd
// table 24 row 5). A monster turned to stone can't be stunned again until
// it wakes.
void Game::stunEnemy(Actor& enemy, int chance, int length, bool uncapped, int attackerElement, StunKind kind) {
    if (!enemy.alive() || enemy.stoned()) return;
    const auto& s = enemy.placement.stats;
    if (attackerElement >= 0 && s.element >= 0 && attackerElement == (s.element ^ 1)) {
        if (const auto* t = tables_.find(24); t && t->rows > 5 && t->cols > 1) {
            chance += t->value(5, 0);
            length += t->value(5, 1);
        }
    }
    if (world_.random.next(100) >= chance - s.stunChanceResist) return;
    int ticks = std::max(1, length - s.stunLengthResist);
    if (!uncapped && !s.stunUncapped) ticks = std::min(ticks, kStunCap);
    enemy.stunTicks = ticks;
    enemy.stunKind = kind;
    log::info("{} is stunned for {} ticks", enemy.gameId(), ticks);
    enemy.state = Actor::State::Hit;
    enemy.character.restart(chart::kEnemyHit);
    enemy.action = -1;
    enemy.event = AiControl::kHit;
    // Stone: grey, and held still on the hit pose's first frame.
    enemy.character.grey = enemy.character.held = kind == StunKind::Stone;
    if (kind == StunKind::Freeze) {
        // Ice closes round it, one of four blocks holds it and it breaks
        // away when the stun ends (the controller at 0x42d380).
        const int index = static_cast<int>(&enemy - world_.actors.data());
        const double x = enemy.character.x, y = enemy.character.y;
        world_.spawnEffect(kFreezeStart, x, y).followsActor = index;
        playSound(0x77, x, y);  // 0x459690
        Effect& ice = world_.spawnEffect(kFreezeHold + world_.random.next(4), x, y);
        ice.followsActor = index;
        ice.life = ticks;
        ice.then = kFreezeEnd;
    }
}

// Running to an item it was sent for, then handing it to the hero
// (0x4624e0): it gives up when the item is gone or the hero is far away.
bool Game::tickPartnerFetch(double toHero) {
    Partner& p = *partner_;
    Character& c = p.character;
    auto it = std::ranges::find_if(world_.groundItems, [&](const GroundItem& g) { return std::hypot(g.x - p.orderX, g.y - p.orderY) < 1; });
    if (it == world_.groundItems.end() || toHero >= kFetchGiveUp) {
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
    // A fetched mine joins the hero's mines, as one picked up does.
    if (item.def->isMine() ? !pickUpMine() : !inventory_.add(item)) {
        showMessage(item.def->isMine() ? std::string("No room for more mines") : std::format("No room for {}", item.shownName()));
        return true;
    }
    showMessage(item.def->isGold() ? std::format("{} gold", item.count) : item.shownName());
    log::info("The companion fetched {}", item.def->name);
    world_.groundItems.erase(it);
    return true;
}

}  // namespace sf


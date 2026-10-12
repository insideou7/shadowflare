// Spells: choosing them, casting them, what they do and how they look.
//
// The numbers come from the original (logic/magic.h, docs/magic.md), and so
// does what each spell makes and when: the original's spell controllers
// (0x42b1c0 and its neighbours) spawn effects from Character/OPTION, which
// play their animations, fly, home in and hurt. The open content draws its
// own art for those effects (tools/art/effects.py).
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include "core/log.h"
#include "game/game.h"
#include "render/tiny_font.h"

namespace sf {

namespace {

constexpr double kTau = 2 * std::numbers::pi;
// The original runs 30 ticks a second, so each engine tick is 1.5 of them.
constexpr double kOriginalTicksPerTick = 30.0 / kTicksPerSecond;
// Half the side of the square a hero or monster can be hit in. The
// original reads each one's judge rect; this is ours.
constexpr double kTargetHalf = 50.0;
// Ours: the original lets a missile fly until it meets something or a
// wall. This stops one lost in open ground.
constexpr double kMissileRange = 6000.0;
// What an effect is drawn for when the content has no animation for it.
constexpr int kMissingLength = 16;

// OPTION animations (Character/OPTION/<id>).
enum Option {
    kFireSpray = 10000000, kHellFireBurst = 10000001, kFireRing = 10000002, kFireBallFly = 10000010,
    kFireBurst = 10000011, kCastFlame = 10000012, kLightning = 10000030, kLightningSplash = 10000031,
    kLightningGlitter = 10000032, kIceBoltFly = 10000040, kIceCrystals = 10000050, kIceGather = 10000051,
    kIceMist = 10000052, kEarthSpear = 10000060, kBladeAppear = 10000080, kBladeFly = 10000081,
    kSonicBladeFly = 10000090, kElementOrb = 10000100, kMudFly = 10000110, kMudBurst = 10000111,
    kHitSpark = 11000000, kShock = 11000003, kFrostHit = 11000026, kIceSwirl = 11000027, kWaterBurst = 11000033,
    kHealGlow = 11000060, kGatherSwirl = 11000100, kAirBurst = 11000200, kElementCast = 11000210,
    kMagicShieldGlow = 11000241, kCounterBurstGlow = 11000251, kFirePillar = 12000000,
};

// What is played on whoever a spell hurts (the effect kind in its damage
// block, word 29).
int hitOptionOf(int spell) {
    switch (spell) {
        case kFireBall:
        case kFlameStrike: return kFireBurst;
        case kHellFire: return kHellFireBurst;
        case kPlasma:
        case kLightningStorm: return kShock;
        case kIceBolt:
        case kIceBlast:
        case kDreadDeathscythe: return kFrostHit;
        case kSonicBlade: return kAirBurst;
        default: return -1;
    }
}

enum class Kind { NotYet, Toggle, Heal, Cast };

Kind kindOf(int spell) {
    switch (spell) {
        case kTransport:
        case kMoon:
        case kMedusa:
        case kIdentify:
        case kExplosion: return Kind::NotYet;
        case kHeal: return Kind::Heal;
        default: break;
    }
    if (isToggleSpell(spell)) return Kind::Toggle;
    return Kind::Cast;
}

// Turns an angle by at most `limit` towards `target`.
double turnTowards(double angle, double target, double limit) {
    double diff = std::remainder(target - angle, kTau);
    return angle + std::clamp(diff, -limit, limit);
}

SDL_Color elementColor(int element) {
    switch (element) {
        case kFire: return {255, 120, 40, 255};
        case kWater: return {90, 170, 255, 255};
        case kEarth: return {170, 130, 70, 255};
        case kThunder: return {250, 240, 110, 255};
        case kHoly: return {255, 250, 220, 255};
        case kDark: return {150, 70, 200, 255};
        case kGel: return {110, 230, 160, 255};
        case kMetal: return {200, 210, 225, 255};
        default: return {255, 255, 255, 255};
    }
}

// Short names for the spell bar.
std::string_view barName(int spell) {
    constexpr std::string_view kShort[kSpellCount] = {"TRN", "FIRE", "ICE", "PLSM", "HELL", "BLST", "HEAL", "MOON",
                                                      "BERS", "ESHD", "SPRS", "FLAM", "DREAD", "STRM", "MDSA", "SONC",
                                                      "MUD", "IDNT", "MSHD", "CNTR", "EXPL", "ELEM"};
    return spell >= 0 && spell < kSpellCount ? kShort[spell] : "";
}

// A ring flattened like the ground.
void drawRing(SDL_Renderer* r, int cx, int cy, double radius) {
    constexpr int kSteps = 48;
    for (int i = 0; i < kSteps; ++i) {
        const double a0 = i * 2 * std::numbers::pi / kSteps, a1 = (i + 1) * 2 * std::numbers::pi / kSteps;
        SDL_RenderDrawLine(r, cx + static_cast<int>(radius * std::cos(a0)), cy + static_cast<int>(radius * std::sin(a0) / 2),
                           cx + static_cast<int>(radius * std::cos(a1)), cy + static_cast<int>(radius * std::sin(a1) / 2));
    }
}

}  // namespace

int Game::spellPower(int spell, int level) const {
    // Table 17 plus the hero's magic attack (the cast code, 0x4399db).
    return std::max(1, magic_.power(spell, level) + playerStats()[HeroStats::kMagicAttack]);
}

int Game::spellHit(int spell, int level) const { return magic_.hit(spell, level) + playerStats()[HeroStats::kMagicHit]; }

void Game::selectSpellSlot(int slot) {
    if (slot < 0 || slot >= kSpellBarSize) return;
    const int spell = spells_.bar[static_cast<std::size_t>(slot)];
    if (spell < 0) return;
    spells_.selected = spell;
    showMessage(std::format("{} (level {})", magic_.spell(spell).name, spells_.castLevel(spell)));
}

void Game::castBy(int slot, int screenDx, int screenDy) {
    if (!player_ || !map_) return;
    selectSpellSlot(slot);
    int px, py;
    worldToScreen(static_cast<int>(player_->x), static_cast<int>(player_->y), px, py);
    double wx, wy;
    screenToWorld(px + screenDx, py + screenDy, wx, wy);
    startCast(wx, wy);
}

void Game::teachSpell(int spell, int level) {
    if (spell < 0 || spell >= kSpellCount) return;
    spells_.level[static_cast<std::size_t>(spell)] = std::clamp(level, 1, kMaxSpellLevel);
    spells_.bar[0] = spell;
    spells_.selected = spell;
    playerMp_ = std::max(playerMp_, 9999);
}

void Game::trainSpell(int spell) {
    if (!spells_.train(magic_, spell)) return;
    log::info("{} is now level {}", magic_.spell(spell).name, spells_.castLevel(spell));
    showMessage(std::format("{} is now level {}", magic_.spell(spell).name, spells_.castLevel(spell)));
}

void Game::startCast(double worldX, double worldY) {
    if (casting_ || dialogue_ || openShop_) return;
    const int spell = spells_.selected;
    if (!spells_.knows(spell)) {
        showMessage("No spell chosen (F1-F8)");
        return;
    }
    const Kind kind = kindOf(spell);
    if (kind == Kind::NotYet) {
        showMessage(std::format("{} is not in the game yet", magic_.spell(spell).name));
        return;
    }
    // Turning a shield or Berserker off is free and needs no casting.
    if (kind == Kind::Toggle && spellOn(spell)) {
        setSpellOn(spell, false);
        showMessage(std::format("{} off", magic_.spell(spell).name));
        return;
    }
    if (kind == Kind::Heal && playerHp_ >= playerMaxHp()) return;  // as the original: not when full
    const int level = spells_.castLevel(spell);
    const int cost = spellMpCost(magic_, spell, level);
    if (playerMp_ < cost) {
        showMessage("Not enough MP");
        return;
    }
    playerMp_ -= cost;
    casting_ = spell;
    castTarget_ = {worldX, worldY};
    castReleased_ = false;
    walkTarget_.reset();
    attackTarget_.reset();
    talkTarget_.reset();
    mouseHeld_ = false;
    player_->faceTowards(worldX - player_->x, worldY - player_->y);
    int chantSpeed = 0;
    {
        HeroStats stats = heroStats();
        applyElementLevels(stats, chantSpeed, playerElements());
    }
    // Heal has a casting animation of its own.
    int chart = spell == kHeal ? chart::kPlayerHeal : chart::kPlayerCast;
    if (!player_->hasChart(chart)) chart = player_->hasChart(chart::kPlayerCast) ? chart::kPlayerCast : chart::kPlayerPunch;
    player_->restart(chart, castSpeedFactor(magic_, spell, chantSpeed));
    if (kind == Kind::Cast) {
        SpellCast cast;
        cast.spell = spell;
        cast.level = level;
        cast.power = spellPower(spell, level);
        cast.hit = spellHit(spell, level);
        cast.x = player_->x;
        cast.y = player_->y;
        cast.aim = std::atan2(worldY - player_->y, worldX - player_->x);
        casts_.push_back(std::move(cast));
    }
}

void Game::releaseSpell() {
    castReleased_ = true;
    const int spell = *casting_;
    const int level = spells_.castLevel(spell);
    log::info("Cast {} at level {}", magic_.spell(spell).name, level);
    switch (kindOf(spell)) {
        case Kind::NotYet: break;
        case Kind::Toggle:
            setSpellOn(spell, true);
            if (spell == kBerserker) berserkerCarry_ = 0;
            spawnEffect(kGatherSwirl, player_->x, player_->y).followsPlayer = true;
            showMessage(std::format("{} on", magic_.spell(spell).name));
            break;
        case Kind::Heal: {
            // Table 17 is the percent of the most HP restored.
            const int amount = std::max(1, playerMaxHp() * magic_.power(spell, level) / 100);
            playerHp_ = std::min(playerMaxHp(), playerHp_ + amount);
            spawnEffect(kHealGlow, player_->x, player_->y).followsPlayer = true;
            trainSpell(spell);
            break;
        }
        case Kind::Cast:
            // The controller makes the spell's effects from its next tick.
            for (auto& cast : casts_) {
                if (cast.spell == spell && cast.release < 0) cast.release = cast.tick;
            }
            break;
    }
}

void Game::showSpellHit(int spell, double x, double y) {
    const int option = hitOptionOf(spell);
    if (option >= 0) spawnEffect(option, x, y);
}

void Game::spellHits(Actor& enemy, int spell, int level, int power, int hit, int hitOption) {
    (void)level;
    const auto& s = enemy.placement.stats;
    const int element = magic_.spell(spell).element;
    if (hitOption >= 0) spawnEffect(hitOption, enemy.character.x, enemy.character.y);
    // Spells must beat the monster's magic evasion (0x45eb2b).
    if (random_.next(100) >= hitChance(hit, s.magicEvasion)) return;
    // Every monster hit is one use towards the spell's next level.
    trainSpell(spell);
    const int damage = spellDamage(power, element, s.element, s.magicDefense, random_.next(3), random_.next(4));
    log::info("{} hits for {}", magic_.spell(spell).name, damage);
    hurtEnemy(enemy, damage, true);
}

const CharacterModel* Game::effectModel(int option) {
    if (option <= 0) return nullptr;
    auto it = effectModels_.find(option);
    if (it == effectModels_.end()) {
        auto model = CharacterModel::load(*data_, std::format("Character/OPTION/{:08d}/Animation", option));
        if (!model) log::warn("No animation for effect {}", option);
        it = effectModels_.emplace(option, model ? std::make_unique<CharacterModel>(std::move(*model)) : nullptr).first;
    }
    return it->second.get();
}

// The most frames any direction of a chart has (GetMaxFrameCount).
int Game::effectLength(int option, int chart) {
    const CharacterModel* model = effectModel(option);
    if (!model || chart < 0 || static_cast<std::size_t>(chart) >= model->animations.charts.size()) return kMissingLength;
    int frames = 0;
    for (const auto& dir : model->animations.charts[static_cast<std::size_t>(chart)].directions) {
        frames = std::max(frames, static_cast<int>(dir.frameCount));
    }
    return frames > 0 ? frames : kMissingLength;
}

// New effects wait in pendingEffects_ until the tick ends, so the ones
// being stepped never move in memory. The reference is only good until the
// next spawn.
Game::Effect& Game::spawnEffect(int option, double x, double y, int chart) {
    Effect e;
    e.id = nextEffectId_++;
    e.option = option;
    e.chart = chart;
    e.x = x;
    e.y = y;
    e.life = effectLength(option, chart);
    pendingEffects_.push_back(std::move(e));
    return pendingEffects_.back();
}

Game::Effect* Game::findEffect(int id) {
    for (auto* list : {&effects_, &pendingEffects_}) {
        for (auto& e : *list) {
            if (e.id == id && !e.dead) return &e;
        }
    }
    return nullptr;
}

void Game::clearSpells() {
    casts_.clear();
    effects_.clear();
    pendingEffects_.clear();
    originalTicks_ = 0;
}

// One of the original's ticks of a spell controller. `t` counts from the
// start of the cast; R, the release, is when the hand reaches halfway.
void Game::stepCast(SpellCast& c) {
    const int t = c.tick++;
    const bool released = c.release >= 0 && t >= c.release;
    const int r = released ? t - c.release : -1;
    auto along = [&](double distance, double angle) {
        return std::pair{c.x + std::cos(angle) * distance, c.y + std::sin(angle) * distance};
    };
    auto hurts = [&](Effect& e, double box, bool area, int hitOption) {
        e.spell = c.spell;
        e.level = c.level;
        e.power = c.power;
        e.hit = c.hit;
        e.box = box;
        e.area = area;
        e.hitOption = hitOption;
    };
    // A missile from 180 in front of the caster, flying until it hits
    // something or a wall.
    auto bolt = [&](int option, double angle, double box) -> Effect& {
        const auto [x, y] = along(180, angle);
        Effect& e = spawnEffect(option, x, y);
        e.angle = angle;
        e.directional = true;
        e.speed = magic_.speed(c.spell, c.level);
        e.life = -1;
        e.diesOnWall = e.diesOnHit = true;
        hurts(e, box, false, hitOptionOf(c.spell));
        return e;
    };
    auto blockedAt = [&](double x, double y) { return !map_ || !map_->ground().walkable(x, y); };
    // Plasma's burst: lightning that hurts all around it, a splash and glitter.
    auto plasma = [&](double x, double y, int chart) {
        hurts(spawnEffect(kLightning, x, y, chart), 100, true, kShock);
        spawnEffect(kLightningSplash, x, y);
        spawnEffect(kLightningGlitter, x, y);
    };
    auto glow = [&](int option) { spawnEffect(option, c.x, c.y); };

    switch (c.spell) {
        case kFireBall:
        case kIceBolt:
            if (t == 0) glow(c.spell == kFireBall ? kCastFlame : kIceSwirl);
            if (r == 0) {
                bolt(c.spell == kFireBall ? kFireBallFly : kIceBoltFly, c.aim, 50);
                c.done = true;
            }
            break;
        case kPlasma: {
            // A line of bursts marching forward, one every 4 ticks.
            const int n = magic_.count(205, c.level);
            if (r >= 0 && r % 4 == 0) {
                const auto [x, y] = along(250 + 200 * c.step, c.aim);
                if (!c.blocked[0] && blockedAt(x, y)) c.blocked[0] = true;
                if (!c.blocked[0]) plasma(x, y, random_.next(4));
                if (++c.step >= n) c.done = true;
            }
            break;
        }
        case kHellFire:
            if (t == 3) glow(kFireRing);
            if (r == 0) {
                glow(kFireSpray);
                spawnEffect(kFireSpray, c.x, c.y, 1);
                // Everything within 150 of the caster's own square, once.
                Effect& area = spawnEffect(0, c.x, c.y);
                area.life = 1;
                hurts(area, kTargetHalf + 150, true, kHellFireBurst);
            }
            if (t >= 3 && r >= 0) c.done = true;
            break;
        case kIceBlast: {
            // Times from the gathering ice, not the release.
            const int l = effectLength(kIceGather);
            if (t == 3) glow(kIceGather);
            if (t == l) spawnEffect(kIceCrystals, c.x, c.y);
            if (t == l + 4) {
                Effect& area = spawnEffect(0, c.x, c.y);
                area.life = 1;
                hurts(area, kTargetHalf + 150, true, kFrostHit);
            }
            if (t == l + 15) spawnEffect(kIceMist, c.x, c.y);
            if (t >= std::max(l + 21, 3)) c.done = true;
            break;
        }
        case kEarthSpears: {
            // A line of spears, one every 8 ticks.
            const int n = magic_.count(206, c.level);
            if (r >= 0 && r % 8 == 0) {
                const auto [x, y] = along(250 + 300 * c.step, c.aim);
                if (!c.blocked[0] && blockedAt(x, y)) c.blocked[0] = true;
                if (!c.blocked[0]) hurts(spawnEffect(kEarthSpear, x, y), 150, true, -1);
                if (++c.step >= n) c.done = true;
            }
            break;
        }
        case kFlameStrike:
            if (t == 0) glow(kCastFlame);
            if (r == 0) {
                // Fireballs all round, starting at the aim, that home in.
                const int n = std::max(1, magic_.count(204, c.level));
                for (int i = 0; i < n; ++i) {
                    Effect& e = bolt(kFireBallFly, c.aim - i * kTau / n, 80);
                    e.turn = 20 * std::numbers::pi / 180;
                    e.life = 90;
                }
                c.done = true;
            }
            break;
        case kDreadDeathscythe: {
            // A fan of blades, 144 degrees wide at the most blades.
            const int n = std::max(1, magic_.count(204, c.level));
            const int most = std::max(1, magic_.counts[0][kSpellColumns - 1]);
            auto angle = [&](int i) {
                if (n == 1) return c.aim;
                const double width = n * (0.4 * kTau) / most;
                const double even = n % 2 == 0 ? 1.0 : 0.0;
                return c.aim - 0.5 * width + 0.5 * even * (width / n) + i * width / (n - 1);
            };
            if (t == 0) {
                glow(kIceSwirl);
                for (int i = 0; i < n; ++i) {
                    const auto [x, y] = along(150, angle(i));
                    Effect& e = spawnEffect(kBladeAppear, x, y);
                    e.angle = angle(i);
                    e.directional = true;
                    e.life = -1;  // until the spell leaves the hand
                    c.tracked.push_back(e.id);
                }
            }
            if (r == 0) {
                for (int id : c.tracked) {
                    if (Effect* e = findEffect(id)) e->dead = true;
                }
                for (int i = 0; i < n; ++i) bolt(kBladeFly, angle(i), 50).life = 90;
                c.done = true;
            }
            break;
        }
        case kLightningStorm: {
            // Four rings of bursts, every 4 ticks, 350 to 950 out.
            const int n = std::clamp(magic_.count(204, c.level), 1, static_cast<int>(c.blocked.size()));
            if (r >= 0 && r % 4 == 0) {
                for (int i = 0; i < n; ++i) {
                    const auto [x, y] = along(350 + 200 * c.step, c.aim + i * kTau / n);
                    if (!c.blocked[static_cast<std::size_t>(i)] && blockedAt(x, y)) c.blocked[static_cast<std::size_t>(i)] = true;
                    if (!c.blocked[static_cast<std::size_t>(i)]) plasma(x, y, random_.next(4));
                }
                if (++c.step >= 4) c.done = true;
            }
            break;
        }
        case kSonicBlade:
            if (r == 0) {
                // A short, fast cut that hurts all in its square and is gone.
                const auto [x, y] = along(100, c.aim);
                Effect& e = spawnEffect(kSonicBladeFly, x, y);
                e.angle = c.aim;
                e.directional = true;
                e.speed = magic_.speed(c.spell, c.level);
                e.life = 7;
                e.diesOnWall = e.diesOnHit = true;
                hurts(e, 80, true, kAirBurst);
                c.done = true;
            }
            break;
        case kMudJavelin:
            if (r == 0) {
                Effect& e = bolt(kMudFly, c.aim, 80);
                c.tracked = {e.id};
                c.trackedAt = {{e.x, e.y}};
            } else if (r > 0) {
                // Where the javelin ends, the mud bursts.
                if (const Effect* e = findEffect(c.tracked[0])) {
                    c.trackedAt[0] = {e->x, e->y};
                } else {
                    hurts(spawnEffect(kMudBurst, c.trackedAt[0].first, c.trackedAt[0].second), 240, true, -1);
                    c.done = true;
                }
            }
            break;
        case kElementalStrike: {
            // Orbs all round that home in; where each ends, four bursts
            // follow 4 ticks apart: fire, water, thunder, earth.
            static constexpr int kStageOption[] = {0, kFirePillar, kWaterBurst, kLightning, kEarthSpear};
            static constexpr int kStageHit[] = {-1, kFireBurst, kFrostHit, kShock, kHitSpark};
            if (t == 0) glow(kElementCast);
            if (r == 0) {
                const int n = magic_.count(207, c.level);
                for (int i = 0; i < n; ++i) {
                    Effect& e = bolt(kElementOrb, c.aim - i * kTau / n, 80);
                    e.directional = false;
                    e.turn = 20 * std::numbers::pi / 180;
                    e.animSpeed = std::max(1, magic_.speed(c.spell, c.level) * 1000 / 30);
                    e.life = effectLength(kElementOrb);
                    e.hitOption = -1;
                    c.tracked.push_back(e.id);
                    c.trackedAt.emplace_back(e.x, e.y);
                }
                c.stage.assign(static_cast<std::size_t>(n), 0);
                c.stageTicks.assign(static_cast<std::size_t>(n), 0);
                if (n == 0) c.done = true;
            } else if (r > 0) {
                bool finished = true;
                for (std::size_t i = 0; i < c.tracked.size(); ++i) {
                    if (c.stage[i] == 0) {
                        if (const Effect* e = findEffect(c.tracked[i])) {
                            c.trackedAt[i] = {e->x, e->y};
                        } else {
                            c.stage[i] = 1;
                        }
                    }
                    if (c.stage[i] != 0) {
                        if (c.stageTicks[i] % 4 == 0 && c.stage[i] != 5) {
                            const int stage = c.stage[i];
                            const auto [x, y] = c.trackedAt[i];
                            hurts(spawnEffect(kStageOption[stage], x, y), 240, true, kStageHit[stage]);
                            if (stage == 3) {
                                spawnEffect(kLightningSplash, x, y);
                                spawnEffect(kLightningGlitter, x, y);
                            }
                            ++c.stage[i];
                        }
                        ++c.stageTicks[i];
                    }
                    if (c.stage[i] != 5) finished = false;
                }
                if (finished) c.done = true;
            }
            break;
        }
        default: c.done = true; break;
    }
}

// One of the original's ticks of an effect: follow, home in, fly, hurt.
void Game::stepEffect(Effect& e) {
    if (e.followsPlayer && player_) {
        e.x = player_->x;
        e.y = player_->y;
    }
    if (e.turn > 0) {
        // Home in on the nearest monster.
        const Actor* nearest = nullptr;
        double best = 0;
        for (const auto& actor : actors_) {
            if (!actor.active || !actor.hostile() || !actor.alive()) continue;
            const double d = std::hypot(actor.character.x - e.x, actor.character.y - e.y);
            if (!nearest || d < best) {
                nearest = &actor;
                best = d;
            }
        }
        if (nearest) e.angle = turnTowards(e.angle, std::atan2(nearest->character.y - e.y, nearest->character.x - e.x), e.turn);
    }
    if (e.speed > 0) {
        e.x += std::cos(e.angle) * e.speed;
        e.y += std::sin(e.angle) * e.speed;
        e.travelled += e.speed;
        if (e.diesOnWall && (!map_ || !map_->ground().walkable(e.x, e.y))) e.dead = true;
        if (e.travelled >= kMissileRange) e.dead = true;
    }
    if (!e.dead && e.spell >= 0) {
        bool struck = false;
        for (std::size_t i = 0; i < actors_.size(); ++i) {
            Actor& actor = actors_[i];
            if (!actor.active || !actor.hostile() || !actor.alive()) continue;
            if (std::ranges::find(e.struck, i) != e.struck.end()) continue;
            const double reach = e.box + kTargetHalf;
            if (std::abs(actor.character.x - e.x) > reach || std::abs(actor.character.y - e.y) > reach) continue;
            e.struck.push_back(i);
            spellHits(actor, e.spell, e.level, e.power, e.hit, e.hitOption);
            struck = true;
            if (!e.area) break;
        }
        if (struck && e.diesOnHit) e.dead = true;
    }
    ++e.age;
    if (e.life >= 0 && e.age >= e.life) e.dead = true;
}

void Game::tickSpells() {
    for (originalTicks_ += kOriginalTicksPerTick; originalTicks_ >= 1.0; originalTicks_ -= 1.0) {
        for (auto& cast : casts_) {
            // A cast cut short before it left the hand makes nothing more.
            if (cast.release < 0 && !casting_) {
                for (int id : cast.tracked) {
                    if (Effect* e = findEffect(id)) e->dead = true;
                }
                cast.done = true;
            }
            if (!cast.done) stepCast(cast);
        }
        std::erase_if(casts_, [](const SpellCast& c) { return c.done; });
        for (auto& effect : effects_) {
            if (!effect.dead) stepEffect(effect);
        }
        std::erase_if(effects_, [](const Effect& e) { return e.dead; });
        for (auto& effect : pendingEffects_) effects_.push_back(std::move(effect));
        pendingEffects_.clear();
    }
    // Berserker drains MP every third tick (0x44f2f0) and ends with it.
    if (spellOn(kBerserker) && ticks_ % 3 == 0) {
        const int rate = magic_.berserker[0][MagicTables::column(spells_.castLevel(kBerserker))];
        playerMp_ += regenerate(playerMaxMp(), rate, berserkerCarry_);
        if (playerMp_ <= 0) {
            playerMp_ = 0;
            setSpellOn(kBerserker, false);
            showMessage("Berserker off");
        }
    }
    playerHp_ = std::min(playerHp_, playerMaxHp());
    playerMp_ = std::min(playerMp_, playerMaxMp());
}

// One effect, drawn where it stands; renderMap sorts effects in with the
// map's objects and characters.
void Game::renderEffect(const Effect& e, int camX, int camY) {
    const CharacterModel* model = effectModel(e.option);
    if (!model) return;
    const auto& charts = model->animations.charts;
    if (static_cast<std::size_t>(e.chart) >= charts.size()) return;
    const auto& chart = charts[static_cast<std::size_t>(e.chart)];
    // Its own direction if it has one, else the undirected one (8), else 0.
    const AnimationSet::Direction* dir = nullptr;
    const int facing = e.directional ? facingOf(std::cos(e.angle), std::sin(e.angle)) : 8;
    for (int d : {facing, 8, 0}) {
        if (static_cast<std::size_t>(d) < chart.directions.size() && chart.directions[static_cast<std::size_t>(d)].frameCount > 0) {
            dir = &chart.directions[static_cast<std::size_t>(d)];
            break;
        }
    }
    if (!dir) return;
    int frame = static_cast<int>(static_cast<long long>(e.age) * e.animSpeed / 1000);
    frame = chart.status == 1 ? frame % dir->frameCount : std::min(frame, dir->frameCount - 1);
    int sx, sy;
    worldToScreen(static_cast<int>(e.x), static_cast<int>(e.y), sx, sy);
    sx -= camX;
    sy -= camY;
    std::vector<const AnimationSet::Cell*> cells;
    for (const auto& layer : dir->layers) {
        if (frame < static_cast<int>(layer.size()) && layer[static_cast<std::size_t>(frame)].pattern >= 0) {
            cells.push_back(&layer[static_cast<std::size_t>(frame)]);
        }
    }
    std::ranges::stable_sort(cells, [](const auto* a, const auto* b) { return a->priority > b->priority; });
    for (const auto* cell : cells) {
        const bool shadow = cell->status == AnimationSet::kShadowStatus;
        const SpriteTexture* sprite = sprites_.get(shadow ? model->shadow : model->body, cell->pattern, false);
        if (!sprite) continue;
        // Status 16 adds its light to what is behind (flames, glows).
        SDL_SetTextureBlendMode(sprite->texture, cell->status & AnimationSet::kAdditiveStatus ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
        if (shadow) SDL_SetTextureColorMod(sprite->texture, 0, 0, 0);
        const int alpha = shadow ? 96 : std::clamp(cell->trans, std::int16_t{0}, std::int16_t{1000}) * 255 / 1000;
        SDL_SetTextureAlphaMod(sprite->texture, static_cast<Uint8>(alpha));
        SDL_Rect dst{sx + sprite->originX, sy + sprite->originY, sprite->width, sprite->height};
        SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
    }
}

void Game::renderSpells(int camX, int camY) {
    // A faint ring around the hero for each shield or Berserker that is on.
    // Ours: the original shows nothing while they are on.
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
    if (player_) {
        int sx, sy;
        worldToScreen(static_cast<int>(player_->x), static_cast<int>(player_->y), sx, sy);
        sx -= camX;
        sy -= camY;
        int ring = 0;
        for (int spell : {kBerserker, kEnergyShield, kMagicShield, kCounterBurst}) {
            if (!spellOn(spell)) continue;
            const SDL_Color c = spell == kBerserker ? SDL_Color{255, 60, 40, 255} : elementColor(magic_.spell(spell).element);
            const double pulse = 0.5 + 0.5 * std::sin(static_cast<double>(ticks_) * 0.3 + ring);
            SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, static_cast<Uint8>(70 + 60 * pulse));
            drawRing(renderer_, sx, sy, 40 + ring * 6);
            ++ring;
        }
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
}

void Game::renderSpellBar() {
    if (!player_ || showInventory_) return;
    // F1-F8 along the bottom right; the chosen spell is framed in gold.
    constexpr int kSlot = 36, kGap = 2;
    const int left = kScreenWidth - kSpellBarSize * (kSlot + kGap) - 6;
    const int top = kScreenHeight - kSlot - 6;
    for (int b = 0; b < kSpellBarSize; ++b) {
        const SDL_Rect r{left + b * (kSlot + kGap), top, kSlot, kSlot};
        const int spell = spells_.bar[static_cast<std::size_t>(b)];
        SDL_SetRenderDrawColor(renderer_, 20, 18, 26, 200);
        SDL_RenderFillRect(renderer_, &r);
        const bool chosen = spell >= 0 && spell == spells_.selected;
        SDL_SetRenderDrawColor(renderer_, chosen ? 230 : 110, chosen ? 190 : 100, chosen ? 80 : 80, 255);
        SDL_RenderDrawRect(renderer_, &r);
        drawText(renderer_, r.x + 2, r.y + 2, std::format("F{}", b + 1), {150, 150, 150, 255}, 1);
        if (spell < 0) continue;
        const SDL_Color c = elementColor(magic_.spell(spell).element);
        if (spellOn(spell)) {
            SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, 90);
            const SDL_Rect inner{r.x + 1, r.y + 1, r.w - 2, r.h - 2};
            SDL_RenderFillRect(renderer_, &inner);
        }
        const std::string_view name = barName(spell);
        drawText(renderer_, r.x + (r.w - textWidth(name, 1)) / 2, r.y + 14, name, c, 1);
        const std::string level = std::format("{}", spells_.castLevel(spell));
        drawText(renderer_, r.x + r.w - 2 - textWidth(level, 1), r.y + r.h - 9, level, {255, 255, 255, 255}, 1);
    }
}

}  // namespace sf

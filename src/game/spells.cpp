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
#include <optional>
#include <tuple>

#include "core/log.h"
#include "game/game.h"
#include "render/tiny_font.h"

namespace sf {

namespace {

constexpr double kTau = 2 * std::numbers::pi;
// The sound most spells' shots make when they land (their struct's words
// 0x76 and 0x77 set to bank 0, voice 20).
constexpr int kMagicHitSound = 20;
// A monster's judge rectangle: its placement's rect (+0x20).
JudgeRect judgeOf(const std::int32_t (&r)[4]) { return {r[0], r[1], r[2], r[3]}; }

// A square judge rectangle, `half` out from the middle each way.
constexpr JudgeRect square(int half) { return {-half, -half, half, half}; }
// How far Hell Fire's and Ice Blast's area reaches past the caster's own
// judge rectangle (0x42a860, 0x42cd70).
constexpr int kBurstReach = 150;
// Ours: the original lets a missile fly until it meets something or a
// wall. This stops one lost in open ground.
constexpr double kMissileRange = 6000.0;
// The height spells' missiles fly at: the hero's and monsters' (the
// controller's height word, +0x24, as the casts start them).
constexpr double kHeroMissileHeight = 200.0, kMonsterMissileHeight = 250.0;
// "MISS!!": OPTION 11000011's picture, starting this high over whoever
// was missed (0x417a60).
constexpr int kMissOption = 11000011;
constexpr double kMissHeight = 400.0;
// What an effect is drawn for when the content has no animation for it.
constexpr int kMissingLength = 16;
// Special items (class 4) that add 5 to Life and Mental Recovery while
// carried (0x44f2f0 looks for 0x5d75c83 and 0x5d75c84).
constexpr std::int32_t kSpiritualHeraldry = 98000003, kDemonFang = 98000004;

// OPTION animations (Character/OPTION/<id>).
enum Option {
    kFireSpray = 10000000, kHellFireBurst = 10000001, kFireRing = 10000002, kFireBallFly = 10000010,
    kFireBurst = 10000011, kCastFlame = 10000012, kLightning = 10000030, kLightningSplash = 10000031,
    kLightningGlitter = 10000032, kIceBoltFly = 10000040, kIceCrystals = 10000050, kIceGather = 10000051,
    kIceMist = 10000052, kEarthSpear = 10000060, kMedusaFly = 10000070, kBladeAppear = 10000080, kBladeFly = 10000081,
    kSonicBladeFly = 10000090, kElementOrb = 10000100, kMudFly = 10000110, kMudBurst = 10000111,
    kHitSpark = 11000000, kShock = 11000003, kFrostHit = 11000026, kIceSwirl = 11000027, kWaterBurst = 11000033,
    kStoneHit = 11000050, kHealGlow = 11000060, kGatherSwirl = 11000100, kAirBurst = 11000200, kElementCast = 11000210,
    kFreezeStart = 11000028, kFreezeHold = 11000029, kFreezeEnd = 11000033,
    kMoonGlow = 11000040, kIdentifyGlow = 11000230, kMagicShieldGlow = 11000241, kCounterBurstGlow = 11000251, kFirePillar = 12000000,
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
        case kMedusa: return kStoneHit;
        default: return -1;
    }
}

enum class Kind { Toggle, Moon, Heal, Cast, Companion, Transport, Identify };

Kind kindOf(int spell) {
    switch (spell) {
        case kTransport: return Kind::Transport;
        case kMoon: return Kind::Moon;
        case kIdentify: return Kind::Identify;
        case kExplosion: return Kind::Companion;  // the companion blows up (partner.cpp)
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
    showMessage(std::format("{} (level {})", magic_.spell(spell).name, castLevel(spell)));
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
    // The Debug tab's experience rate counts each use that many times.
    if (!spells_.train(magic_, spell, xpRate() / Settings::kXpRateNormal)) return;
    log::info("{} is now level {}", magic_.spell(spell).name, spells_.castLevel(spell));
    showMessage(std::format("{} is now level {}", magic_.spell(spell).name, spells_.castLevel(spell)));
}

void Game::startCast(double worldX, double worldY, std::optional<std::size_t> enemy) {
    if (casting_ || dialogue_ || openShop_) return;
    const int spell = spells_.selected;
    if (!spells_.knows(spell)) {
        showMessage("No spell chosen (F1-F8)");
        return;
    }
    // Power mode keeps Berserker, Moon and Energy Shield from being cast,
    // or cast again to turn them off (0x441c00).
    if (power_.active() && blockedInPowerMode(spell)) return;
    const Kind kind = kindOf(spell);
    // Not while Identify already waits for an item (0x441c00).
    if (kind == Kind::Identify && identifying_) return;
    if (kind == Kind::Transport && !transportAllowed()) return;
    // Turning a shield or Berserker off is free and needs no casting.
    if (kind == Kind::Toggle && spellOn(spell)) {
        setSpellOn(spell, false);
        showMessage(std::format("{} off", magic_.spell(spell).name));
        return;
    }
    if (kind == Kind::Heal && playerHp_ >= playerMaxHp()) return;  // as the original: not when full
    const int level = castLevel(spell);
    const int cost = mpCost(spell, level);
    if (playerMp_ < cost) {
        showMessage("Not enough MP");
        return;
    }
    playerMp_ -= cost;
    casting_ = spell;
    castTarget_ = {worldX, worldY};
    castEnemy_ = enemy;
    castReleased_ = false;
    walkTarget_.reset();
    attackTarget_.reset();
    talkTarget_.reset();
    mouseHeld_ = false;
    player_->faceTowards(worldX - player_->x, worldY - player_->y);
    const int chantSpeed = playerChantSpeed();
    // Heal has a casting animation of its own, and Moon and Transport cast
    // with it too (0x43d290, 0x43a260).
    int chart = spell == kHeal || spell == kMoon || spell == kTransport ? chart::kPlayerHeal : chart::kPlayerCast;
    if (!player_->hasChart(chart)) chart = player_->hasChart(chart::kPlayerCast) ? chart::kPlayerCast : chart::kPlayerPunch;
    player_->restart(chart, castSpeedFactor(magic_, spell, chantSpeed, power_.active(), overloaded(playerStats())));
    // Transport's gate appears as the cast begins.
    if (kind == Kind::Transport) openTransportGate();
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
    // Cast at a monster, it goes for where the monster is now.
    if (castEnemy_ && *castEnemy_ < actors_.size() && actors_[*castEnemy_].alive() && actors_[*castEnemy_].active) {
        const Character& t = actors_[*castEnemy_].character;
        castTarget_ = {t.x, t.y};
        for (auto& cast : casts_) {
            if (cast.spell == spell && cast.caster < 0 && cast.release < 0) cast.aim = std::atan2(t.y - cast.y, t.x - cast.x);
        }
    }
    castEnemy_.reset();
    const int level = castLevel(spell);
    log::info("Cast {} at level {}", magic_.spell(spell).name, level);
    switch (kindOf(spell)) {
        case Kind::Identify:
            // The inventory opens and the next unknown item clicked in the
            // bag is identified (0x43f8d0).
            spawnEffect(kIdentifyGlow, player_->x, player_->y).followsPlayer = true;
            identifying_ = true;
            openItemWindow();
            closeWarpWindow();
            showMessage("Choose an item to identify");
            break;
        case Kind::Transport: break;
        case Kind::Companion: orderPartnerExplosion(castTarget_.first, castTarget_.second); break;
        case Kind::Toggle:
            setSpellOn(spell, true);
            spawnEffect(kGatherSwirl, player_->x, player_->y).followsPlayer = true;
            showMessage(std::format("{} on", magic_.spell(spell).name));
            break;
        case Kind::Moon:
            // Cast again, it goes off (0x43d290); either way it costs the
            // casting. The companion's stats follow at once.
            setSpellOn(spell, !spellOn(spell));
            moonLevel_ = level;
            refreshPartnerStats();
            showMessage(std::format("{} {}", magic_.spell(spell).name, spellOn(spell) ? "on" : "off"));
            break;
        case Kind::Heal: {
            // Table 17 is the percent of the most HP restored.
            const int amount = std::max(1, playerMaxHp() * magic_.power(spell, level) / 100);
            playerHp_ = std::min(playerMaxHp(), playerHp_ + amount);
            spawnEffect(kHealGlow, player_->x, player_->y).followsPlayer = true;
            playSound(0x11);  // 0x43ca60
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

// A monster's spell leaves its hand (0x45b043): the same controller as
// the hero's, made at once with the monster as its owner. It aims where
// the action began, its power is the monster's own (words 35-37), its hit
// the spell's at the monster's level plus its bonus (words 41-43), and it
// goes for the hero and an ACTIVE companion. Some spells leave the hand
// 10 ticks into it (monsterSpell).
void Game::castMonsterSpell(Actor& enemy, int slot) {
    const auto& s = enemy.placement.stats;
    const auto k = static_cast<std::size_t>(std::clamp(slot, 0, 2));
    const int spell = s.spells[k];
    if (spell < 0 || spell >= kSpellCount) return;
    const auto how = monsterSpell(spell, random_.next(4));
    if (!how) {
        log::warn("Monster {} has no way to cast {}", enemy.gameId(), magic_.spell(spell).name);
        return;
    }
    SpellCast cast;
    cast.spell = spell;
    cast.level = std::clamp(s.spellLevel[k], 1, kSpellColumns);
    cast.power = s.spellPower[k];
    cast.hit = magic_.hit(spell, cast.level) + s.spellHit[k];
    cast.x = enemy.character.x;
    cast.y = enemy.character.y;
    cast.aim = enemy.aim;
    cast.release = how->release;
    cast.caster = static_cast<int>(&enemy - actors_.data());
    cast.hitOption = how->hitOption;
    cast.targetOnly = how->targetOnly;
    log::info("Monster {} casts {} at level {}", enemy.gameId(), magic_.spell(spell).name, cast.level);
    casts_.push_back(std::move(cast));
}

// A monster's spell reaching the hero (0x45e1e0, then 0x443cb0): its hit
// against their magic evasion; then (9 + r) x power / 10 less their magic
// defence (table 11 by their level in the spell's element), the hit's
// effect on them, the sound, and maybe a stun from tables 70-78 that the
// worn armour and accessories resist. Ice Bolt, Ice Blast and Dread
// Deathscythe freeze them in ice while it lasts.
void Game::monsterSpellHitsPlayer(const Effect& e) {
    const HeroStats stats = playerStats();
    if (random_.next(100) >= hitChance(e.hit, stats[HeroStats::kMagicEvasion])) {
        showMiss(player_->x, player_->y);
        return;
    }
    const int element = magic_.spell(e.spell).element;
    const int damage = damageToHero(magic_, e.power, element, playerElements(), stats[HeroStats::kMagicDefense], random_.next(3));
    log::info("{} hits the hero for {}", magic_.spell(e.spell).name, damage);
    if (e.hitOption >= 0) spawnEffect(e.hitOption, player_->x, player_->y).followsPlayer = true;
    playSound(e.hitSound, e.x, e.y);
    Actor* caster = static_cast<std::size_t>(e.caster) < actors_.size() ? &actors_[static_cast<std::size_t>(e.caster)] : nullptr;
    hurtPlayer(damage, true, caster, false);
    if (playerHp_ <= 0 || !player_) return;
    const auto st = heroSpellStun(magic_, e.spell, e.level, playerElements());
    const auto ticks = stunTicks(st.chance, st.length, st.uncapped, inventory_.guardModifier(ItemDef::kModStunChance),
                                 inventory_.guardModifier(ItemDef::kModStunLength), random_.next(100));
    if (!ticks) return;
    Stun stun;
    stun.total = *ticks;
    stun.angle = std::atan2(-(player_->y - e.y), player_->x - e.x);
    stun.pushed = !st.uncapped && inventory_.guardModifier(ItemDef::kModHighDamage) == 0;
    stunHero(stun);
    if (stunKindOf(e.spell) == StunKind::Freeze) iceOnHero(stun.total);
}

// The hero frozen in ice for `ticks`: the same ice as on a monster (the
// controllers 0x5217-0x521a) and its crackle.
void Game::iceOnHero(int ticks) {
    spawnEffect(kFreezeStart, player_->x, player_->y).followsPlayer = true;
    Effect& ice = spawnEffect(kFreezeHold + random_.next(4), player_->x, player_->y);
    ice.followsPlayer = true;
    ice.life = ticks;
    ice.then = kFreezeEnd;
    playSound(0x77, player_->x, player_->y);
}

// The same on an ACTIVE companion: its magic evasion and magic defence,
// by the damage rule its own blows take (0x45f9f0).
void Game::monsterSpellHitsPartner(const Effect& e) {
    const PartnerStats& st = partner_->stats;
    if (random_.next(100) >= hitChance(e.hit, st.magicEvasion)) {
        showMiss(partner_->character.x, partner_->character.y);
        return;
    }
    const int damage = spellDamage(e.power, magic_.spell(e.spell).element, st.element, st.magicDefense, random_.next(3), random_.next(4));
    log::info("{} hits the companion for {}", magic_.spell(e.spell).name, damage);
    if (e.hitOption >= 0) spawnEffect(e.hitOption, partner_->character.x, partner_->character.y).followsPartner = true;
    playSound(e.hitSound, e.x, e.y);
    hurtPartner(damage);
}

bool Game::spellHits(Actor& enemy, int spell, int level, int power, int hit, int hitOption) {
    const auto& s = enemy.placement.stats;
    const int element = magic_.spell(spell).element;
    if (hitOption >= 0) spawnEffect(hitOption, enemy.character.x, enemy.character.y);
    // Spells must beat the monster's magic evasion (0x45eb2b).
    if (random_.next(100) >= hitChance(hit, s.magicEvasion)) {
        showMiss(enemy.character.x, enemy.character.y);
        return false;
    }
    // Every monster hit is one use towards the spell's next level.
    trainSpell(spell);
    const int damage = spellDamage(power, element, s.element, s.magicDefense, random_.next(3), random_.next(4));
    log::info("{} hits for {}", magic_.spell(spell).name, damage);
    hurtEnemy(enemy, damage, true);
    // Then the spell's stun, as tables 70-78 give it for the monster's
    // element (0x459690).
    const auto stun = spellStun(magic_, spell, level, s.element, playerElements());
    stunEnemy(enemy, stun.chance, stun.length, stun.uncapped, -1, stunKindOf(spell));
    return true;
}

const CharacterModel* Game::effectModel(int option) {
    if (option < 0) return nullptr;  // -1: an unseen area that only hurts; 0 is the trap arrow
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

// "MISS!!" pops up over whoever was missed: a still picture, made here
// rather than by spawnEffect, hopping where it was put (stepHop).
void Game::showMiss(double x, double y) {
    log::info("Missed at ({:.0f}, {:.0f})", x, y);
    Effect e;
    e.id = nextEffectId_++;
    e.option = kMissOption;
    e.pattern = 0;
    e.x = x;
    e.y = y;
    e.life = -1;
    e.height = kMissHeight;
    e.hop = Effect::Hop{};
    e.hop->floor = kMissHeight;
    pendingEffects_.push_back(std::move(e));
}

// One of the original's ticks of the hop (0x42c900): up by a tenth of its
// speed, which falls by 100 a tick. Each time it comes down to where it
// started it bounces, at 300 and then 200; after the third landing it
// fades by a tenth a tick and is gone.
void Game::stepHop(Effect& e) {
    Effect::Hop& h = *e.hop;
    e.height += h.speed / 10;
    if (e.height < h.floor) {
        e.height = h.floor;
        if (h.landings == 0) h.speed = 300;
        if (h.landings == 1) h.speed = 200;
        if (h.landings != 3) ++h.landings;
    }
    h.speed -= 100;
    if (h.landings == 3) {
        e.light = std::max(0, h.fade);
        h.fade -= 100;
        if (h.fade == 0) e.dead = true;
    }
    ++e.age;
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
    mines_.clear();
    mineWait_ = 0;
    originalTicks_ = 0;
    identifying_ = false;
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
    auto hurts = [&](Effect& e, const JudgeRect& judge, bool area, int hitOption) {
        e.spell = c.spell;
        e.level = c.level;
        e.power = c.power;
        e.hit = c.hit;
        e.judge = judge;
        e.area = area;
        e.hitOption = hitOption;
        e.pierces = magic_.pierces(c.spell, c.level);
        e.caster = c.caster;
        e.targetOnly = c.targetOnly;
    };
    // What a monster's spell plays on whoever it hurts comes from its
    // cast (0x45b043); the hero's from the spell.
    const int hitOption = c.caster >= 0 ? c.hitOption : hitOptionOf(c.spell);
    // A missile from 180 in front of the caster, flying until it hits
    // something or a wall.
    auto bolt = [&](int option, double angle, int half) -> Effect& {
        const auto [x, y] = along(180, angle);
        Effect& e = spawnEffect(option, x, y);
        e.angle = angle;
        e.directional = true;
        e.speed = magic_.speed(c.spell, c.level);
        e.life = -1;
        e.diesOnWall = !c.targetOnly;
        e.diesOnHit = true;
        hurts(e, square(half), false, hitOption);
        e.hitSound = kMagicHitSound;
        e.height = c.caster >= 0 ? kMonsterMissileHeight : kHeroMissileHeight;
        return e;
    };
    auto blockedAt = [&](double x, double y) { return !map_ || !map_->ground().walkable(x, y); };
    // Plasma's burst: lightning that hurts all around it, a splash and glitter.
    auto plasma = [&](double x, double y, int chart) {
        hurts(spawnEffect(kLightning, x, y, chart), square(100), true, kShock);
        spawnEffect(kLightningSplash, x, y);
        spawnEffect(kLightningGlitter, x, y);
    };
    auto glow = [&](int option) { spawnEffect(option, c.x, c.y); };
    // The caster's own judge rectangle, which the controller is given
    // (the hero's + 0x2e8, as 0x439d10 passes it; a monster's its own).
    auto casterJudge = [&](const SpellCast& cast) {
        if (cast.caster >= 0 && static_cast<std::size_t>(cast.caster) < actors_.size()) return judgeOf(actors_[static_cast<std::size_t>(cast.caster)].placement.rect);
        return kHeroJudge;
    };

    switch (c.spell) {
        case kFireBall:
        case kIceBolt:
            if (t == 0) glow(c.spell == kFireBall ? kCastFlame : kIceSwirl);
            if (r == 0) {
                const Effect& e = bolt(c.spell == kFireBall ? kFireBallFly : kIceBoltFly, c.aim, 50);
                playSound(c.spell == kFireBall ? 0x13 : 0x5e, e.x, e.y);
                c.done = true;
            }
            break;
        case kPlasma: {
            // A line of bursts marching forward, one every 4 ticks.
            const int n = magic_.count(205, c.level);
            if (r >= 0 && r % 4 == 0) {
                const auto [x, y] = along(250 + 200 * c.step, c.aim);
                if (!c.blocked[0] && blockedAt(x, y)) c.blocked[0] = true;
                if (!c.blocked[0]) {
                    plasma(x, y, random_.next(4));
                    playSound(0x15, x, y);
                }
                if (++c.step >= n) c.done = true;
            }
            break;
        }
        case kHellFire:
            // Each tick it looks up where its owner stands now (0x42a860).
            if (c.caster < 0 && player_) {
                c.x = player_->x;
                c.y = player_->y;
            } else if (c.caster >= 0 && static_cast<std::size_t>(c.caster) < actors_.size()) {
                c.x = actors_[static_cast<std::size_t>(c.caster)].character.x;
                c.y = actors_[static_cast<std::size_t>(c.caster)].character.y;
            }
            if (t == 3) glow(kFireRing);
            if (r == 0) {
                glow(kFireSpray);
                spawnEffect(kFireSpray, c.x, c.y, 1);
                // Everything within 150 of the caster's own judge
                // rectangle, once.
                Effect& area = spawnEffect(-1, c.x, c.y);
                area.life = 1;
                hurts(area, grown(casterJudge(c), kBurstReach), true, kHellFireBurst);
                area.hitSound = kMagicHitSound;
                playSound(0x1d, c.x, c.y);
                playSound(0x17, c.x, c.y);
            }
            if (t >= 3 && r >= 0) c.done = true;
            break;
        case kIceBlast: {
            // Times from the gathering ice, not the release.
            const int l = effectLength(kIceGather);
            if (t == 3) glow(kIceGather);
            if (t == l) spawnEffect(kIceCrystals, c.x, c.y);
            if (t == l + 4) {
                Effect& area = spawnEffect(-1, c.x, c.y);
                area.life = 1;
                hurts(area, grown(casterJudge(c), kBurstReach), true, kFrostHit);
                area.hitSound = kMagicHitSound;
            }
            // Six times, every 3 ticks from L + 6 to L + 21.
            if (t >= l + 6 && t <= l + 21 && (t - l - 6) % 3 == 0) playSound(0x16, c.x, c.y);
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
                if (!c.blocked[0]) {
                    hurts(spawnEffect(kEarthSpear, x, y), square(150), true, c.caster >= 0 ? c.hitOption : -1);
                    playSound(0x16, x, y);
                }
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
                // Once, where the last fireball starts.
                const auto [x, y] = along(180, c.aim - (n - 1) * kTau / n);
                playSound(0x13, x, y);
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
                const auto [x, y] = along(180, angle(n - 1));
                playSound(0x5e, x, y);
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
                // Once a ring, at its last ray, even when every ray is blocked.
                const auto [x, y] = along(350 + 200 * c.step, c.aim + (n - 1) * kTau / n);
                playSound(0x15, x, y);
                if (++c.step >= 4) c.done = true;
            }
            break;
        }
        case kMedusa:
            // A missile that turns what it hits to stone (its stun).
            if (r == 0) {
                const Effect& e = bolt(kMedusaFly, c.aim, 50);
                playSound(0x16, e.x, e.y);
                c.done = true;
            }
            break;
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
                hurts(e, square(80), true, kAirBurst);
                e.hitSound = 155;
                playSound(0x9a, player_->x, player_->y);  // as the cut is made (0x43e5e0)
                c.done = true;
            }
            break;
        case kMudJavelin:
            if (r == 0) {
                Effect& e = bolt(kMudFly, c.aim, 80);
                playSound(0x13, e.x, e.y);
                c.tracked = {e.id};
                c.trackedAt = {{e.x, e.y}};
            } else if (r > 0) {
                // Where the javelin ends, the mud bursts.
                if (const Effect* e = findEffect(c.tracked[0])) {
                    c.trackedAt[0] = {e->x, e->y};
                } else {
                    Effect& burst = spawnEffect(kMudBurst, c.trackedAt[0].first, c.trackedAt[0].second);
                    hurts(burst, square(240), true, -1);
                    burst.hitSound = kMagicHitSound;
                    playSound(0x16, burst.x, burst.y);
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
                if (n > 0) {
                    const auto [x, y] = along(180, c.aim - (n - 1) * kTau / n);
                    playSound(0x13, x, y);
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
                            Effect& burst = spawnEffect(kStageOption[stage], x, y);
                            hurts(burst, square(240), true, kStageHit[stage]);
                            burst.hitSound = kMagicHitSound;
                            if (stage == 4) playSound(0x16, x, y);
                            playSound(0x13, x, y);
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
    if (e.hop) {
        stepHop(e);
        return;
    }
    if (e.trap) {
        stepTrap(e);
        return;
    }
    if (e.shot) {
        stepShot(e);
        return;
    }
    if (e.followsPlayer && player_) {
        e.x = player_->x;
        e.y = player_->y;
    }
    if (e.follows >= 0) {
        if (const auto at = characterAt(e.follows)) std::tie(e.x, e.y) = *at;
    }
    if (e.followsPartner && partner_) {
        e.x = partner_->character.x;
        e.y = partner_->character.y;
    }
    if (e.followsActor >= 0 && static_cast<std::size_t>(e.followsActor) < actors_.size()) {
        e.x = actors_[static_cast<std::size_t>(e.followsActor)].character.x;
        e.y = actors_[static_cast<std::size_t>(e.followsActor)].character.y;
    }
    if (e.turn > 0) {
        // Home in on the nearest monster, or for a monster's spell the
        // nearest of the hero and an ACTIVE companion.
        std::optional<std::pair<double, double>> nearest;
        double best = 0;
        auto consider = [&](double x, double y) {
            const double d = std::hypot(x - e.x, y - e.y);
            if (!nearest || d < best) {
                nearest = {x, y};
                best = d;
            }
        };
        if (e.homesOnPlayer) {
            if (player_) consider(player_->x, player_->y);
        } else if (e.caster >= 0) {
            if (player_) consider(player_->x, player_->y);
            if (partnerAlive() && partnerRecord_.active && !e.targetOnly) consider(partner_->character.x, partner_->character.y);
        } else {
            for (const auto& actor : actors_) {
                if (actor.active && actor.hostile() && actor.alive()) consider(actor.character.x, actor.character.y);
            }
        }
        if (nearest) e.angle = turnTowards(e.angle, std::atan2(nearest->second - e.y, nearest->first - e.x), e.turn);
    }
    if (e.speed > 0) {
        e.x += std::cos(e.angle) * e.speed;
        e.y += std::sin(e.angle) * e.speed;
        e.travelled += e.speed;
        if (e.diesOnWall && (!map_ || !map_->ground().walkable(e.x, e.y))) e.dead = true;
        if (e.travelled >= kMissileRange) e.dead = true;
        if (e.homesOnPlayer && player_ && std::hypot(player_->x - e.x, player_->y - e.y) <= e.speed) e.dead = true;
    }
    if (!e.dead && e.spell >= 0 && e.caster >= 0) {
        // A monster's spell: the hero, then an ACTIVE companion, each once.
        bool struck = false;
        if (player_ && !e.struckHero && judgesOverlap(e.x, e.y, e.judge, player_->x, player_->y, kHeroJudge)) {
            e.struckHero = struck = true;
            monsterSpellHitsPlayer(e);
        }
        if ((!struck || e.area) && !e.targetOnly && !e.struckPartner && partnerAlive() && partnerRecord_.active &&
            judgesOverlap(e.x, e.y, e.judge, partner_->character.x, partner_->character.y, kPartnerJudge)) {
            e.struckPartner = struck = true;
            monsterSpellHitsPartner(e);
        }
        if (struck && e.diesOnHit && !e.pierces) e.dead = true;
    } else if (!e.dead && e.spell >= 0) {
        bool struck = false, sounded = false;
        for (std::size_t i = 0; i < actors_.size(); ++i) {
            Actor& actor = actors_[i];
            if (!actor.active || !actor.hostile() || !actor.alive()) continue;
            if (std::ranges::find(e.struck, i) != e.struck.end()) continue;
            if (!judgesOverlap(e.x, e.y, e.judge, actor.character.x, actor.character.y, judgeOf(actor.placement.rect))) continue;
            e.struck.push_back(i);
            if (spellHits(actor, e.spell, e.level, e.power, e.hit, e.hitOption) && !sounded) {
                playSound(e.hitSound, e.x, e.y);
                sounded = true;
            }
            struck = true;
            if (!e.area) break;
        }
        if (struck && e.diesOnHit && !e.pierces) e.dead = true;
    }
    ++e.age;
    if (e.life >= 0 && e.age >= e.life) {
        e.dead = true;
        if (e.then > 0) spawnEffect(e.then, e.x, e.y).followsActor = e.followsActor;
    }
}

// Every third of the original's ticks (0x44f2f0): the Life Recovery
// modifiers worn, and 5 more with the Spiritual Heraldry, give back that
// many hundredths of a percent of the hero's health, and Mental Recovery
// (5 more with the Demon Fang), Berserker's drain and Moon's together of
// their mental power. Recovery never takes health below 1; Berserker and
// Moon end when mental power runs out (0x443490).
void Game::regenerateHero() {
    if (!player_) return;
    int life = inventory_.modifierEvenBroken(ItemDef::kModLifeRecovery);
    if (inventory_.carries(ItemDef::Class::Special, kSpiritualHeraldry)) life += 5;
    if (playerHp_ > 0) {
        playerHp_ = std::clamp(playerHp_ + regenerate(playerMaxHp(), life, lifeCarry_), 1, playerMaxHp());
    }
    int mental = inventory_.modifierEvenBroken(ItemDef::kModMentalRecovery);
    if (inventory_.carries(ItemDef::Class::Special, kDemonFang)) mental += 5;
    if (spellOn(kBerserker)) mental += magic_.berserker[0][MagicTables::column(castLevel(kBerserker))];
    if (spellOn(kMoon)) mental += magic_.moon[0][MagicTables::column(moonLevel_)];
    playerMp_ = std::clamp(playerMp_ + regenerate(playerMaxMp(), mental, mentalCarry_), 0, playerMaxMp());
    if (playerMp_ > 0) return;
    if (spellOn(kBerserker)) {
        setSpellOn(kBerserker, false);
        showMessage("Berserker off");
    }
    if (spellOn(kMoon)) {
        setSpellOn(kMoon, false);
        refreshPartnerStats();
        showMessage("Moon off");
    }
}

void Game::tickSpells() {
    for (originalTicks_ += kOriginalTicksPerTick; originalTicks_ >= 1.0; originalTicks_ -= 1.0) {
        for (auto& cast : casts_) {
            // A cast cut short before it left the hand makes nothing more.
            if (cast.caster < 0 && cast.release < 0 && !casting_) {
                for (int id : cast.tracked) {
                    if (Effect* e = findEffect(id)) e->dead = true;
                }
                cast.done = true;
            }
            if (!cast.done) stepCast(cast);
        }
        std::erase_if(casts_, [](const SpellCast& c) { return c.done; });
        if (mineWait_ > 0) --mineWait_;
        for (auto& mine : mines_) stepMine(mine);
        std::erase_if(mines_, [](const Mine& m) { return m.done; });
        for (auto& effect : effects_) {
            if (!effect.dead) stepEffect(effect);
        }
        std::erase_if(effects_, [](const Effect& e) { return e.dead; });
        for (auto& effect : pendingEffects_) effects_.push_back(std::move(effect));
        pendingEffects_.clear();
        if (++regenTicks_ % 3 == 0) regenerateHero();
    }
    // While Moon is on, the companion glows (0x45f550).
    const bool glows = spellOn(kMoon) && partner_ && partner_->hp > 0 && partner_->state != Partner::State::Dead;
    Effect* glow = moonGlow_ ? findEffect(moonGlow_) : nullptr;
    if (glows && !glow && player_) {
        Effect& e = spawnEffect(kMoonGlow, partner_->character.x, partner_->character.y);
        e.followsPartner = true;
        e.life = -1;
        moonGlow_ = e.id;
    } else if (!glows && glow) {
        glow->dead = true;
    }
    playerHp_ = std::min(playerHp_, playerMaxHp());
    playerMp_ = std::min(playerMp_, playerMaxMp());
}

// One effect, drawn where it stands; renderMap sorts effects in with the
// map's objects and characters.
void Game::renderEffect(const Effect& e, int camX, int camY) {
    if (e.pattern >= 0) {
        // A still picture from the folder's Pattern bank (the mine).
        const SpriteBank* bank = itemBank(std::format("Character/OPTION/{:08d}/Pattern", e.option));
        const SpriteTexture* sprite = bank ? sprites_.get(*bank, e.pattern, false) : nullptr;
        if (!sprite) return;
        int sx, sy;
        worldToScreen(static_cast<int>(e.x), static_cast<int>(e.y), sx, sy);
        sy -= static_cast<int>(e.height * projection.y / 100);
        const auto light = static_cast<Uint8>(std::clamp(e.light, 0, 1000) * 255 / 1000);
        SDL_SetTextureBlendMode(sprite->texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(sprite->texture, 255);
        SDL_SetTextureColorMod(sprite->texture, light, light, light);
        const SDL_Rect dst{sx - camX + sprite->originX, sy - camY + sprite->originY, sprite->width, sprite->height};
        SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
        return;
    }
    // A volley's arrow is not seen until it starts to fall.
    if (e.shot && e.shot->target >= 0 && e.shot->fall < 0) return;
    const CharacterModel* model = effectModel(e.option);
    if (!model) return;
    const auto& charts = model->animations.charts;
    if (static_cast<std::size_t>(e.chart) >= charts.size()) return;
    const auto& chart = charts[static_cast<std::size_t>(e.chart)];
    // Its own direction if it has one, else the undirected one (8), else 0.
    const AnimationSet::Direction* dir = nullptr;
    const int facing = e.facing >= 0 ? e.facing : e.directional ? facingOf(std::cos(e.angle), std::sin(e.angle)) : 8;
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
    // Height lifts it up the screen, at the ground's vertical scale (ours:
    // the original passes it to its renderer, whose scale is not read yet).
    sy -= camY + static_cast<int>(e.height * projection.y / 100);
    std::vector<const AnimationSet::Cell*> cells;
    for (std::size_t l = 0; l < dir->layers.size(); ++l) {
        const auto& layer = dir->layers[l];
        if (l < 32 && (e.hiddenLayers >> l & 1u)) continue;
        if (frame < static_cast<int>(layer.size()) && layer[static_cast<std::size_t>(frame)].pattern >= 0) {
            cells.push_back(&layer[static_cast<std::size_t>(frame)]);
        }
    }
    std::ranges::stable_sort(cells, [](const auto* a, const auto* b) { return a->priority > b->priority; });
    for (const auto* cell : cells) {
        const bool shadow = AnimationSet::isShadow(cell->status);
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

// The magic/attack bar above the panel, as ShadowFlare.exe lays it out
// (0x404ee0 draws it, 0x447570 takes its clicks): the eight slots of
// F1-F8 from x 224, 4 pixels more before each group of four, then the
// attack 4 further on. The chosen one is MagicIcon.njp's 26 x 26 picture
// at y 382 (spell + 2; 0 the attack), the rest MagicBarIcon.njp's 16 x 16
// at y 392 (spell + 4; 2 the attack, 3 an empty slot or a spell not
// known). The bar moves right while a window covers the left of the
// screen (to 344) and left for the item window (to 124).
std::vector<Game::BarIcon> Game::spellBarLayout() const {
    std::vector<BarIcon> icons;
    if (!player_ || missionsOpen_ || menuOpen_) return icons;
    const bool left = showStatus_ || magicOpen_ || openShop_.has_value() || mapOpen_ || warpOpen_;
    if (left && rightWindowOpen()) return icons;
    int x = left ? 344 : rightWindowOpen() ? 124 : 224;
    for (int b = 0; b < kSpellBarSize; ++b) {
        if (b % 4 == 0) x += 4;
        const int spell = spells_.bar[static_cast<std::size_t>(b)];
        if (!spells_.knows(spell)) {
            icons.push_back({x, 392, 16, -1, false});
            x += 16;
        } else if (spell == spells_.selected) {
            icons.push_back({x, 382, 26, spell, true});
            x += 26;
        } else {
            icons.push_back({x, 392, 16, spell, false});
            x += 16;
        }
    }
    const bool attack = !spells_.knows(spells_.selected);
    icons.push_back(attack ? BarIcon{x + 4, 382, 26, kAttackIcon, true} : BarIcon{x + 4, 392, 16, kAttackIcon, false});
    return icons;
}

bool Game::spellBarClick(int x, int y) {
    for (const BarIcon& icon : spellBarLayout()) {
        if (x < icon.x || x >= icon.x + icon.size || y < icon.y || y >= 408) continue;
        // The attack icon always clicks; a spell only when it is not the
        // chosen one already (0x447570).
        if (icon.spell == kAttackIcon) {
            playSound(0x3a);
            spells_.selected = -1;
            showMessage("Attack");
        } else if (icon.spell >= 0 && !icon.chosen) {
            playSound(0x3a);
            spells_.selected = icon.spell;
            showMessage(std::format("{} (level {})", magic_.spell(icon.spell).name, castLevel(icon.spell)));
        }
        return true;
    }
    return false;
}

void Game::renderSpellBar() {
    const SpriteBank* big = itemBank("System/Game/Pattern/MagicIcon");
    const SpriteBank* small = itemBank("System/Game/Pattern/MagicBarIcon");
    for (const BarIcon& icon : spellBarLayout()) {
        const int pattern = icon.spell == kAttackIcon ? (icon.chosen ? 0 : 2)
                            : icon.spell < 0          ? 3
                            : icon.chosen             ? icon.spell + 2
                                                      : icon.spell + 4;
        const SpriteBank* bank = icon.chosen ? big : small;
        if (const SpriteTexture* t = bank ? sprites_.get(*bank, pattern, false) : nullptr) {
            const SDL_Rect dst{icon.x, icon.y, t->width, t->height};
            SDL_RenderCopy(renderer_, t->texture, nullptr, &dst);
            continue;
        }
        // Without the pictures: a box with the spell's short name.
        const SDL_Rect r{icon.x, icon.y, icon.size, icon.size};
        SDL_SetRenderDrawColor(renderer_, 20, 18, 26, 220);
        SDL_RenderFillRect(renderer_, &r);
        SDL_SetRenderDrawColor(renderer_, icon.chosen ? 230 : 110, icon.chosen ? 190 : 100, 80, 255);
        SDL_RenderDrawRect(renderer_, &r);
        if (icon.spell == kAttackIcon) {
            drawText(renderer_, r.x + 2, r.y + 4, "ATK", {220, 220, 220, 255}, 1);
        } else if (icon.spell >= 0) {
            drawText(renderer_, r.x + 2, r.y + 4, barName(icon.spell).substr(0, icon.chosen ? 4 : 2),
                     elementColor(magic_.spell(icon.spell).element), 1);
        }
    }
}

}  // namespace sf

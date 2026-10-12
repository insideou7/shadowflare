// Spells: choosing them, casting them, what they do and how they look.
//
// The numbers come from the original (logic/magic.h, docs/magic.md). How a
// missile flies and how far a burst reaches are ours, and so are the
// effects, drawn from simple shapes in each element's colour.
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include "core/log.h"
#include "game/game.h"
#include "render/tiny_font.h"

namespace sf {

namespace {

// Ours: missiles fly table 35's speed x 1.5 world units a tick (Fire Ball
// about twice walking pace) for at most this far; bursts reach this far
// around the point cast at.
constexpr double kMissileScale = 1.5;
constexpr double kMissileRange = 2400.0;
constexpr double kMissileRadius = 110.0;  // how close it must pass to hit
constexpr double kBurstRadius = 320.0;

enum class Kind { NotYet, Toggle, Heal, Missile, Burst };

Kind kindOf(const MagicTables& t, int spell) {
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
    return t.speed(spell, 1) > 0 ? Kind::Missile : Kind::Burst;
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

void fillCircle(SDL_Renderer* r, int cx, int cy, int radius) {
    for (int dy = -radius; dy <= radius; ++dy) {
        const int dx = static_cast<int>(std::sqrt(static_cast<double>(radius * radius - dy * dy)));
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
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
    const Kind kind = kindOf(magic_, spell);
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
    const int chart = player_->hasChart(chart::kPlayerCast) ? chart::kPlayerCast : chart::kPlayerPunch;
    player_->restart(chart, castSpeedFactor(magic_, spell, chantSpeed));
}

void Game::releaseSpell() {
    castReleased_ = true;
    const int spell = *casting_;
    const int level = spells_.castLevel(spell);
    const int element = magic_.spell(spell).element;
    const auto [tx, ty] = castTarget_;
    log::info("Cast {} at level {}", magic_.spell(spell).name, level);
    switch (kindOf(magic_, spell)) {
        case Kind::NotYet: break;
        case Kind::Toggle:
            setSpellOn(spell, true);
            if (spell == kBerserker) berserkerCarry_ = 0;
            showMessage(std::format("{} on", magic_.spell(spell).name));
            break;
        case Kind::Heal: {
            // Table 17 is the percent of the most HP restored.
            const int amount = std::max(1, playerMaxHp() * magic_.power(spell, level) / 100);
            playerHp_ = std::min(playerMaxHp(), playerHp_ + amount);
            spellEffects_.push_back({SpellEffect::Kind::Heal, element, player_->x, player_->y, 90, 0, 14});
            trainSpell(spell);
            break;
        }
        case Kind::Missile: {
            const double dx = tx - player_->x, dy = ty - player_->y;
            const double length = std::max(1.0, std::hypot(dx, dy));
            const double speed = magic_.speed(spell, level) * kMissileScale;
            missiles_.push_back({spell, level, spellPower(spell, level), spellHit(spell, level), player_->x, player_->y,
                                 dx / length * speed, dy / length * speed, 0});
            break;
        }
        case Kind::Burst: {
            spellEffects_.push_back({SpellEffect::Kind::Burst, element, tx, ty, kBurstRadius, 0, 10});
            const int power = spellPower(spell, level), hit = spellHit(spell, level);
            for (auto& actor : actors_) {
                if (!actor.active || !actor.hostile() || !actor.alive()) continue;
                if (std::hypot(actor.character.x - tx, actor.character.y - ty) <= kBurstRadius) {
                    spellHits(actor, spell, level, power, hit);
                }
            }
            break;
        }
    }
}

void Game::spellHits(Actor& enemy, int spell, int level, int power, int hit) {
    (void)level;
    const auto& s = enemy.placement.stats;
    const int element = magic_.spell(spell).element;
    spellEffects_.push_back({SpellEffect::Kind::Spark, element, enemy.character.x, enemy.character.y, 100, 0, 8});
    // Spells must beat the monster's magic evasion (0x45eb2b).
    if (random_.next(100) >= hitChance(hit, s.magicEvasion)) return;
    // Every monster hit is one use towards the spell's next level.
    trainSpell(spell);
    const int damage = spellDamage(power, element, s.element, s.magicDefense, random_.next(3), random_.next(4));
    log::info("{} hits for {}", magic_.spell(spell).name, damage);
    hurtEnemy(enemy, damage, true);
}

void Game::tickSpells() {
    // Missiles hit the first monster they pass close to.
    for (auto it = missiles_.begin(); it != missiles_.end();) {
        Missile& m = *it;
        m.x += m.dx;
        m.y += m.dy;
        m.travelled += std::hypot(m.dx, m.dy);
        bool done = m.travelled >= kMissileRange || !map_ || !map_->ground().walkable(m.x, m.y);
        for (auto& actor : actors_) {
            if (done) break;
            if (!actor.active || !actor.hostile() || !actor.alive()) continue;
            if (std::hypot(actor.character.x - m.x, actor.character.y - m.y) <= kMissileRadius) {
                spellHits(actor, m.spell, m.level, m.power, m.hit);
                done = true;
            }
        }
        it = done ? missiles_.erase(it) : it + 1;
    }
    for (auto it = spellEffects_.begin(); it != spellEffects_.end();) {
        it = ++it->ticks >= it->length ? spellEffects_.erase(it) : it + 1;
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

void Game::renderSpells(int camX, int camY) {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
    auto toScreen = [&](double wx, double wy, int& sx, int& sy) {
        worldToScreen(static_cast<int>(wx), static_cast<int>(wy), sx, sy);
        sx -= camX;
        sy -= camY;
    };
    for (const Missile& m : missiles_) {
        const SDL_Color c = elementColor(magic_.spell(m.spell).element);
        // A bright head with a fading tail, a little above the ground.
        for (int k = 4; k >= 0; --k) {
            int sx, sy;
            toScreen(m.x - m.dx * k * 0.35, m.y - m.dy * k * 0.35, sx, sy);
            const Uint8 alpha = static_cast<Uint8>(200 - k * 35);
            SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, alpha);
            fillCircle(renderer_, sx, sy - 40, 7 - k);
        }
        int sx, sy;
        toScreen(m.x, m.y, sx, sy);
        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 220);
        fillCircle(renderer_, sx, sy - 40, 3);
    }
    for (const SpellEffect& e : spellEffects_) {
        const SDL_Color c = elementColor(e.element);
        const double t = static_cast<double>(e.ticks + 1) / e.length;
        const Uint8 alpha = static_cast<Uint8>(220 * (1.0 - t));
        int sx, sy;
        toScreen(e.x, e.y, sx, sy);
        SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, alpha);
        switch (e.kind) {
            case SpellEffect::Kind::Burst: {
                // Rings spreading over the ground from the point cast at.
                int rx, ry;
                toScreen(e.x + e.radius, e.y, rx, ry);
                const double screenRadius = std::hypot(rx - sx, ry - sy);
                for (int ring = 0; ring < 3; ++ring) drawRing(renderer_, sx, sy, screenRadius * t * (1.0 - ring * 0.15));
                fillCircle(renderer_, sx, sy - 10, static_cast<int>(18 * (1.0 - t)) + 2);
                break;
            }
            case SpellEffect::Kind::Heal:
                // Sparks rising around the hero.
                for (int k = 0; k < 10; ++k) {
                    const double a = k * 2 * std::numbers::pi / 10 + t * 2;
                    const int px = sx + static_cast<int>(28 * std::cos(a));
                    const int py = sy - static_cast<int>(20 + 70 * t) + static_cast<int>(8 * std::sin(a));
                    SDL_SetRenderDrawColor(renderer_, 140, 255, 150, alpha);
                    fillCircle(renderer_, px, py, 2);
                }
                break;
            case SpellEffect::Kind::Spark:
                // A flash where a spell lands.
                for (int k = 0; k < 8; ++k) {
                    const double a = k * 2 * std::numbers::pi / 8;
                    const double r = 6 + 26 * t;
                    SDL_RenderDrawLine(renderer_, sx, sy - 40, sx + static_cast<int>(r * std::cos(a)),
                                       sy - 40 + static_cast<int>(r * std::sin(a)));
                }
                break;
        }
    }
    // A faint ring around the hero for each shield or Berserker that is on.
    if (player_) {
        int sx, sy;
        toScreen(player_->x, player_->y, sx, sy);
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
    // Above the belt pocket, which sits where the original's does.
    const int top = kBeltTop - kSlot - 6;
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

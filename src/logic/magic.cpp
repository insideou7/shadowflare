#include "logic/magic.h"

#include <cmath>

#include "formats/table_data.h"

namespace sf {
namespace {

// Anchors of the elements on the attribute disc (0x44fba0).
constexpr int kDiscAnchors[kElementCount][2] = {
    {0, 20000}, {0, -20000}, {-20000, 0}, {20000, 0}, {14140, -14140}, {-14140, 14140}, {-14140, -14140}, {14140, 14140},
};

// The casting animation's speed steps (the table the cast code indexes).
constexpr double kCastSteps[10] = {0.6, 0.7, 0.8, 1.0, 1.15, 1.3, 1.45, 1.6, 1.75, 1.9};

// stat + stat * percent / 100, truncated towards zero as the exe does.
int addPercent(int stat, int percent) { return stat + stat * percent / 100; }

// Which HeroStats entry row r (1-12) of table 201 changes: the same order
// without strength.
HeroStats::Stat berserkerStat(int row) { return static_cast<HeroStats::Stat>(row <= 4 ? row - 1 : row); }

int elementFactor(const std::array<int, 21>& table, int level) {
    return table[static_cast<std::size_t>(std::clamp(level, -10, 10) + 10)];
}

int levelIn(const ElementLevels& levels, int element) {
    return element >= 0 && element < kElementCount ? levels[static_cast<std::size_t>(element)] : 0;
}

}  // namespace

std::string_view elementName(int element) {
    constexpr std::string_view kNames[kElementCount] = {"Fire", "Water", "Earth", "Thunder", "Holy", "Dark", "Gel", "Metal"};
    return element >= 0 && element < kElementCount ? kNames[element] : "";
}

ElementLevels discElementLevels(int x, int y) {
    ElementLevels levels{};
    for (int e = 0; e < kElementCount; ++e) {
        const double d = std::hypot(static_cast<double>(x - kDiscAnchors[e][0]), static_cast<double>(y - kDiscAnchors[e][1]));
        levels[static_cast<std::size_t>(e)] = (20000 - static_cast<int>(d)) / 1000;
    }
    return levels;
}

ElementLevels elementLevels(const ElementLevels& base, const ElementLevels& worn) {
    ElementLevels levels{};
    for (std::size_t e = 0; e < levels.size(); ++e) levels[e] = std::clamp(base[e] + worn[e], -10, 10);
    return levels;
}

MagicTables MagicTables::fromTables(const GameTables& tables) {
    MagicTables t = standard();
    auto grid = [&](int no, auto&& set) {
        const auto* table = tables.find(no);
        if (!table) return;
        for (int r = 0; r < table->rows; ++r) {
            for (int c = 0; c < table->cols; ++c) set(r, c, table->value(r, c));
        }
    };
    auto perSpell = [&](int no, std::size_t cols, auto member) {
        grid(no, [&](int r, int c, int v) {
            if (r < kSpellCount && static_cast<std::size_t>(c) < cols) (t.spells[static_cast<std::size_t>(r)].*member)[static_cast<std::size_t>(c)] = v;
        });
    };
    perSpell(16, kSpellColumns, &SpellInfo::mpCost);
    perSpell(17, kSpellColumns, &SpellInfo::power);
    perSpell(18, kSpellColumns, &SpellInfo::hit);
    perSpell(21, kSpellColumns, &SpellInfo::flag21);
    perSpell(35, kSpellColumns, &SpellInfo::speed);
    perSpell(27, kMaxSpellLevel, &SpellInfo::usesToLevel);
    grid(19, [&](int r, int c, int v) {
        if (r < kSpellCount && c == 0) t.spells[static_cast<std::size_t>(r)].element = v;
    });
    grid(20, [&](int r, int c, int v) {
        if (r < kSpellCount && c == 0) t.spells[static_cast<std::size_t>(r)].castSpeed = v;
    });
    for (int cls = 0; cls < 4; ++cls) {
        grid(104 + cls, [&](int r, int c, int v) {
            if (r < kSpellCount && c == 0) t.spells[static_cast<std::size_t>(r)].learnAt[static_cast<std::size_t>(cls)] = v;
        });
        grid(100 + cls, [&](int r, int c, int v) {
            if (r < 4 && c == 0) t.classWeights[static_cast<std::size_t>(cls)][static_cast<std::size_t>(r)] = v;
        });
    }
    grid(23, [&](int r, int c, int v) {
        if (r < static_cast<int>(t.minePower.size()) && c == 0) t.minePower[static_cast<std::size_t>(r)] = v;
    });
    grid(7, [&](int r, int c, int v) {
        if (r < 21 && c == 0) t.elementAttack[static_cast<std::size_t>(r)] = v;
    });
    grid(11, [&](int r, int c, int v) {
        if (r < 21 && c == 0) t.elementDefense[static_cast<std::size_t>(r)] = v;
    });
    grid(4, [&](int r, int c, int v) {
        if (r < 8 && c == 0) t.chantSteps[static_cast<std::size_t>(r)] = v;
    });
    for (int no = 204; no <= 207; ++no) {
        grid(no, [&](int r, int c, int v) {
            if (r == 0 && c < kSpellColumns) t.counts[static_cast<std::size_t>(no - 204)][static_cast<std::size_t>(c)] = v;
        });
    }
    grid(201, [&](int r, int c, int v) {
        if (r < 13 && c < kSpellColumns) t.berserker[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = v;
    });
    grid(200, [&](int r, int c, int v) {
        if (r < 14 && c < kSpellColumns) t.moon[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = v;
    });
    grid(24, [&](int r, int c, int v) {
        if (r < 10 && c < 2) t.stunBonus[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = v;
    });
    grid(26, [&](int r, int c, int v) {
        if (r < 10 && c < 2) t.heroStunBonus[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = v;
    });
    // Three columns a level: chance, length, may run past 15 ticks.
    for (int e = 0; e <= kElementCount; ++e) {
        grid(70 + e, [&](int r, int c, int v) {
            if (r >= kSpellCount || c >= 3 * kSpellColumns) return;
            Stun& s = t.stun(e, r, c / 3 + 1);
            if (c % 3 == 0) s.chance = v;
            else if (c % 3 == 1) s.length = v;
            else s.uncapped = v != 0;
        });
    }
    // Tables 600-621: the name (row 0, "Fire Ball - Fire Attribute") and the
    // description (rows 2 on).
    for (int s = 0; s < kSpellCount; ++s) {
        const auto* table = tables.find(600 + s);
        if (!table || table->strings.empty() || table->cols < 1) continue;
        auto& spell = t.spells[static_cast<std::size_t>(s)];
        const std::string& title = table->string(0, 0);
        if (!title.empty()) spell.name = title.substr(0, title.find(" - "));
        spell.description.clear();
        for (int r = 2; r < table->rows; ++r) spell.description.push_back(table->string(r, 0));
    }
    return t;
}

MagicTables::Stun spellStun(const MagicTables& t, int spell, int level, int monsterElement, const ElementLevels& hero) {
    MagicTables::Stun s = t.stun(monsterElement, spell, level);
    const int elementLevel = monsterElement >= 0 ? levelIn(hero, oppositeElement(monsterElement)) : 0;
    if (elementLevel >= 1 && elementLevel <= 10) {
        s.chance += t.stunBonus[static_cast<std::size_t>(elementLevel - 1)][0];
        s.length += t.stunBonus[static_cast<std::size_t>(elementLevel - 1)][1];
    }
    return s;
}

MagicTables::Stun heroSpellStun(const MagicTables& t, int spell, int level, const ElementLevels& hero) {
    const int element = t.spell(spell).element;
    const int elementLevel = std::clamp(levelIn(hero, element), -10, 10);
    const int column = elementLevel > 0 ? element : elementLevel < 0 ? oppositeElement(element) : kElementCount;
    MagicTables::Stun s = t.stun(column, spell, level);
    if (elementLevel < 0) {
        s.chance += t.heroStunBonus[static_cast<std::size_t>(elementLevel + 10)][0];
        s.length += t.heroStunBonus[static_cast<std::size_t>(elementLevel + 10)][1];
    }
    return s;
}

std::optional<MonsterSpell> monsterSpell(int spell, int roll4) {
    // The hit controllers' effects (0x42b860 by way of 0x42f2f0): 20000
    // Fire Ball's burst, 20001 Hell Fire's, 20005 the shock, 21000-21003
    // sparks, 21013 frost, 21019 stone.
    constexpr int kFireBurst = 10000011, kHellFireBurst = 10000001, kShock = 11000003, kFrost = 11000026, kStone = 11000050;
    constexpr int kSparks[] = {11000000, 11000001, 11000002, 11000009};
    switch (spell) {
        case kFireBall:
        case kMudJavelin:
        case kFlameStrike: return MonsterSpell{0, kFireBurst, false};
        case kIceBolt: return MonsterSpell{0, kFrost, false};
        case kPlasma:
        case kLightningStorm: return MonsterSpell{0, kShock, false};
        case kHellFire: return MonsterSpell{10, kHellFireBurst, false};
        case kIceBlast: return MonsterSpell{10, kFrost, false};
        case kEarthSpears: return MonsterSpell{0, kSparks[std::clamp(roll4, 0, 3)], false};
        case kDreadDeathscythe: return MonsterSpell{10, kFrost, true};
        case kMedusa: return MonsterSpell{0, kStone, false};
        case kElementalStrike: return MonsterSpell{0, kSparks[0], false};
        default: return std::nullopt;
    }
}

StunKind stunKindOf(int spell) {
    switch (spell) {
        case kIceBolt:
        case kIceBlast:
        case kDreadDeathscythe: return StunKind::Freeze;
        case kMedusa: return StunKind::Stone;
        default: return StunKind::Plain;
    }
}

bool isToggleSpell(int spell) {
    return spell == kMoon || spell == kBerserker || spell == kEnergyShield || spell == kMagicShield || spell == kCounterBurst;
}

bool trainsByKills(int spell) { return spell == kMoon || spell == kBerserker || spell == kEnergyShield; }

void applyElementLevels(HeroStats& stats, int& chantSpeed, const ElementLevels& levels) {
    auto& v = stats.value;
    constexpr HeroStats::Stat kRaises[kElementCount] = {
        HeroStats::kAttack, HeroStats::kDefense, HeroStats::kHit, HeroStats::kEvasion,
        HeroStats::kMagicAttack, HeroStats::kMagicDefense, HeroStats::kMagicHit, HeroStats::kMagicEvasion,
    };
    for (int e = 0; e < kElementCount; ++e) v[kRaises[e]] = addPercent(v[kRaises[e]], levels[static_cast<std::size_t>(e)]);
    auto both = [&](int e) { return levels[static_cast<std::size_t>(e)] == 10 && levels[static_cast<std::size_t>(e + 1)] == 10; };
    if (both(kFire)) v[HeroStats::kHp] = addPercent(v[HeroStats::kHp], 10);
    if (both(kEarth)) v[HeroStats::kAttackSpeed] = addPercent(v[HeroStats::kAttackSpeed], 10);
    if (both(kHoly)) v[HeroStats::kMp] = addPercent(v[HeroStats::kMp], 10);
    if (both(kGel)) chantSpeed = addPercent(chantSpeed, 10);
    clampStats(stats, chantSpeed);
}

void applyBerserker(HeroStats& stats, int& chantSpeed, const MagicTables& tables, int level) {
    const std::size_t col = MagicTables::column(level);
    for (int row = 1; row <= 12; ++row) {
        const auto s = berserkerStat(row);
        stats.value[s] = addPercent(stats.value[s], tables.berserker[static_cast<std::size_t>(row)][col]);
    }
    clampStats(stats, chantSpeed);
}

void clampStats(HeroStats& stats, int& chantSpeed) {
    for (int s = 0; s < HeroStats::kCount; ++s) {
        int& v = stats.value[s];
        switch (s) {
            case HeroStats::kAttackSpeed:
            case HeroStats::kWalkSpeed: v = std::clamp(v, 0, 255); break;
            case HeroStats::kStrength: v = std::max(v, 0); break;
            default: v = std::max(v, 1); break;
        }
    }
    chantSpeed = std::clamp(chantSpeed, 0, 255);
}

int heroBlowDamage(const MagicTables& t, int attack, const ElementLevels& levels, int monsterElement, int defense, int roll) {
    const int factor = monsterElement >= 0 ? elementFactor(t.elementAttack, levelIn(levels, oppositeElement(monsterElement))) : 10;
    return std::max(1, attack * (9 + std::clamp(roll, 0, 2)) * factor / 100 - defense);
}

int damageToHero(const MagicTables& t, int power, int element, const ElementLevels& levels, int defense, int roll) {
    const int factor = element >= 0 ? elementFactor(t.elementDefense, levelIn(levels, element)) : 10;
    return std::max(1, (9 + std::clamp(roll, 0, 2)) * power / 10 - defense * factor / 10);
}

int spellDamage(int power, int spellElement, int monsterElement, int magicDefense, int roll, int roll4) {
    int multiplier = 10;
    if (monsterElement >= 0 && spellElement >= 0) {
        if (spellElement == oppositeElement(monsterElement)) {
            multiplier = 10 + std::clamp(roll4, 0, 3);
        } else if (spellElement == monsterElement) {
            multiplier = 7 + std::clamp(roll4, 0, 3);
        }
    }
    return std::max(1, (9 + std::clamp(roll, 0, 2)) * power * multiplier / 100 - magicDefense);
}

int tableClassIndex(HeroClass cls) {
    switch (cls) {
        case HeroClass::Mercenary: return 0;
        case HeroClass::Warrior: return 1;
        case HeroClass::Wizard: return 2;
        case HeroClass::Hunter: return 3;
    }
    return 0;
}

std::array<int, 4> classPoints(const MagicTables& t, const std::vector<HeroClass>& history) {
    std::array<int, 4> levels{};
    levels[0] = 1;  // the new hero's first level
    for (const HeroClass cls : history) ++levels[static_cast<std::size_t>(tableClassIndex(cls))];
    std::array<int, 4> points{};
    for (std::size_t c = 0; c < 4; ++c) {
        for (std::size_t k = 0; k < 4; ++k) points[c] += t.classWeights[c][k] * levels[k];
    }
    return points;
}

std::vector<int> SpellBook::learn(const MagicTables& t, const std::vector<HeroClass>& history) {
    std::vector<int> learned;
    if (history.empty()) return learned;
    // The class just finished: Career::levelUp has recorded it but not yet
    // made the hero a warrior at level 5.
    const auto cls = static_cast<std::size_t>(tableClassIndex(history.back()));
    const int points = classPoints(t, history)[cls];
    for (int s = 0; s < kSpellCount; ++s) {
        const auto i = static_cast<std::size_t>(s);
        if (level[i] > 0 || t.spells[i].learnAt[cls] > points) continue;
        level[i] = 1;
        uses[i] = 0;
        learned.push_back(s);
        if (const auto free = std::ranges::find(bar, -1); free != bar.end()) *free = s;
    }
    return learned;
}

SpellBook SpellBook::replay(const MagicTables& t, const std::vector<HeroClass>& history) {
    SpellBook book;
    std::vector<HeroClass> sofar;
    for (const HeroClass cls : history) {
        sofar.push_back(cls);
        book.learn(t, sofar);
    }
    return book;
}

bool SpellBook::train(const MagicTables& t, int spell, int times) {
    if (!knows(spell)) return false;
    const auto i = static_cast<std::size_t>(spell);
    if (level[i] >= kMaxSpellLevel) return false;
    uses[i] += std::max(1, times);
    bool raised = false;
    while (level[i] < kMaxSpellLevel) {
        const int need = t.spells[i].usesToLevel[static_cast<std::size_t>(level[i] - 1)];
        if (uses[i] < need) break;
        uses[i] -= need;
        ++level[i];
        raised = true;
        // One use raises one level at most, as in the original.
        if (times <= 1) break;
    }
    return raised;
}

int SpellBook::castLevel(int spell, bool powerMode, int equipmentBonus) const {
    if (!knows(spell)) return 0;
    const int own = std::clamp(level[static_cast<std::size_t>(spell)] + (powerMode ? 2 : 0), 1, kMaxSpellLevel);
    return std::clamp(own + equipmentBonus, 1, kSpellColumns);
}

int spellMpCost(const MagicTables& t, int spell, int level, int equipmentSaving) {
    return std::max(1, t.mpCost(spell, level) - equipmentSaving);
}

double castSpeedFactor(const MagicTables& t, int spell, int chantSpeed, bool powerMode, bool overloaded) {
    const auto step = static_cast<std::size_t>(std::clamp(chantSpeed, 0, 255) / 32);
    int index = std::clamp(t.chantSteps[std::min(step, t.chantSteps.size() - 1)] + 1, 0, 9);
    // The step comes back 0 when overloaded (0x450cd0); power mode then
    // takes the top one anyway (0x439825).
    if (overloaded) index = 0;
    if (powerMode) index = 9;
    return 0.001 * t.spell(spell).castSpeed * kCastSteps[index];
}

bool PowerMode::start(bool spiritStone) {
    if (!ready(spiritStone)) return false;
    charge = 0;
    left = kLength;
    return true;
}

bool blockedInPowerMode(int spell) { return spell == kMoon || spell == kBerserker || spell == kEnergyShield; }

int heroMoveSpeed(int walkSpeed, bool running, bool powerMode, bool overloaded) {
    // The scale the walk and run code builds on the stack (0x4351f0).
    static constexpr double kSteps[10] = {2.0 / 3, 0.75, 0.8, 5.0 / 6, 6.0 / 7, 1.0, 7.0 / 6, 1.2, 1.25, 4.0 / 3};
    if (powerMode) return static_cast<int>(kSteps[9] * (running ? 40 : 20));
    const int step = overloaded ? 0 : std::clamp((walkSpeed + 32) / 32, 0, 9);
    const int walk = static_cast<int>(kSteps[step] * 20);
    return running ? walk * 2 : walk;
}

double attackSpeedFactor(const MagicTables& t, int attackSpeed, bool powerMode, bool overloaded) {
    // The hero's swing's scale (0x437fe0).
    static constexpr double kSteps[10] = {0.3, 0.4, 0.5, 0.6, 0.8, 1.0, 1.2, 1.4, 1.7, 2.0};
    const auto column = static_cast<std::size_t>(std::clamp(attackSpeed, 0, 255) / 32);
    int step = std::clamp(t.chantSteps[std::min(column, t.chantSteps.size() - 1)] + 1, 0, 9);
    if (overloaded) step = 0;
    if (powerMode) step = 9;
    return kSteps[step];
}

double volleySpeedFactor(const MagicTables& t, int attackSpeed, bool powerMode) {
    static constexpr double kSteps[10] = {0.6, 0.9, 1.2, 1.3, 1.5, 1.7, 1.9, 2.2, 2.7, 3.0};
    const auto column = static_cast<std::size_t>(std::clamp(attackSpeed, 0, 255) / 32);
    int step = std::clamp(t.chantSteps[std::min(column, t.chantSteps.size() - 1)] + 1, 0, 9);
    if (powerMode) step = 9;
    return kSteps[step];
}

int regenerate(int maximum, int rate, int& carry) {
    if (rate == 0) {
        carry = 0;
        return 0;
    }
    // In hundredths of a point, as 0x44f2f0 counts.
    int amount = maximum * rate / 100;
    carry += amount % 100;
    if (carry / 100 != 0) {
        if (carry < 0) {
            carry += 100;
            amount -= 100;
        }
        if (carry > 0) {
            carry -= 100;
            amount += 100;
        }
    }
    return amount / 100;
}

}  // namespace sf

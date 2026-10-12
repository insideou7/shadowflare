#include "logic/mines.h"

#include <algorithm>

namespace sf::mines {

int most(int bonus) { return std::max(0, kBaseMost + bonus); }

int power(const MagicTables& tables, int heroLevel, int bonus) {
    const auto row = static_cast<std::size_t>(std::clamp(heroLevel, 1, static_cast<int>(tables.minePower.size())) - 1);
    return std::max(1, tables.minePower[row] + bonus);
}

int damage(const MagicTables& tables, int power, int monsterElement, int defense, int roll) {
    return heroBlowDamage(tables, power, ElementLevels{}, monsterElement, defense, roll);
}

}  // namespace sf::mines

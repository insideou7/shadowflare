#pragma once

#include <vector>

#include "formats/drop_tables.h"
#include "formats/item_data.h"

namespace sf {

class Random;

// Rolls a drop list and returns the items it gives, in the order they
// appear. With `qualityPerRoll` off, the original's slip is kept: the
// quality allowances are never cleared between rolls, so once one roll
// may give a magic or unique item every later roll may too. `luck` (the
// Debug tab's, in percent) multiplies the drop weight of unique items
// where a pick chooses by weight; 100 is the original's chances. Above
// 100, uniques up to item level `luckLevel` (the hero's) may come even
// where the pick's highest item level is lower.
std::vector<const ItemDef*> rollDrops(const DropTables& tables, const ItemTable& items, int list, Random& random,
                                      bool qualityPerRoll, int luck = 100, int luckLevel = 0);

// Where the i-th of `count` dropped items lands around (x, y): on a circle
// of 200 world units, spread evenly, as 0x417550 places them.
void dropSpot(int i, int count, double x, double y, double& outX, double& outY);

// Gold, as ShadowFlare.exe drops it (0x45a030): `chance` percent of the
// time, an amount from min to max, raised by `bonus` percent (the Amount
// of Gold modifiers worn), at least 1. Returns 0 for none.
int rollGold(int chance, int min, int max, int bonus, Random& random);

// Ours, the Debug tab's gold rate: `amount` times `percent` / 100, at most
// kMaxGoldDrop so a pile of piles stays in reach.
inline constexpr int kMaxGoldDrop = 10'000'000;
int scaleGold(int amount, int percent);

// Gold split into piles of at most `stack` (the bag's 10,000), the last one
// holding what is left over.
std::vector<int> goldPiles(int amount, int stack);

}  // namespace sf

#pragma once

#include <vector>

#include "formats/drop_tables.h"
#include "formats/item_data.h"

namespace sf {

class Random;

// Rolls a drop list and returns the items it gives, in the order they
// appear. With `qualityPerRoll` off, the original's slip is kept: the
// quality allowances are never cleared between rolls, so once one roll
// may give a magic or unique item every later roll may too.
std::vector<const ItemDef*> rollDrops(const DropTables& tables, const ItemTable& items, int list, Random& random,
                                      bool qualityPerRoll);

// Where the i-th of `count` dropped items lands around (x, y): on a circle
// of 200 world units, spread evenly, as 0x417550 places them.
void dropSpot(int i, int count, double x, double y, double& outX, double& outY);

// Gold, as ShadowFlare.exe drops it (0x45a030): `chance` percent of the
// time, an amount from min to max, raised by `bonus` percent (the Amount
// of Gold modifiers worn), at least 1. Returns 0 for none.
int rollGold(int chance, int min, int max, int bonus, Random& random);

}  // namespace sf

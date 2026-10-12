#pragma once

#include <optional>
#include <vector>

#include "logic/inventory.h"

namespace sf {

struct GameTables;
class Random;

// A shop's goods, laid out on its own 9 x 10 grid of 32-pixel cells.
struct ShopStock {
    static constexpr int kWidth = 9, kHeight = 10;
    std::vector<Inventory::Placed> goods;

    bool fits(int w, int h, int x, int y) const;
    // Puts an item in the first free place, scanning each row from the left,
    // top row first, like the original (0x466f80). False when it does not fit.
    bool add(const Item& item);
    int indexAt(int x, int y) const;  // -1 when the cell is empty
    // Storage: with an empty hand, picks up the item at (x, y); with an item
    // in hand, puts it down with its top-left at (x, y), picking up the one
    // item it covers, as the bag does.
    Inventory::Result click(std::optional<Item>& hand, int x, int y);
};

// INITTRADE's stock, as ShadowFlare.exe builds it (0x430c10): row `list` of
// Table.Tbd table 32 names entries of table 33. Each entry is a class, a
// highest and lowest item level, an item code, a count (-1 = 0 to 29), and
// which qualities may appear (columns 7-9: normal, magic, unique). With a
// code the entry adds that item `count` times; without, it adds `count`
// items picked by drop weight among those that match, from any of weapons,
// armour and accessories when the class is -1. Items that do not fit are
// left out.
ShopStock stockShop(const GameTables& tables, const ItemTable& items, int list, Random& random);

// What a shop asks for an item, and what it pays for one (a quarter, at
// least 1; ShadowFlare.exe 0x4661c0).
int buyPrice(const ItemDef& def);
int sellPrice(const ItemDef& def);

}  // namespace sf

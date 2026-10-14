#include "logic/shop.h"

#include <algorithm>

#include "formats/table_data.h"
#include "logic/ai_select.h"

namespace sf {

using Class = ItemDef::Class;

bool ShopStock::fits(int w, int h, int x, int y) const {
    if (x < 0 || y < 0 || x + w > kWidth || y + h > kHeight) return false;
    for (const auto& g : goods) {
        if (x < g.x + g.item.def->width && g.x < x + w && y < g.y + g.item.def->height && g.y < y + h) return false;
    }
    return true;
}

bool ShopStock::add(const Item& item) {
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (fits(item.def->width, item.def->height, x, y)) {
                goods.push_back({item, x, y});
                return true;
            }
        }
    }
    return false;
}

Inventory::Result ShopStock::click(std::optional<Item>& hand, int x, int y) {
    if (!hand) {
        const int index = indexAt(x, y);
        if (index < 0) return Inventory::Result::Nothing;
        hand = goods[static_cast<std::size_t>(index)].item;
        goods.erase(goods.begin() + index);
        return Inventory::Result::Done;
    }
    const int w = hand->def->width, h = hand->def->height;
    if (x < 0 || y < 0 || x + w > kWidth || y + h > kHeight) return Inventory::Result::NoRoom;
    int covered = -1;
    for (std::size_t i = 0; i < goods.size(); ++i) {
        const auto& g = goods[i];
        if (x < g.x + g.item.def->width && g.x < x + w && y < g.y + g.item.def->height && g.y < y + h) {
            if (covered >= 0) return Inventory::Result::NoRoom;
            covered = static_cast<int>(i);
        }
    }
    std::optional<Item> picked;
    if (covered >= 0) {
        picked = goods[static_cast<std::size_t>(covered)].item;
        goods.erase(goods.begin() + covered);
    }
    goods.push_back({*hand, x, y});
    hand = picked;
    return Inventory::Result::Done;
}

bool ShopStock::takeIn(std::optional<Item>& hand, bool emptyGoldStays) {
    if (!hand) return false;
    if (hand->def->isGold()) {
        // The fullest stack short of 10000; of equals, the one put there
        // last, as the original's list keeps the newest first (0x466cd0).
        for (;;) {
            Inventory::Placed* fullest = nullptr;
            for (auto it = goods.rbegin(); it != goods.rend(); ++it) {
                if (it->item.def->isGold() && it->item.count < Inventory::kGoldStack &&
                    (!fullest || it->item.count > fullest->item.count)) {
                    fullest = &*it;
                }
            }
            if (!fullest) break;
            const int room = Inventory::kGoldStack - fullest->item.count;
            // All of it fits only with room to spare (the exe compares
            // with <): an exact fill goes the long way round.
            if (hand->count < room) {
                fullest->item.count += hand->count;
                hand.reset();
                return false;
            }
            hand->count -= room;
            fullest->item.count = Inventory::kGoldStack;
        }
        if (hand->count == 0 && !emptyGoldStays) {
            hand.reset();
            return false;
        }
    }
    if (!add(*hand)) return false;
    hand.reset();
    return true;
}

int ShopStock::indexAt(int x, int y) const {
    for (std::size_t i = 0; i < goods.size(); ++i) {
        const auto& g = goods[i];
        if (x >= g.x && x < g.x + g.item.def->width && y >= g.y && y < g.y + g.item.def->height) return static_cast<int>(i);
    }
    return -1;
}

namespace {

struct Filter {
    int maxLevel = -1, minLevel = -1;  // -1 = no bound
    bool quality[4] = {true, true, true, true};
};

bool matches(const ItemDef& def, const Filter& f) {
    if (def.quality < 0 || def.quality > 3 || !f.quality[def.quality] || def.poolMask == 0) return false;
    if (f.maxLevel != -1 && def.itemLevel > f.maxLevel) return false;
    if (f.minLevel != -1 && def.itemLevel < f.minLevel) return false;
    return true;
}

int weightOf(const ItemTable& items, Class cls, const Filter& f) {
    int total = 0;
    for (const auto& def : items.items) {
        if (def.cls == cls && matches(def, f)) total += def.dropWeight;
    }
    return total;
}

// The original's weighted pick (0x4653a0): `pick` counts down through the
// matching items' drop weights.
const ItemDef* pickWeighted(const ItemTable& items, Class cls, const Filter& f, int pick) {
    for (const auto& def : items.items) {
        if (def.cls != cls || !matches(def, f)) continue;
        pick -= def.dropWeight;
        if (pick < 0) return &def;
    }
    return nullptr;
}

}  // namespace

ShopStock stockShop(const GameTables& tables, const ItemTable& items, int list, Random& random) {
    ShopStock stock;
    const auto* lists = tables.find(32);
    const auto* entries = tables.find(33);
    if (!lists || !entries || list < 0 || list >= lists->rows || entries->cols < 10) return stock;
    for (int col = 0; col < lists->cols; ++col) {
        const int row = lists->value(list, col);
        if (row < 0 || row >= entries->rows) continue;
        const int cls = entries->value(row, 0);
        Filter f;
        f.maxLevel = entries->value(row, 1);
        f.minLevel = entries->value(row, 2);
        const int code = entries->value(row, 3);
        int count = entries->value(row, 4);
        for (int q = 0; q < 3; ++q) f.quality[q] = entries->value(row, 7 + q) == 1;
        if (count == -1) count = random.next(30);
        for (int n = 0; n < count; ++n) {
            const ItemDef* def = nullptr;
            if (code != -1) {
                def = items.find(static_cast<Class>(cls), code);
            } else if (cls == -1) {
                // Any of weapons, armour and accessories, weighted together.
                int weights[3], total = 0;
                for (int k = 0; k < 3; ++k) total += weights[k] = weightOf(items, static_cast<Class>(k), f);
                if (total == 0) continue;
                int pick = random.next(total);
                for (int k = 0; k < 3 && !def; ++k) {
                    if (pick < weights[k]) {
                        def = pickWeighted(items, static_cast<Class>(k), f, pick);
                    } else {
                        pick -= weights[k];
                    }
                }
            } else {
                const int total = weightOf(items, static_cast<Class>(cls), f);
                if (total > 0) def = pickWeighted(items, static_cast<Class>(cls), f, random.next(total));
            }
            if (!def) continue;
            // Shop goods come identified, modifiers rolled.
            Item item = Item::make(def, random);
            item.unidentified = false;
            stock.add(item);
        }
    }
    return stock;
}

int buyPrice(const Item& item, const ModifierPrices& prices) { return std::max(1, item.value(prices)); }

int sellPrice(const Item& item, const ModifierPrices& prices) { return item.sellValue(prices); }

}  // namespace sf

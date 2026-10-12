#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "logic/inventory.h"
#include "logic/progression.h"
#include "logic/script_runner.h"

namespace sf {

// A saved game: where the player is, how far they have come and what they
// carry. Written as a small text file of key=value lines, so it can be
// read and fixed by hand; items are kept by their (class, code), so a save
// works with whichever item list the game data has, original or open.
struct SaveGame {
    struct ItemRef {
        int cls = 0;
        std::int32_t code = 0;
        int count = 1;
    };
    struct BagEntry {
        ItemRef item;
        int x = 0, y = 0;
    };

    std::string map;       // map name
    std::string scenario;  // Scenario/<dir> of the map, or empty
    int x = 0, y = 0, direction = 0;
    int hp = 0;
    int level = 1;
    std::int64_t experience = 0;
    std::optional<Career> career;
    int gold = 0;
    std::vector<BagEntry> bag;
    std::array<std::optional<ItemRef>, kEquipSlotCount> equipped;
    ScriptFlags flags;  // the persistent scenario flags

    std::string write() const;
    // Throws FormatError on a malformed file.
    static SaveGame parse(std::string_view text);

    // Fills an inventory from the saved items, skipping any the item list
    // does not have. Returns how many were skipped.
    int restore(Inventory& inventory, const ItemTable& items) const;
    void store(const Inventory& inventory);
};

}  // namespace sf

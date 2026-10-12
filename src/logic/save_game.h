#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "logic/inventory.h"
#include "logic/magic.h"
#include "logic/partner.h"
#include "logic/progression.h"
#include "logic/shop.h"
#include "logic/script_runner.h"

namespace sf {

// A saved game: where the player is, how far they have come and what they
// carry. Written as a small text file of key=value lines, so it can be
// read and fixed by hand; items are kept by their (class, code), so a save
// works with whichever item list the game data has, original or open.
struct SaveGame {
    static constexpr std::size_t kSavedModifiers = ItemDef::kModifierCount + 8;
    struct ItemRef {
        int cls = 0;
        std::int32_t code = 0;
        int count = 1;
        int wear = 0;               // Item::wear
        bool unidentified = false;  // Item::unidentified
        // Item::modifiers then Item::elementModifiers, as one list.
        std::array<std::int32_t, kSavedModifiers> modifiers{};
    };
    struct BagEntry {
        ItemRef item;
        int x = 0, y = 0;
    };

    std::string map;       // map name
    std::string scenario;  // Scenario/<dir> of the map, or empty
    int x = 0, y = 0, direction = 0;
    int hp = 0;
    std::optional<int> mp;  // none in saves from before magic: start full
    int level = 1;
    std::int64_t experience = 0;
    std::optional<Career> career;
    int gold = 0;
    std::vector<BagEntry> bag;
    std::vector<BagEntry> belt;     // the belt pocket, cells of its 4 x 2 grid
    std::vector<BagEntry> storage;  // the warehouse (STORAGE), cells of its 9 x 10 grid
    std::array<std::optional<ItemRef>, kEquipSlotCount> equipped;
    ScriptFlags flags;  // the persistent scenario flags
    std::optional<SpellBook> spells;  // none in saves from before magic
    std::array<bool, kSpellCount> spellsOn{};  // toggle spells that are on
    bool originalMode = false;
    std::optional<PartnerRecord> partner;  // none in saves from before companions
    int partnerHp = 0;                     // 0 = full (or down, with reviveTicks)

    std::string write() const;
    // Throws FormatError on a malformed file.
    static SaveGame parse(std::string_view text);

    // Fills an inventory from the saved items, skipping any the item list
    // does not have. Returns how many were skipped.
    int restore(Inventory& inventory, const ItemTable& items) const;
    void store(const Inventory& inventory);
    int restoreStorage(ShopStock& stock, const ItemTable& items) const;
    void storeStorage(const ShopStock& stock);
};

}  // namespace sf

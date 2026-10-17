#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace sf {

// One thing the player does with their items: a click in the bag, on a
// worn slot or on the belt, using, wearing or taking off an item, dropping
// what is in the hand, the weapon set tabs, splitting gold, the Warehouse
// Transfer Box and the mine slot, buying and selling, the warehouse
// window and choosing the item for Identify. The item and shop windows
// turn clicks into these and Game::applyItemRequest carries them out
// against the hero's own inventory, checking each one, so the same request
// can come from a player's game to the server online (docs/online.md).
// Items are named by the cell or slot they are in, never by what they are,
// so a request can only move what the hero has.
struct ItemRequest {
    enum class Kind : std::uint8_t {
        BagClick,         // x, y: take up the item on a cell, or put the hand down with its top-left there
        SlotClick,        // slot: take off, wear or swap with the hand
        BeltClick,        // x, y: take from or put on a belt cell
        UseBag,           // x, y: use the consumable on a bag cell
        UseBelt,          // x, y: use the consumable on a belt cell
        EquipFromBag,     // x, y: wear the item on a bag cell (right click, ours)
        UnequipToBag,     // slot: take off into the bag (right click, ours)
        DropHand,         // let what is in the hand fall at the hero's feet
        ChooseWeaponSet,  // set: the I or II tab
        ToggleShared,     // part, set: right click on a weapon set tab
        SplitGold,        // x, y, amount: take some gold off a bag stack into the hand
        ToWarehouse,      // the hand into the Warehouse Transfer Box
        MineSlot,         // take a mine into the hand, or put one back
        // The open shop or warehouse (the cells are its 9 x 10 grid's).
        Buy,               // x, y: buy the item on a shop cell
        SellHand,          // sell what is in the hand to the open shop
        SellFromBag,       // x, y: sell the item on a bag cell (right click, ours)
        StorageClick,      // x, y: take up or put down in the warehouse, as BagClick
        StoreFromBag,      // x, y: the item on a bag cell into the warehouse (right click, ours)
        FromStorage,       // x, y: right click on a warehouse item: into the bag (ours) or use it
        SplitStorageGold,  // x, y, amount: some gold off a warehouse stack into the hand
        GiantPage,         // x: show another page of the Giant Warehouse
        CloseShop,         // leave the shop or warehouse
        // The Identify spell, waiting for a bag item to be chosen.
        Identify,          // x, y: identify the item on a bag cell
        CancelIdentify,    // call it off
    };
    static constexpr int kKindCount = static_cast<int>(Kind::CancelIdentify) + 1;

    Kind kind = Kind::DropHand;
    // Cells (x, y), or slot (EquipSlot) in x, or part in x and set in y.
    std::int16_t x = 0, y = 0;
    std::int32_t amount = 0;  // SplitGold, SplitStorageGold

    static ItemRequest bagClick(int x, int y) { return {Kind::BagClick, static_cast<std::int16_t>(x), static_cast<std::int16_t>(y)}; }
    static ItemRequest at(Kind kind, int x, int y) { return {kind, static_cast<std::int16_t>(x), static_cast<std::int16_t>(y)}; }
    static ItemRequest of(Kind kind, int x = 0) { return {kind, static_cast<std::int16_t>(x)}; }

    bool operator==(const ItemRequest&) const = default;

    // Whether the numbers make sense for the kind: cells inside the bag or
    // belt (a hand put down in the bag may hang over its edge, which the
    // bag itself refuses), a real slot, set or part, a positive amount,
    // and nothing in the fields the kind does not use. Whether the item is
    // there and the move is allowed is for the inventory to say.
    bool valid() const;

    // A fixed-size message: a version byte, then the fields little-endian.
    static constexpr std::uint8_t kVersion = 1;
    static constexpr std::size_t kEncodedSize = 1 + 1 + 2 + 2 + 4;
    std::array<std::uint8_t, kEncodedSize> encode() const;
    // Null for another size or version, or a request that is not valid().
    static std::optional<ItemRequest> decode(std::span<const std::uint8_t> bytes);
};

}  // namespace sf

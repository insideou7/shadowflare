#include "logic/item_request.h"

#include "logic/inventory.h"
#include "logic/shop.h"

namespace sf {
namespace {

// More than any item is wide or tall, in cells.
constexpr int kOverhang = 8;

bool inBag(int x, int y) { return x >= 0 && x < Inventory::kBagWidth && y >= 0 && y < Inventory::kBagHeight; }
bool inStock(int x, int y) { return x >= 0 && x < ShopStock::kWidth && y >= 0 && y < ShopStock::kHeight; }
bool inBelt(int x, int y) { return x >= 0 && x < Inventory::kBeltWidth && y >= 0 && y < Inventory::kBeltHeight; }

}  // namespace

bool ItemRequest::valid() const {
    using K = Kind;
    if (static_cast<int>(kind) >= kKindCount) return false;
    if (kind != K::SplitGold && kind != K::SplitStorageGold && amount != 0) return false;
    switch (kind) {
        case K::BagClick:
            // An item held by its middle can hang over the bag's edge by
            // less than its size, and the bag says NoRoom.
            return x > -kOverhang && x < Inventory::kBagWidth + kOverhang && y > -kOverhang && y < Inventory::kBagHeight + kOverhang;
        case K::StorageClick:
            return x > -kOverhang && x < ShopStock::kWidth + kOverhang && y > -kOverhang && y < ShopStock::kHeight + kOverhang;
        case K::UseBag:
        case K::EquipFromBag:
        case K::SellFromBag:
        case K::StoreFromBag:
        case K::Identify: return inBag(x, y);
        case K::Buy:
        case K::FromStorage: return inStock(x, y);
        case K::SplitStorageGold: return inStock(x, y) && amount > 0;
        case K::GiantPage: return x >= 0 && x < kGiantStoragePages && y == 0;
        case K::SplitGold: return inBag(x, y) && amount > 0;
        case K::BeltClick:
        case K::UseBelt: return inBelt(x, y);
        case K::SlotClick:
        case K::UnequipToBag: return x >= 0 && x < kEquipSlotCount && y == 0;
        case K::ChooseWeaponSet: return (x == 0 || x == 1) && y == 0;
        case K::ToggleShared: return (x == 0 || x == 1) && (y == 0 || y == 1);
        case K::DropHand:
        case K::ToWarehouse:
        case K::MineSlot:
        case K::SellHand:
        case K::CloseShop:
        case K::CancelIdentify: return x == 0 && y == 0;
    }
    return false;
}

std::array<std::uint8_t, ItemRequest::kEncodedSize> ItemRequest::encode() const {
    const auto ux = static_cast<std::uint16_t>(x), uy = static_cast<std::uint16_t>(y);
    const auto ua = static_cast<std::uint32_t>(amount);
    return {kVersion,
            static_cast<std::uint8_t>(kind),
            static_cast<std::uint8_t>(ux),
            static_cast<std::uint8_t>(ux >> 8),
            static_cast<std::uint8_t>(uy),
            static_cast<std::uint8_t>(uy >> 8),
            static_cast<std::uint8_t>(ua),
            static_cast<std::uint8_t>(ua >> 8),
            static_cast<std::uint8_t>(ua >> 16),
            static_cast<std::uint8_t>(ua >> 24)};
}

std::optional<ItemRequest> ItemRequest::decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != kEncodedSize || bytes[0] != kVersion) return std::nullopt;
    ItemRequest r;
    r.kind = static_cast<Kind>(bytes[1]);
    r.x = static_cast<std::int16_t>(bytes[2] | bytes[3] << 8);
    r.y = static_cast<std::int16_t>(bytes[4] | bytes[5] << 8);
    r.amount = static_cast<std::int32_t>(std::uint32_t(bytes[6]) | std::uint32_t(bytes[7]) << 8 | std::uint32_t(bytes[8]) << 16 |
                                         std::uint32_t(bytes[9]) << 24);
    if (!r.valid()) return std::nullopt;
    return r;
}

}  // namespace sf

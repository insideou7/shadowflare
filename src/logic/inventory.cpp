#include "logic/inventory.h"

#include <algorithm>
#include <utility>

#include "logic/ai_select.h"

namespace sf {

Item Item::make(const ItemDef* def, int count) {
    Item item{def, count};
    item.unidentified = def && (def->quality == 1 || def->quality == 2);
    return item;
}

int Item::durability() const { return std::max(0, def->durability - wear); }

const std::string& Item::shownName() const {
    return unidentified && !def->unidentifiedName.empty() ? def->unidentifiedName : def->name;
}

int Item::repairPrice() const {
    const int max = def->durability;
    const int worn = std::min(wear, max);
    if (max <= 0 || worn <= 0) return 0;
    return std::max(1, worn * (def->price / 10) / max);
}

namespace {

using Class = ItemDef::Class;

bool isAccessorySlot(EquipSlot slot) { return slot >= EquipSlot::Accessory1; }

// The worn item a REPAIR part number means, if any.
std::optional<EquipSlot> partSlot(int part) {
    static constexpr EquipSlot kParts[] = {EquipSlot::Weapon, EquipSlot::Helmet, EquipSlot::Body, EquipSlot::Shield, EquipSlot::Boots};
    if (part < 0 || part > 4) return std::nullopt;
    return kParts[part];
}

}  // namespace

void Inventory::clear() {
    bag_.clear();
    equipped_ = {};
    gold_ = 0;
}

bool Inventory::fits(int w, int h, int x, int y) const {
    if (x < 0 || y < 0 || x + w > kBagWidth || y + h > kBagHeight) return false;
    return std::ranges::none_of(bag_, [&](const Placed& p) {
        return x < p.x + p.item.def->width && p.x < x + w && y < p.y + p.item.def->height && p.y < y + h;
    });
}

int Inventory::indexAt(int x, int y) const {
    for (std::size_t i = 0; i < bag_.size(); ++i) {
        const auto& p = bag_[i];
        if (x >= p.x && x < p.x + p.item.def->width && y >= p.y && y < p.y + p.item.def->height) return static_cast<int>(i);
    }
    return -1;
}

bool Inventory::add(const Item& item) {
    if (!item.def) return false;
    if (item.def->isGold()) {
        gold_ += item.count;
        return true;
    }
    for (int x = 0; x < kBagWidth; ++x) {
        for (int y = 0; y < kBagHeight; ++y) {
            if (addAt(item, x, y)) return true;
        }
    }
    return false;
}

bool Inventory::addAt(const Item& item, int x, int y) {
    if (!item.def || !fits(item.def->width, item.def->height, x, y)) return false;
    bag_.push_back({item, x, y});
    return true;
}

Inventory::Result Inventory::clickBag(std::optional<Item>& hand, int x, int y) {
    if (!hand) {
        const int index = indexAt(x, y);
        if (index < 0) return Result::Nothing;
        hand = bag_[static_cast<std::size_t>(index)].item;
        bag_.erase(bag_.begin() + index);
        return Result::Done;
    }
    const int w = hand->def->width, h = hand->def->height;
    if (x < 0 || y < 0 || x + w > kBagWidth || y + h > kBagHeight) return Result::NoRoom;
    // Whatever lies under the item being put down.
    int covered = -1;
    for (std::size_t i = 0; i < bag_.size(); ++i) {
        const auto& p = bag_[i];
        if (x < p.x + p.item.def->width && p.x < x + w && y < p.y + p.item.def->height && p.y < y + h) {
            if (covered >= 0) return Result::NoRoom;
            covered = static_cast<int>(i);
        }
    }
    std::optional<Item> picked;
    if (covered >= 0) {
        picked = bag_[static_cast<std::size_t>(covered)].item;
        bag_.erase(bag_.begin() + covered);
    }
    bag_.push_back({*hand, x, y});
    hand = picked;
    return Result::Done;
}

bool Inventory::accepts(EquipSlot slot, const ItemDef& def) {
    switch (def.cls) {
        case Class::Weapon: return slot == EquipSlot::Weapon;
        case Class::Armour:
            switch (def.subtype) {
                case ItemDef::kHelmet: return slot == EquipSlot::Helmet;
                case ItemDef::kBody: return slot == EquipSlot::Body;
                case ItemDef::kShield: return slot == EquipSlot::Shield;
                case ItemDef::kBoots: return slot == EquipSlot::Boots;
                default: return false;
            }
        case Class::Accessory: return isAccessorySlot(slot);
        default: return false;
    }
}

std::optional<EquipSlot> Inventory::slotFor(const ItemDef& def) const {
    std::optional<EquipSlot> first;
    for (int s = 0; s < kEquipSlotCount; ++s) {
        const auto slot = static_cast<EquipSlot>(s);
        if (!accepts(slot, def)) continue;
        if (!equipped_[s]) return slot;
        if (!first) first = slot;
    }
    return first;
}

Inventory::Result Inventory::canEquip(EquipSlot slot, const ItemDef& def, int level) const {
    if (!accepts(slot, def)) return Result::WrongSlot;
    // The exe compares the required level with the player's (0x4466f2).
    if (def.requiredLevel > level) return Result::LevelTooLow;
    return Result::Done;
}

std::optional<Item> Inventory::equip(EquipSlot slot, const Item& item) {
    auto previous = equipped_[static_cast<int>(slot)];
    equipped_[static_cast<int>(slot)] = item;
    return previous;
}

Inventory::Result Inventory::clickSlot(std::optional<Item>& hand, EquipSlot slot, int level) {
    auto& worn = equipped_[static_cast<int>(slot)];
    if (!hand) {
        if (!worn) return Result::Nothing;
        hand = worn;
        worn.reset();
        return Result::Done;
    }
    if (const auto check = canEquip(slot, *hand->def, level); check != Result::Done) return check;
    std::swap(worn, hand);
    return Result::Done;
}

const ItemDef* Inventory::use(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= bag_.size()) return nullptr;
    const ItemDef* def = bag_[static_cast<std::size_t>(index)].item.def;
    if (def->cls != Class::Consumable) return nullptr;
    auto& count = bag_[static_cast<std::size_t>(index)].item.count;
    if (--count <= 0) bag_.erase(bag_.begin() + index);
    return def;
}

std::vector<EquipSlot> Inventory::wearFromAttacking(Random& random) {
    auto& weapon = equipped_[static_cast<int>(EquipSlot::Weapon)];
    if (!weapon || random.next(100) >= 30 || weapon->def->durability <= 0 || weapon->broken()) return {};
    if (++weapon->wear < weapon->def->durability) return {};
    return {EquipSlot::Weapon};
}

std::vector<EquipSlot> Inventory::wearFromBlow(Random& random) {
    const auto& weapon = equipped(EquipSlot::Weapon);
    const bool shieldHidden = weapon && weapon->def->twoHanded();
    std::vector<EquipSlot> broke;
    // The exe rolls for each worn piece in this order.
    static constexpr std::pair<EquipSlot, int> kChances[] = {
        {EquipSlot::Helmet, 20}, {EquipSlot::Body, 30}, {EquipSlot::Shield, 30}, {EquipSlot::Boots, 20}};
    for (const auto& [slot, chance] : kChances) {
        auto& worn = equipped_[static_cast<int>(slot)];
        if (!worn || random.next(100) >= chance) continue;
        if (slot == EquipSlot::Shield && shieldHidden) continue;
        if (worn->def->durability <= 0 || worn->broken()) continue;
        if (++worn->wear >= worn->def->durability) broke.push_back(slot);
    }
    return broke;
}

bool Inventory::hasUnidentified() const {
    return std::ranges::any_of(bag_, [](const Placed& p) { return p.item.unidentified; }) ||
           std::ranges::any_of(equipped_, [](const auto& worn) { return worn && worn->unidentified; });
}

bool Inventory::carries(ItemDef::Class cls, std::int32_t code) const {
    return std::ranges::any_of(bag_, [&](const Placed& p) { return p.item.def->cls == cls && p.item.def->code == code; });
}

void Inventory::identifyAll() {
    for (auto& p : bag_) p.item.unidentified = false;
    for (auto& worn : equipped_) {
        if (worn) worn->unidentified = false;
    }
}

int Inventory::repairPrice(int part) const {
    if (const auto slot = partSlot(part)) {
        const auto& worn = equipped(*slot);
        return worn ? worn->repairPrice() : 0;
    }
    int total = 0;
    for (const auto& p : bag_) {
        if (p.item.def->cls == Class::Weapon || p.item.def->cls == Class::Armour) total += p.item.repairPrice();
    }
    return total;
}

void Inventory::repair(int part) {
    if (const auto slot = partSlot(part)) {
        if (auto& worn = equipped_[static_cast<int>(*slot)]) worn->wear = 0;
    } else if (part == kRepairBag) {
        for (auto& p : bag_) {
            if (p.item.def->cls == Class::Weapon || p.item.def->cls == Class::Armour) p.item.wear = 0;
        }
    }
}

std::array<int, ItemDef::kStatCount> Inventory::stats() const {
    std::array<int, ItemDef::kStatCount> total{};
    for (const auto& worn : equipped_) {
        if (!worn || worn->broken()) continue;
        for (int i = 0; i < ItemDef::kStatCount; ++i) total[static_cast<std::size_t>(i)] += worn->def->stats[static_cast<std::size_t>(i)];
    }
    return total;
}

std::array<int, 8> Inventory::elements() const {
    std::array<int, 8> total{};
    for (const auto& worn : equipped_) {
        if (!worn) continue;
        for (std::size_t i = 0; i < total.size(); ++i) total[i] += worn->def->elements[i];
    }
    return total;
}

std::vector<int> Inventory::heroLayers() const {
    std::vector<int> layers = {0, 1};
    auto add = [&](int layer) {
        if (layer >= 0 && std::ranges::find(layers, layer) == layers.end()) layers.push_back(layer);
    };
    const auto& body = equipped(EquipSlot::Body);
    const auto& weapon = equipped(EquipSlot::Weapon);
    const auto& shield = equipped(EquipSlot::Shield);
    if (body) add(body->def->heroLayer);
    if (weapon) {
        add(weapon->def->heroLayer);
        add(weapon->def->glowLayer);
    }
    if (shield && !(weapon && weapon->def->twoHanded())) add(shield->def->heroLayer);
    return layers;
}

int rollGold(int chance, int min, int max, Random& random) {
    if (chance <= 0 || random.next(100) >= chance) return 0;
    if (max < min) std::swap(min, max);
    return std::max(1, min + random.next(max - min + 1));
}

const ItemDef* pickDrop(const ItemTable& items, int level, Random& random) {
    level = std::max(1, level);
    std::vector<const ItemDef*> pool;
    int total = 0;
    for (const auto& def : items.items) {
        if (def.cls == Class::Special || def.quality != 0 || def.itemLevel > level) continue;
        pool.push_back(&def);
        total += std::max(1, def.dropWeight);
    }
    if (pool.empty()) return nullptr;
    int pick = random.next(total);
    for (const ItemDef* def : pool) {
        pick -= std::max(1, def->dropWeight);
        if (pick < 0) return def;
    }
    return pool.back();
}

}  // namespace sf

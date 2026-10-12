#include "logic/save_game.h"

#include <charconv>
#include <format>

#include "formats/binary_reader.h"

namespace sf {
namespace {

constexpr int kVersion = 1;

std::vector<long long> numbers(std::string_view value, std::size_t count, int line) {
    std::vector<long long> out;
    while (!value.empty()) {
        const auto comma = value.find(',');
        const auto part = value.substr(0, comma);
        long long n = 0;
        const auto [end, ec] = std::from_chars(part.data(), part.data() + part.size(), n);
        if (ec != std::errc() || end != part.data() + part.size()) {
            throw FormatError(std::format("save line {}: '{}' is not a number", line, part));
        }
        out.push_back(n);
        if (comma == std::string_view::npos) break;
        value.remove_prefix(comma + 1);
    }
    if (out.size() != count) throw FormatError(std::format("save line {}: expected {} numbers", line, count));
    return out;
}

SaveGame::ItemRef ref(const Item& item) {
    return {static_cast<int>(item.def->cls), item.def->code, item.count};
}

}  // namespace

std::string SaveGame::write() const {
    std::string out = std::format("# ShadowFlare saved game\nversion={}\nmap={}\nscenario={}\n", kVersion, map, scenario);
    out += std::format("at={},{},{}\nhp={}\nlevel={}\nexperience={}\ngold={}\n", x, y, direction, hp, level, experience, gold);
    for (const auto& [key, value] : flags) out += std::format("flag={},{},{}\n", key.first, key.second, value);
    for (const auto& e : bag) out += std::format("bag={},{},{},{},{}\n", e.item.cls, e.item.code, e.item.count, e.x, e.y);
    for (int slot = 0; slot < kEquipSlotCount; ++slot) {
        if (const auto& e = equipped[static_cast<std::size_t>(slot)]) {
            out += std::format("wear={},{},{},{}\n", slot, e->cls, e->code, e->count);
        }
    }
    return out;
}

SaveGame SaveGame::parse(std::string_view text) {
    SaveGame save;
    bool versioned = false;
    int lineNumber = 0;
    while (!text.empty()) {
        const auto newline = text.find('\n');
        std::string_view line = text.substr(0, newline);
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) throw FormatError(std::format("save line {}: expected key=value", lineNumber));
        const auto key = line.substr(0, equals);
        const auto value = line.substr(equals + 1);
        if (key == "version") {
            if (numbers(value, 1, lineNumber)[0] != kVersion) throw FormatError("save is from another version");
            versioned = true;
        } else if (key == "map") {
            save.map = value;
        } else if (key == "scenario") {
            save.scenario = value;
        } else if (key == "at") {
            const auto v = numbers(value, 3, lineNumber);
            save.x = static_cast<int>(v[0]);
            save.y = static_cast<int>(v[1]);
            save.direction = static_cast<int>(v[2]);
        } else if (key == "hp") {
            save.hp = static_cast<int>(numbers(value, 1, lineNumber)[0]);
        } else if (key == "level") {
            save.level = static_cast<int>(numbers(value, 1, lineNumber)[0]);
        } else if (key == "experience") {
            save.experience = numbers(value, 1, lineNumber)[0];
        } else if (key == "gold") {
            save.gold = static_cast<int>(numbers(value, 1, lineNumber)[0]);
        } else if (key == "flag") {
            const auto v = numbers(value, 3, lineNumber);
            save.flags[{static_cast<std::int32_t>(v[0]), static_cast<std::int32_t>(v[1])}] = static_cast<std::int32_t>(v[2]);
        } else if (key == "bag") {
            const auto v = numbers(value, 5, lineNumber);
            save.bag.push_back({{static_cast<int>(v[0]), static_cast<std::int32_t>(v[1]), static_cast<int>(v[2])},
                                static_cast<int>(v[3]), static_cast<int>(v[4])});
        } else if (key == "wear") {
            const auto v = numbers(value, 4, lineNumber);
            if (v[0] < 0 || v[0] >= kEquipSlotCount) throw FormatError(std::format("save line {}: no slot {}", lineNumber, v[0]));
            save.equipped[static_cast<std::size_t>(v[0])] =
                ItemRef{static_cast<int>(v[1]), static_cast<std::int32_t>(v[2]), static_cast<int>(v[3])};
        }  // unknown keys are skipped, so newer saves still mostly load
    }
    if (!versioned) throw FormatError("not a saved game");
    if (save.map.empty()) throw FormatError("save has no map");
    return save;
}

void SaveGame::store(const Inventory& inventory) {
    gold = inventory.gold();
    bag.clear();
    for (const auto& p : inventory.bag()) bag.push_back({ref(p.item), p.x, p.y});
    for (int slot = 0; slot < kEquipSlotCount; ++slot) {
        const auto& worn = inventory.equipped(static_cast<EquipSlot>(slot));
        equipped[static_cast<std::size_t>(slot)] = worn ? std::optional(ref(*worn)) : std::nullopt;
    }
}

int SaveGame::restore(Inventory& inventory, const ItemTable& items) const {
    inventory.clear();
    inventory.addGold(gold);
    int skipped = 0;
    auto find = [&](const ItemRef& r) { return items.find(static_cast<ItemDef::Class>(r.cls), r.code); };
    for (const auto& e : bag) {
        const ItemDef* def = find(e.item);
        if (!def || !inventory.addAt({def, e.item.count}, e.x, e.y)) ++skipped;
    }
    for (int slot = 0; slot < kEquipSlotCount; ++slot) {
        const auto& r = equipped[static_cast<std::size_t>(slot)];
        if (!r) continue;
        if (const ItemDef* def = find(*r)) {
            inventory.equip(static_cast<EquipSlot>(slot), {def, r->count});
        } else {
            ++skipped;
        }
    }
    return skipped;
}

}  // namespace sf

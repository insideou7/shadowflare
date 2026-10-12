#include "logic/save_game.h"

#include <algorithm>
#include <charconv>
#include <format>

#include "formats/binary_reader.h"

namespace sf {
namespace {

constexpr int kVersion = 1;

// A line of comma-separated numbers: `count` of them, or up to `most`.
std::vector<long long> numbers(std::string_view value, std::size_t count, int line, std::size_t most = 0) {
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
    if (out.size() < count || out.size() > std::max(count, most)) {
        throw FormatError(std::format("save line {}: expected {} numbers", line, count));
    }
    return out;
}

HeroClass heroClass(long long n, int line) {
    if (n < 0 || n >= kHeroClassCount) throw FormatError(std::format("save line {}: no class {}", line, n));
    return static_cast<HeroClass>(n);
}

constexpr std::size_t kMaxModifierNumbers = 2 * SaveGame::kSavedModifiers;

SaveGame::ItemRef ref(const Item& item) {
    SaveGame::ItemRef r{static_cast<int>(item.def->cls), item.def->code, item.count, item.wear, item.unidentified};
    std::ranges::copy(item.modifiers, r.modifiers.begin());
    std::ranges::copy(item.elementModifiers, r.modifiers.begin() + ItemDef::kModifierCount);
    return r;
}

// An item's wear and whether it is unidentified, after its other numbers,
// then each modifier it rolled as index,value; left off when it is new,
// identified and plain.
std::string condition(const SaveGame::ItemRef& r) {
    std::string mods;
    for (std::size_t i = 0; i < r.modifiers.size(); ++i) {
        if (r.modifiers[i] != 0) mods += std::format(",{},{}", i, r.modifiers[i]);
    }
    if (r.wear == 0 && !r.unidentified && mods.empty()) return {};
    return std::format(",{},{}{}", r.wear, r.unidentified ? 1 : 0, mods);
}

// cls,code,count then `extra` numbers, then optionally wear,unidentified
// and modifiers.
SaveGame::ItemRef readRef(const std::vector<long long>& v, std::size_t extra, int line) {
    SaveGame::ItemRef r{static_cast<int>(v[0]), static_cast<std::int32_t>(v[1]), static_cast<int>(v[2])};
    if (v.size() >= 5 + extra) {
        r.wear = static_cast<int>(v[3 + extra]);
        r.unidentified = v[4 + extra] != 0;
    }
    for (std::size_t i = 5 + extra; i + 1 < v.size(); i += 2) {
        if (v[i] < 0 || static_cast<std::size_t>(v[i]) >= r.modifiers.size()) {
            throw FormatError(std::format("save line {}: no modifier {}", line, v[i]));
        }
        r.modifiers[static_cast<std::size_t>(v[i])] = static_cast<std::int32_t>(v[i + 1]);
    }
    return r;
}

Item toItem(const ItemDef* def, const SaveGame::ItemRef& r) {
    Item item{def, r.count};
    item.wear = r.wear;
    item.unidentified = r.unidentified;
    std::copy_n(r.modifiers.begin(), ItemDef::kModifierCount, item.modifiers.begin());
    std::copy_n(r.modifiers.begin() + ItemDef::kModifierCount, 8, item.elementModifiers.begin());
    return item;
}

}  // namespace

std::string SaveGame::write() const {
    std::string out = std::format("# ShadowFlare saved game\nversion={}\nmap={}\nscenario={}\n", kVersion, map, scenario);
    out += std::format("at={},{},{}\nhp={}\nlevel={}\nexperience={}\ngold={}\n", x, y, direction, hp, level, experience, gold);
    if (career) {
        // career=class,offered,kills,kills by weapon type x 9; classes= the
        // class at each level-up, one digit each.
        out += std::format("career={},{},{}", static_cast<int>(career->current), static_cast<int>(career->offered), career->kills);
        for (int k : career->killsBy) out += std::format(",{}", k);
        out += "\nclasses=";
        for (HeroClass c : career->history) out += static_cast<char>('0' + static_cast<int>(c));
        out += '\n';
    }
    if (mp) out += std::format("mp={}\n", *mp);
    if (spells) {
        // spell=number,level,uses for each spell learned; bar= the spells
        // on F1-F8 (-1 empty), then the selected one.
        for (int s = 0; s < kSpellCount; ++s) {
            const auto i = static_cast<std::size_t>(s);
            if (spells->level[i] > 0) out += std::format("spell={},{},{},{}\n", s, spells->level[i], spells->uses[i], spellsOn[i] ? 1 : 0);
        }
        out += "bar=";
        for (int b : spells->bar) out += std::format("{},", b);
        out += std::format("{}\n", spells->selected);
    }
    if (originalMode) out += "original=1\n";
    if (partner) {
        // partner=kind,active,revive ticks,hp, then each kind's level and
        // experience.
        out += std::format("partner={},{},{},{}", partner->kind, partner->active ? 1 : 0, partner->reviveTicks, partnerHp);
        for (int k = 0; k < kPartnerKinds; ++k) {
            const auto i = static_cast<std::size_t>(k);
            out += std::format(",{},{}", partner->levels[i], partner->experience[i]);
        }
        out += '\n';
    }
    for (const auto& [key, value] : flags) out += std::format("flag={},{},{}\n", key.first, key.second, value);
    // Items: class,code,count[,x,y], then wear,unidentified and the rolled
    // modifiers as index,value pairs, when any of them is not 0.
    for (const auto& e : bag) out += std::format("bag={},{},{},{},{}{}\n", e.item.cls, e.item.code, e.item.count, e.x, e.y, condition(e.item));
    for (const auto& e : belt) out += std::format("belt={},{},{},{},{}{}\n", e.item.cls, e.item.code, e.item.count, e.x, e.y, condition(e.item));
    for (const auto& e : storage) {
        out += std::format("stored={},{},{},{},{}{}\n", e.item.cls, e.item.code, e.item.count, e.x, e.y, condition(e.item));
    }
    for (int slot = 0; slot < kEquipSlotCount; ++slot) {
        if (const auto& e = equipped[static_cast<std::size_t>(slot)]) {
            out += std::format("wear={},{},{},{}{}\n", slot, e->cls, e->code, e->count, condition(*e));
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
        } else if (key == "mp") {
            save.mp = static_cast<int>(numbers(value, 1, lineNumber)[0]);
        } else if (key == "spell") {
            const auto v = numbers(value, 4, lineNumber);
            if (v[0] < 0 || v[0] >= kSpellCount) throw FormatError(std::format("save line {}: no spell {}", lineNumber, v[0]));
            if (!save.spells) save.spells.emplace();
            const auto i = static_cast<std::size_t>(v[0]);
            save.spells->level[i] = static_cast<int>(std::clamp<long long>(v[1], 1, kMaxSpellLevel));
            save.spells->uses[i] = static_cast<int>(std::max<long long>(v[2], 0));
            save.spellsOn[i] = v[3] != 0;
        } else if (key == "bar") {
            const auto v = numbers(value, kSpellBarSize + 1, lineNumber);
            if (!save.spells) save.spells.emplace();
            auto spell = [&](long long n) { return n >= 0 && n < kSpellCount ? static_cast<int>(n) : -1; };
            for (std::size_t b = 0; b < kSpellBarSize; ++b) save.spells->bar[b] = spell(v[b]);
            save.spells->selected = spell(v[kSpellBarSize]);
        } else if (key == "partner") {
            const auto v = numbers(value, 4 + 2 * kPartnerKinds, lineNumber);
            if (v[0] < 0 || v[0] >= kPartnerKinds) throw FormatError(std::format("save line {}: no such companion", lineNumber));
            PartnerRecord p;
            p.kind = static_cast<int>(v[0]);
            p.active = v[1] != 0;
            p.reviveTicks = static_cast<int>(std::max(0LL, v[2]));
            save.partnerHp = static_cast<int>(std::max(0LL, v[3]));
            for (std::size_t i = 0; i < kPartnerKinds; ++i) {
                p.levels[i] = static_cast<int>(std::clamp(v[4 + 2 * i], 1LL, static_cast<long long>(kPartnerMaxLevel)));
                p.experience[i] = static_cast<int>(std::max(0LL, v[5 + 2 * i]));
            }
            save.partner = p;
        } else if (key == "original") {
            save.originalMode = numbers(value, 1, lineNumber)[0] != 0;
        } else if (key == "level") {
            save.level = static_cast<int>(numbers(value, 1, lineNumber)[0]);
        } else if (key == "career") {
            const auto v = numbers(value, 12, lineNumber);
            if (!save.career) save.career.emplace();
            save.career->current = heroClass(v[0], lineNumber);
            save.career->offered = heroClass(v[1], lineNumber);
            save.career->kills = static_cast<int>(v[2]);
            for (std::size_t i = 0; i < save.career->killsBy.size(); ++i) save.career->killsBy[i] = static_cast<int>(v[3 + i]);
        } else if (key == "classes") {
            if (!save.career) save.career.emplace();
            save.career->history.clear();
            for (char c : value) save.career->history.push_back(heroClass(c - '0', lineNumber));
        } else if (key == "experience") {
            save.experience = numbers(value, 1, lineNumber)[0];
        } else if (key == "gold") {
            save.gold = static_cast<int>(numbers(value, 1, lineNumber)[0]);
        } else if (key == "flag") {
            const auto v = numbers(value, 3, lineNumber);
            save.flags[{static_cast<std::int32_t>(v[0]), static_cast<std::int32_t>(v[1])}] = static_cast<std::int32_t>(v[2]);
        } else if (key == "bag" || key == "belt" || key == "stored") {
            const auto v = numbers(value, 5, lineNumber, 7 + kMaxModifierNumbers);
            (key == "bag" ? save.bag : key == "belt" ? save.belt : save.storage).push_back({readRef(v, 2, lineNumber), static_cast<int>(v[3]), static_cast<int>(v[4])});
        } else if (key == "wear") {
            const auto v = numbers(value, 4, lineNumber, 6 + kMaxModifierNumbers);
            if (v[0] < 0 || v[0] >= kEquipSlotCount) throw FormatError(std::format("save line {}: no slot {}", lineNumber, v[0]));
            save.equipped[static_cast<std::size_t>(v[0])] = readRef({v.begin() + 1, v.end()}, 0, lineNumber);
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
    belt.clear();
    for (int y = 0; y < Inventory::kBeltHeight; ++y) {
        for (int x = 0; x < Inventory::kBeltWidth; ++x) {
            if (const auto& item = inventory.beltAt(x, y)) belt.push_back({ref(*item), x, y});
        }
    }
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
        if (!def || !inventory.addAt(toItem(def, e.item), e.x, e.y)) ++skipped;
    }
    for (const auto& e : belt) {
        const ItemDef* def = find(e.item);
        if (!def || !inventory.addToBeltAt(toItem(def, e.item), e.x, e.y)) ++skipped;
    }
    for (int slot = 0; slot < kEquipSlotCount; ++slot) {
        const auto& r = equipped[static_cast<std::size_t>(slot)];
        if (!r) continue;
        if (const ItemDef* def = find(*r)) {
            inventory.equip(static_cast<EquipSlot>(slot), toItem(def, *r));
        } else {
            ++skipped;
        }
    }
    return skipped;
}

void SaveGame::storeStorage(const ShopStock& stock) {
    storage.clear();
    for (const auto& g : stock.goods) storage.push_back({ref(g.item), g.x, g.y});
}

int SaveGame::restoreStorage(ShopStock& stock, const ItemTable& items) const {
    stock.goods.clear();
    int skipped = 0;
    for (const auto& e : storage) {
        const ItemDef* def = items.find(static_cast<ItemDef::Class>(e.item.cls), e.item.code);
        if (!def || !stock.fits(def->width, def->height, e.x, e.y)) {
            ++skipped;
            continue;
        }
        stock.goods.push_back({toItem(def, e.item), e.x, e.y});
    }
    return skipped;
}

}  // namespace sf

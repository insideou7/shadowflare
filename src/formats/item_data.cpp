#include "formats/item_data.h"

#include <algorithm>
#include <format>

#include "formats/binary_reader.h"
#include "formats/rclib.h"

namespace sf {

namespace {

constexpr std::string_view kMagic = "SFItemDataV0000\x1a";

// Strings are stored with every byte inverted, like Table.Tbd's.
std::string invertedString(BinaryReader& in) {
    const std::int32_t length = in.i32();
    if (length < 0 || length > 4096) throw FormatError("bad item name length");
    std::string s;
    for (std::uint8_t b : in.bytes(static_cast<std::size_t>(length))) s.push_back(static_cast<char>(~b));
    return s;
}

void skipWords(BinaryReader& in, std::size_t count) { in.skip(4 * count); }

ItemDef readRecord(BinaryReader& in, ItemDef::Class cls) {
    ItemDef d;
    d.cls = cls;
    d.name = invertedString(in);
    invertedString(in);  // the name shown before the item is identified
    // The part every class shares, in file order (object offsets in
    // brackets, as in docs/formats.md).
    d.subtype = in.i32();     // 0x10
    d.code = in.i32();        // 0x14
    d.quality = in.i32();     // 0x18
    d.poolMask = in.i32();    // 0x88
    d.dropWeight = in.i32();  // 0x78
    d.price = in.i32();       // 0x40
    in.i32();                 // 0x44 fixed price
    d.width = in.i32();       // 0x20
    d.height = in.i32();      // 0x24
    in.i32();                 // 0x28 weight
    d.invBank = in.i32();     // 0x2c
    d.invPattern = in.i32();  // 0x30
    d.groundFolder = in.i32();   // 0x34
    d.groundPicture = in.i32();  // 0x38
    skipWords(in, 4);         // 0x3c icon draw parameter, 0x50-0x58 ground tint
    const std::int32_t glow = in.i32();  // 0x68
    skipWords(in, 3 + 1 + 2);  // 0x6c-0x74 its tint, 0x7c, 0x80[2]
    if (d.width < 1 || d.height < 1 || d.width > 9 || d.height > 4) {
        throw FormatError(std::format("item \"{}\" has a bad size {}x{}", d.name, d.width, d.height));
    }

    auto stats = [&] {
        for (auto& s : d.stats) s = in.i32();
    };
    switch (cls) {
        case ItemDef::Class::Weapon:
            in.i32();  // durability
            stats();
            d.itemLevel = in.i32();
            d.requiredLevel = in.i32();
            skipWords(in, 1 + 3);  // 0xec, weapon effect tint
            d.heroLayer = in.i32();
            skipWords(in, 3 + 5);  // layer tint, projectile ways, orbit, 0x104, 0x108, pierce
            d.twoHandedFlag = in.i32() != 0;
            for (auto& e : d.elements) e = in.i32();  // 0xc8
            skipWords(in, 39 * 3 + 8 * 3);  // modifiers, elemental modifiers
            d.glowLayer = glow;
            break;
        case ItemDef::Class::Armour:
            in.i32();  // durability
            stats();
            d.itemLevel = in.i32();
            d.requiredLevel = in.i32();
            d.heroLayer = in.i32();
            skipWords(in, 3);  // layer tint
            for (auto& e : d.elements) e = in.i32();  // 0xc8
            skipWords(in, 39 * 3 + 8 * 3);
            d.glowLayer = glow;
            break;
        case ItemDef::Class::Accessory:
            d.itemLevel = in.i32();
            d.requiredLevel = in.i32();
            skipWords(in, 39 * 3 + 8 * 3);
            break;
        case ItemDef::Class::Consumable:
            d.itemLevel = in.i32();
            for (auto& e : d.effect) e = in.i32();
            break;
        case ItemDef::Class::Special: break;
    }
    return d;
}

}  // namespace

const ItemDef* ItemTable::find(ItemDef::Class cls, std::int32_t code) const {
    const auto it = std::ranges::find_if(items, [&](const ItemDef& d) { return d.cls == cls && d.code == code; });
    return it == items.end() ? nullptr : &*it;
}

ItemTable ItemTable::parseRecords(std::span<const std::uint8_t> body) {
    BinaryReader in(body);
    ItemTable table;
    for (int c = 0; c < 5; ++c) {
        const std::int32_t count = in.i32();
        // A record is at least two string lengths and the shared fields.
        if (count < 0 || static_cast<std::size_t>(count) > in.remaining() / (4 * 27)) throw FormatError("bad item count");
        for (std::int32_t i = 0; i < count; ++i) table.items.push_back(readRecord(in, static_cast<ItemDef::Class>(c)));
    }
    if (!in.atEnd()) throw FormatError("unexpected data after the item records");
    return table;
}

ItemTable ItemTable::parse(std::span<const std::uint8_t> data, const std::array<std::uint8_t, 256>& substitution) {
    BinaryReader file(data);
    if (file.chars(kMagic.size()) != kMagic) throw FormatError("not an item file");
    const std::int32_t checksum = file.i32();
    if (file.i32() != 1) throw FormatError("unpacked item files are not supported");
    // An RCLIB-L block whose packed bytes first go through the table.
    if (file.chars(8) != std::string_view("RCLIB-L\x1a", 8)) throw FormatError("missing RCLIB-L header");
    const std::uint32_t unpacked = file.u32();
    const std::uint32_t packedSize = file.u32();
    const auto packed = file.bytes(packedSize);
    std::vector<std::uint8_t> substituted(packed.size());
    std::ranges::transform(packed, substituted.begin(), [&](std::uint8_t b) { return substitution[b]; });
    const auto body = rclib::lzDecode(substituted, unpacked);
    // The game checks the sum of the unpacked bytes as signed chars (0x463bd1).
    std::int32_t sum = 0;
    for (std::uint8_t b : body) sum += static_cast<std::int8_t>(b);
    if (sum != checksum) throw FormatError("item file checksum mismatch (wrong ShadowFlare.exe?)");
    return parseRecords(body);
}

std::array<std::uint8_t, 256> itemSubstitutionTable(std::span<const std::uint8_t> exe) {
    constexpr std::uint32_t kStart = 0x463031, kEnd = 0x46376e;
    constexpr std::uint32_t kTableOffset = 0x3c;  // esp offset of entry 0

    // Find the file offset of kStart through the PE section table.
    BinaryReader pe(exe);
    if (pe.chars(2) != "MZ") throw FormatError("not an executable");
    pe.seek(0x3c);
    pe.seek(pe.u32());
    if (pe.chars(4) != std::string_view("PE\0\0", 4)) throw FormatError("not a PE executable");
    pe.skip(2);  // machine
    const int sections = static_cast<std::uint16_t>(pe.i16());
    pe.skip(12);  // timestamp, symbol table
    const std::uint32_t optionalSize = static_cast<std::uint16_t>(pe.i16());
    pe.skip(2);  // characteristics
    const std::size_t optional = pe.offset();
    pe.seek(optional + 28);
    const std::uint32_t imageBase = pe.u32();
    std::optional<std::size_t> start;
    std::size_t sectionEnd = 0;
    for (int s = 0; s < sections; ++s) {
        pe.seek(optional + optionalSize + 40 * static_cast<std::size_t>(s) + 12);
        const std::uint32_t va = imageBase + pe.u32();
        const std::uint32_t rawSize = pe.u32();
        const std::uint32_t rawOffset = pe.u32();
        if (kStart >= va && kEnd <= va + rawSize) {
            start = rawOffset + (kStart - va);
            sectionEnd = rawOffset + (kEnd - va);
        }
    }
    if (!start) throw FormatError("executable has no code at the item table");

    // Every entry is a `mov byte [esp+disp], imm8`, or `mov [esp+disp], bl`
    // where bl is 0. Pushes in between move esp.
    BinaryReader code(exe);
    code.seek(*start);
    if (sectionEnd > exe.size()) throw FormatError("executable is truncated");
    std::array<int, 256> table;
    table.fill(-1);
    std::uint32_t shift = 0;
    auto store = [&](std::uint32_t disp, int value) {
        const std::int64_t index = static_cast<std::int64_t>(disp) - shift - kTableOffset;
        if (index >= 0 && index < 256) table[static_cast<std::size_t>(index)] = value;
    };
    while (code.offset() < sectionEnd) {
        const std::uint8_t op = code.u8();
        if (op == 0x53) {  // push ebx
            shift += 4;
            continue;
        }
        if (op == 0x68) {  // push imm32
            code.skip(4);
            shift += 4;
            continue;
        }
        const std::uint8_t modrm = code.u8();
        if (code.u8() != 0x24) throw FormatError("unexpected instruction in the item table code");
        if (op == 0xc6 && modrm == 0x44) {
            const std::uint8_t disp = code.u8();
            store(disp, code.u8());
        } else if (op == 0xc6 && modrm == 0x84) {
            const std::uint32_t disp = code.u32();
            store(disp, code.u8());
        } else if (op == 0x88 && modrm == 0x5c) {
            store(code.u8(), 0);
        } else if (op == 0x88 && modrm == 0x9c) {
            store(code.u32(), 0);
        } else if (op == 0x8d && modrm == 0x4c) {  // lea ecx, [esp+disp8]
            code.skip(1);
        } else {
            throw FormatError("unexpected instruction in the item table code");
        }
    }
    std::array<std::uint8_t, 256> result{};
    std::array<bool, 256> seen{};
    for (std::size_t i = 0; i < 256; ++i) {
        if (table[i] < 0 || seen[static_cast<std::size_t>(table[i])]) throw FormatError("item table is not a permutation");
        seen[static_cast<std::size_t>(table[i])] = true;
        result[i] = static_cast<std::uint8_t>(table[i]);
    }
    return result;
}

}  // namespace sf

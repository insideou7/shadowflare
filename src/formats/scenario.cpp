#include "formats/scenario.h"

#include "formats/binary_reader.h"

namespace sf {

namespace {

// A fixed-size, NUL-padded string field.
std::string fixedString(BinaryReader& in, std::size_t size) {
    const std::string_view raw = in.chars(size);
    return std::string(raw.substr(0, raw.find('\0')));
}

std::int32_t count(BinaryReader& in) {
    const std::int32_t n = in.i32();
    if (n < 0 || static_cast<std::size_t>(n) > in.remaining()) throw FormatError("bad count in scenario file");
    return n;
}

// Every group shares this record layout; only the size of the trailing
// group-specific block differs.
ScenarioControl::Placement readPlacement(BinaryReader& in, ScenarioControl::Group group, std::size_t extraSize) {
    ScenarioControl::Placement p;
    p.group = group;
    p.id = in.i32();
    p.kind = in.i32();
    if (const std::int32_t nameLength = count(in)) {
        p.name = std::string(in.chars(nameLength));
        in.skip(4);  // name colour
    }
    in.skip(4);
    p.x = in.i32();
    p.y = in.i32();
    for (auto& v : p.rect) v = in.i32();
    p.direction = in.i32();
    in.skip(4 * static_cast<std::size_t>(count(in)));
    if (in.i32() == 1) {  // a table of 4 + 3 * 2 bytes per entry, not decoded yet
        in.skip(10 * static_cast<std::size_t>(count(in)));
    }
    in.skip(4);
    if (group == ScenarioControl::Group::Enemy) {
        // Offsets are in 32-bit words into the 0x13c-byte enemy block.
        BinaryReader block(in.bytes(extraSize));
        std::int32_t words[0x13c / 4];
        for (auto& w : words) w = block.i32();
        p.stats.element = words[6];
        p.stats.hp = words[8];
        p.stats.attack = words[9];
        p.stats.defense = words[11];
        p.stats.experience = words[13];
        p.stats.level = words[14];
        p.stats.goldMin = words[50];
        p.stats.goldMax = words[51];
    } else {
        in.skip(extraSize);
    }
    return p;
}

}  // namespace

ScenarioControl ScenarioControl::parse(std::span<const std::uint8_t> data) {
    BinaryReader in(data);
    if (!in.chars(16).starts_with("MCED DATA v0000")) throw FormatError("not a scenario control file");
    ScenarioControl s;
    in.skip(0x104);  // AI data path
    s.mapPath = fixedString(in, 0x104);
    in.skip(12);
    s.title = fixedString(in, 0x100);
    // Character types used by the scenario (objects, people, enemies),
    // presumably so they can be preloaded.
    for (int i = 0; i < 3; ++i) in.skip(4 * static_cast<std::size_t>(count(in)));

    const std::pair<Group, std::size_t> groups[] = {
        {Group::Object, 0x34}, {Group::Person, 0x2c}, {Group::Enemy, 0x13c}, {Group::Item, 0x10}};
    for (const auto& [group, extraSize] : groups) {
        const std::int32_t n = count(in);
        for (std::int32_t i = 0; i < n; ++i) s.placements.push_back(readPlacement(in, group, extraSize));
    }
    return s;
}

}  // namespace sf

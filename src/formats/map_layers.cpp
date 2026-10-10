#include "formats/map_layers.h"

#include <string_view>

#include "formats/binary_reader.h"
#include "formats/rclib.h"

namespace sf {

namespace {

// A flag byte, then either an RCLIB block or `size` raw bytes.
std::vector<std::uint8_t> readMaybePacked(BinaryReader& in, std::size_t size) {
    if (in.u8()) return rclib::readBlock(in);
    auto raw = in.bytes(size);
    return {raw.begin(), raw.end()};
}

}  // namespace

GroundLayer GroundLayer::parse(std::span<const std::uint8_t> data) {
    BinaryReader in(data);
    if (!in.chars(16).starts_with("RPGSCRN_GNDv000")) throw FormatError("not a ground file");
    GroundLayer g;
    g.width = in.i32();
    g.height = in.i32();
    g.chipWidth = in.i32();
    g.chipHeight = in.i32();
    g.magX = in.i32();
    g.magY = in.i32();
    if (g.width <= 0 || g.height <= 0 || g.width > 4096 || g.height > 4096) throw FormatError("bad ground size");

    const std::size_t n = static_cast<std::size_t>(g.width) * g.height;
    const auto area = readMaybePacked(in, n * 6);
    if (area.size() < n * 6) throw FormatError("ground data too small");
    // Three planes stored one after another, not interleaved.
    BinaryReader planes(area);
    g.cells.resize(n);
    for (auto& c : g.cells) c.flags = planes.i16();
    for (auto& c : g.cells) c.bank = planes.i16();
    for (auto& c : g.cells) c.pattern = planes.i16();
    // A walkability grid follows; decoded later with collision.
    return g;
}

ObjectLayer ObjectLayer::parse(std::span<const std::uint8_t> data) {
    BinaryReader in(data);
    const std::string_view magic = in.chars(16);
    if (!magic.starts_with("RPGSCRN_OBJv")) throw FormatError("not an object file");
    const int version = magic[14] - '0';
    const std::int32_t count = in.i32();
    if (count < 0) throw FormatError("bad object count");
    ObjectLayer layer;
    layer.objects.reserve(count);
    for (std::int32_t i = 0; i < count; ++i) {
        MapObject o;
        o.x = in.i32();
        o.y = in.i32();
        o.bank = in.i16();
        o.pattern = in.i16();
        o.status = in.i16();
        o.trans = in.i16();
        o.height = in.i16();
        o.palette = in.i16();
        if (version >= 1) {
            o.r = in.i16();
            o.g = in.i16();
            o.b = in.i16();
        }
        for (auto& v : o.judge) v = in.i32();
        layer.objects.push_back(o);
    }
    return layer;
}

}  // namespace sf

#include "formats/map_layers.h"

#include <algorithm>
#include <cmath>
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

int floorDiv(long long a, long long b) {
    long long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return static_cast<int>(q);
}

// RKC_RPGSCRN_GROUNDBLOCK::CalcGroundPos: map pixel to judge cell, using
// the screen base parameters (15, 10) scaled by the ground magnification.
void calcGroundPos(const GroundLayer& g, long long sx, long long sy, int& jx, int& jy) {
    const long long p1 = 15LL * g.magX / 100, p2 = 10LL * g.magY / 100;
    const long long d = 2 * p1 * p2;
    jx = floorDiv(p1 * sy + p2 * sx, d);
    jy = floorDiv(p1 * sy - p2 * sx, d);
}

}  // namespace

std::int16_t GroundLayer::judgeAt(double worldX, double worldY) const {
    const int jx = static_cast<int>(std::floor(worldX / magX)) - judgeX;
    const int jy = static_cast<int>(std::floor(worldY / magY)) - judgeY;
    if (jx < 0 || jy < 0 || jx >= judgeWidth || jy >= judgeHeight) return kBlocked;
    return judge[static_cast<std::size_t>(jy) * judgeWidth + jx];
}

bool GroundLayer::onGrid(double worldX, double worldY) const {
    const int jx = static_cast<int>(std::floor(worldX / magX)) - judgeX;
    const int jy = static_cast<int>(std::floor(worldY / magY)) - judgeY;
    return jx >= 0 && jy >= 0 && jx < judgeWidth && jy < judgeHeight;
}

void GroundLayer::screenBounds(int& left, int& top, int& right, int& bottom) const {
    if (isoTile <= 0) {
        left = top = 0;
        right = width * chipWidth;
        bottom = height * chipHeight;
        return;
    }
    // The corners of the tile grid; the far left one is (0, height).
    const int w = width * isoTile, h = height * isoTile;
    int unused = 0;
    worldToScreen(0, h, left, unused);
    worldToScreen(w, 0, right, unused);
    worldToScreen(0, 0, unused, top);
    worldToScreen(w, h, unused, bottom);
}

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
    if (g.chipWidth <= 0 || g.chipHeight <= 0 || g.chipWidth > 1024 || g.chipHeight > 1024) {
        throw FormatError("bad ground chip size");
    }

    const std::size_t n = static_cast<std::size_t>(g.width) * g.height;
    const auto area = readMaybePacked(in, n * 6);
    if (area.size() < n * 6) throw FormatError("ground data too small");
    // Three planes stored one after another, not interleaved.
    BinaryReader planes(area);
    g.cells.resize(n);
    for (auto& c : g.cells) c.flags = planes.i16();
    for (auto& c : g.cells) c.bank = planes.i16();
    for (auto& c : g.cells) c.pattern = planes.i16();

    // The judge grid covers the bounding box of the map's corners in
    // judge cells, plus a cell of margin (CalcAreaJudgeFromMap).
    if (g.magX < 10 || g.magY < 10 || g.magX > 10000 || g.magY > 10000) throw FormatError("bad ground magnification");
    const long long pw = static_cast<long long>(g.width) * g.chipWidth;
    const long long ph = static_cast<long long>(g.height) * g.chipHeight;
    int x00, y00, xw0, yw0, x0h, y0h, xwh, ywh;
    calcGroundPos(g, 0, 0, x00, y00);
    calcGroundPos(g, pw, 0, xw0, yw0);
    calcGroundPos(g, 0, ph, x0h, y0h);
    calcGroundPos(g, pw, ph, xwh, ywh);
    g.judgeX = x00 - 1;
    g.judgeY = yw0 - 1;
    g.judgeWidth = xwh - g.judgeX + 1;
    g.judgeHeight = y0h - g.judgeY + 1;
    const std::size_t jn = static_cast<std::size_t>(g.judgeWidth) * g.judgeHeight;
    const auto judge = readMaybePacked(in, jn * 2);
    if (judge.size() < jn * 2) throw FormatError("judge data too small");
    BinaryReader j(judge);
    g.judge.resize(jn);
    for (auto& v : g.judge) v = j.i16();
    return g;
}

bool MapObject::hides(double worldX, double worldY) const {
    const double left = static_cast<double>(x) + judge[0], top = static_cast<double>(y) + judge[1];
    const double right = static_cast<double>(x) + judge[2], bottom = static_cast<double>(y) + judge[3];
    // Not std::clamp: a corrupt file may store left > right.
    const double nearX = std::max(left, std::min(worldX, right)), nearY = std::max(top, std::min(worldY, bottom));
    // Standing inside the footprint: compare with its centre instead.
    if (nearX == worldX && nearY == worldY) return worldX + worldY < (left + right + top + bottom) / 2;
    return worldX + worldY < nearX + nearY;
}

bool MapObject::inFront(double worldX, double worldY) const {
    return (worldX > x + judge[2] || worldY > y + judge[3]) && !(worldX < x + judge[0] || worldY < y + judge[1]);
}

bool MapObject::surelyHides(double worldX, double worldY) const {
    return (worldX < x + judge[0] || worldY < y + judge[1]) && !(worldX > x + judge[2] || worldY > y + judge[3]);
}

ObjectLayer ObjectLayer::parse(std::span<const std::uint8_t> data) {
    BinaryReader in(data);
    const std::string_view magic = in.chars(16);
    if (!magic.starts_with("RPGSCRN_OBJv")) throw FormatError("not an object file");
    const int version = magic[14] - '0';
    const std::int32_t count = in.i32();
    if (count < 0 || static_cast<std::size_t>(count) > in.remaining() / 36) throw FormatError("bad object count");
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

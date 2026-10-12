#include "logic/explored_map.h"

#include <algorithm>
#include <charconv>
#include <format>

#include "formats/binary_reader.h"

namespace sf {

void ExploredMap::reveal(int x, int y, int w, int h) {
    const int x0 = std::max(0, x), y0 = std::max(0, y);
    const int x1 = std::min(width, x + w), y1 = std::min(height, y + h);
    for (int row = y0; row < y1; ++row) {
        auto* line = &seen[static_cast<std::size_t>(row) * static_cast<std::size_t>(width)];
        std::fill(line + x0, line + std::max(x0, x1), std::uint8_t{1});
    }
}

std::string ExploredMap::encode() const {
    std::string out = std::format("{},{}", width, height);
    std::uint8_t current = 0;
    std::size_t run = 0;
    for (const std::uint8_t s : seen) {
        if (s == current) {
            ++run;
            continue;
        }
        out += std::format(",{}", run);
        current = s;
        run = 1;
    }
    out += std::format(",{}", run);
    return out;
}

ExploredMap ExploredMap::decode(std::string_view text) {
    std::vector<long long> v;
    while (!text.empty()) {
        const auto comma = text.find(',');
        const auto part = text.substr(0, comma);
        long long n = 0;
        const auto [end, ec] = std::from_chars(part.data(), part.data() + part.size(), n);
        if (ec != std::errc() || end != part.data() + part.size() || n < 0) throw FormatError(std::format("bad explored map number '{}'", part));
        v.push_back(n);
        if (comma == std::string_view::npos) break;
        text.remove_prefix(comma + 1);
    }
    if (v.size() < 2 || v[0] > 10000 || v[1] > 10000) throw FormatError("bad explored map size");
    ExploredMap m(static_cast<int>(v[0]), static_cast<int>(v[1]));
    std::size_t at = 0;
    std::uint8_t value = 0;
    for (std::size_t i = 2; i < v.size(); ++i) {
        const auto run = static_cast<std::size_t>(v[i]);
        if (run > m.seen.size() - at) throw FormatError("explored map runs past its end");
        std::fill_n(m.seen.begin() + static_cast<std::ptrdiff_t>(at), run, value);
        at += run;
        value ^= 1;
    }
    if (at != m.seen.size()) throw FormatError("explored map runs stop short");
    return m;
}

}  // namespace sf

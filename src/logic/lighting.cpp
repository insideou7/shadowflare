#include "logic/lighting.h"

#include <algorithm>
#include <cmath>

namespace sf {

namespace lighting {

void add(LightMap& screen, const LightMap& light, int cx, int cy) {
    const int left = cx - light.width / 2, top = cy - light.height / 2;
    const int x0 = std::max(0, -left), y0 = std::max(0, -top);
    const int x1 = std::min(light.width, screen.width - left), y1 = std::min(light.height, screen.height - top);
    for (int y = y0; y < y1; ++y) {
        const std::uint8_t* src = &light.level[static_cast<std::size_t>(y) * static_cast<std::size_t>(light.width)];
        std::uint8_t* dst = &screen.level[static_cast<std::size_t>(top + y) * static_cast<std::size_t>(screen.width) + static_cast<std::size_t>(left)];
        for (int x = x0; x < x1; ++x) {
            if (src[x] == 0) continue;
            dst[x] = static_cast<std::uint8_t>(std::min(255, dst[x] + src[x]));
        }
    }
}

std::array<std::uint8_t, 256> levels(int areaLight) {
    std::array<std::uint8_t, 256> out{};
    const int over = areaLight - 1000;
    for (int i = 0; i < 256; ++i) {
        const int v = over < 1 ? i + i * over / 1000 : i + (255 - i) * over / 1000;
        out[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(std::clamp(v, 0, 255));
    }
    return out;
}

LightMap ellipse(int w, int h) {
    LightMap m{w, h, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * static_cast<std::size_t>(h))};
    const double rx = w / 2.0, ry = h / 2.0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const double d = std::hypot((x + 0.5 - rx) / rx, (y + 0.5 - ry) / ry);
            m.level[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] =
                static_cast<std::uint8_t>(std::clamp(std::lround(255 * (1 - d)), 0L, 255L));
        }
    }
    return m;
}

}  // namespace lighting

}  // namespace sf

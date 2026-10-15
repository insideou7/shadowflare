#include "formats/ground_skin.h"

#include <algorithm>
#include <cmath>

namespace sf {

namespace {

std::uint32_t blend(std::uint32_t under, std::uint32_t over) {
    const std::uint32_t a = over >> 24;
    if (a == 255) return over;
    if (a == 0) return under;
    auto mix = [&](int shift) {
        const std::uint32_t o = (over >> shift) & 255, u = (under >> shift) & 255;
        return ((o * a + u * (255 - a)) / 255) << shift;
    };
    const std::uint32_t outA = std::min<std::uint32_t>(255, a + ((under >> 24) * (255 - a)) / 255);
    return (outA << 24) | mix(16) | mix(8) | mix(0);
}

// How much of each material goes where the smoothed original colour is
// (r, g, b); returns their total.
float materialWeights(const std::vector<GroundRule::Entry>& entries, float r, float g, float b, std::vector<float>& weights) {
    // How sharply a pixel goes over from one material to the next, in
    // squared colour distance.
    constexpr float kSoftness = 40.0f;
    weights.resize(entries.size());
    float best = 1e30f;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const float dr = r - entries[i].colour.r, dg = g - entries[i].colour.g, db = b - entries[i].colour.b;
        weights[i] = dr * dr + dg * dg + db * db;
        best = std::min(best, weights[i]);
    }
    // Materials with under 1% of the weight of the nearest are left out.
    float total = 0;
    for (auto& wgt : weights) {
        wgt = std::exp((best - wgt) / kSoftness);
        if (wgt < 0.01f) wgt = 0;
        total += wgt;
    }
    return total;
}

}  // namespace

MaterialTexture buildMaterialTexture(const SpriteBank& tiles, int first, int period, int halfWidth, int halfHeight) {
    MaterialTexture out;
    if (period < 1 || halfWidth < 1 || halfHeight < 1 || first < 0 ||
        static_cast<std::size_t>(first + period * period) > tiles.patterns.size()) {
        return out;
    }
    std::vector<SpriteBank::Picture> pictures;
    for (int i = 0; i < period * period; ++i) {
        pictures.push_back(tiles.render(first + i, false));
        if (pictures.back().argb.empty()) return out;
    }
    out.width = 2 * halfWidth * period;
    out.height = 2 * halfHeight * period;
    out.argb.assign(static_cast<std::size_t>(out.width) * out.height, 0);
    // Tile (tx, ty) is centred at ((tx - ty) * halfWidth, (tx + ty) * halfHeight).
    // One repeat of the rectangle holds two repeats of the lattice, hence
    // 2 * period rows. Drawn back to front, as the map draws them.
    for (int sum = 0; sum < 3 * period; ++sum) {
        for (int tx = 0; tx < period; ++tx) {
            const int ty = sum - tx;
            if (ty < 0 || ty >= 2 * period) continue;
            const auto& pic = pictures[static_cast<std::size_t>((ty % period) * period + tx)];
            const int cx = (tx - ty) * halfWidth, cy = (tx + ty) * halfHeight;
            for (int y = 0; y < pic.height; ++y) {
                for (int x = 0; x < pic.width; ++x) {
                    const std::uint32_t c = pic.argb[static_cast<std::size_t>(y) * pic.width + x];
                    if (!(c >> 24)) continue;
                    int px = (cx + pic.x + x) % out.width, py = (cy + pic.y + y) % out.height;
                    if (px < 0) px += out.width;
                    if (py < 0) py += out.height;
                    auto& dst = out.argb[static_cast<std::size_t>(py) * out.width + px];
                    dst = blend(dst, c);
                }
            }
        }
    }
    for (auto& c : out.argb) c |= 0xff000000u;  // the ground is opaque
    return out;
}

MaterialTexture tinted(const MaterialTexture& texture, Rgb target) {
    MaterialTexture out = texture;
    if (texture.argb.empty()) return out;
    double sum[3] = {0, 0, 0};
    for (const std::uint32_t c : texture.argb) {
        for (int ch = 0; ch < 3; ++ch) sum[ch] += (c >> (16 - 8 * ch)) & 255;
    }
    const double want[3] = {static_cast<double>(target.r), static_cast<double>(target.g), static_cast<double>(target.b)};
    double gain[3];
    for (int ch = 0; ch < 3; ++ch) {
        const double mean = sum[ch] / static_cast<double>(texture.argb.size());
        gain[ch] = std::clamp(mean > 0.5 ? want[ch] / mean : 1.0, 0.6, 1.6);
    }
    for (std::uint32_t& c : out.argb) {
        std::uint32_t result = c & 0xff000000u;
        for (int ch = 0; ch < 3; ++ch) {
            const int shift = 16 - 8 * ch;
            const double v = std::min(255.0, ((c >> shift) & 255) * gain[ch] + 0.5);
            result |= static_cast<std::uint32_t>(v) << shift;
        }
        c = result;
    }
    return out;
}

std::vector<std::uint32_t> paintGround(int x0, int y0, int w, int h, int margin, const std::vector<std::uint32_t>& original,
                                       const std::vector<std::int16_t>& rule, const std::vector<GroundRule>& rules) {
    const int fw = w + 2 * margin, fh = h + 2 * margin;
    std::vector<std::uint32_t> out(static_cast<std::size_t>(w) * h, 0xff000000u);
    if (original.size() != static_cast<std::size_t>(fw) * fh || rule.size() != original.size()) return out;

    // Box blur of the original colours, rows then columns, as running sums.
    std::vector<float> rows(original.size() * 3), smooth(static_cast<std::size_t>(w) * h * 3);
    const int r = margin;
    for (int y = 0; y < fh; ++y) {
        for (int ch = 0; ch < 3; ++ch) {
            const int shift = 16 - 8 * ch;
            float sum = 0;
            for (int x = 0; x < std::min(fw, 2 * r + 1); ++x) sum += static_cast<float>((original[static_cast<std::size_t>(y) * fw + x] >> shift) & 255);
            for (int x = r; x < fw - r; ++x) {
                rows[(static_cast<std::size_t>(y) * fw + x) * 3 + ch] = sum / static_cast<float>(2 * r + 1);
                if (x + r + 1 < fw) {
                    sum += static_cast<float>((original[static_cast<std::size_t>(y) * fw + x + r + 1] >> shift) & 255);
                    sum -= static_cast<float>((original[static_cast<std::size_t>(y) * fw + x - r] >> shift) & 255);
                }
            }
        }
    }
    for (int x = 0; x < w; ++x) {
        for (int ch = 0; ch < 3; ++ch) {
            float sum = 0;
            auto at = [&](int y) { return rows[(static_cast<std::size_t>(y) * fw + x + r) * 3 + ch]; };
            for (int y = 0; y < 2 * r + 1; ++y) sum += at(y);
            for (int y = 0; y < h; ++y) {
                smooth[(static_cast<std::size_t>(y) * w + x) * 3 + ch] = sum / static_cast<float>(2 * r + 1);
                if (y + 2 * r + 1 < fh) sum += at(y + 2 * r + 1) - at(y);
            }
        }
    }

    std::vector<float> weights;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t inner = static_cast<std::size_t>(y + margin) * fw + x + margin;
            std::uint32_t& dst = out[static_cast<std::size_t>(y) * w + x];
            const int index = rule[inner];
            if (index < 0 || static_cast<std::size_t>(index) >= rules.size() || rules[index].entries.empty()) {
                dst = original[inner] | 0xff000000u;
                continue;
            }
            const auto& entries = rules[index].entries;
            const int mx = x0 + x, my = y0 + y;
            if (entries.size() == 1) {
                dst = entries[0].texture ? entries[0].texture->at(mx, my) : 0xff000000u;
                continue;
            }
            const float* c = &smooth[(static_cast<std::size_t>(y) * w + x) * 3];
            const float total = materialWeights(entries, c[0], c[1], c[2], weights);
            float rgb[3] = {0, 0, 0};
            for (std::size_t i = 0; i < entries.size(); ++i) {
                if (weights[i] == 0) continue;
                const std::uint32_t t = entries[i].texture ? entries[i].texture->at(mx, my) : 0xff000000u;
                for (int ch = 0; ch < 3; ++ch) rgb[ch] += weights[i] / total * static_cast<float>((t >> (16 - 8 * ch)) & 255);
            }
            dst = 0xff000000u | (static_cast<std::uint32_t>(std::min(255.0f, rgb[0])) << 16) |
                  (static_cast<std::uint32_t>(std::min(255.0f, rgb[1])) << 8) | static_cast<std::uint32_t>(std::min(255.0f, rgb[2]));
        }
    }
    return out;
}

Rgb averageColour(const MaterialTexture& texture) {
    if (texture.argb.empty()) return {};
    double sum[3] = {0, 0, 0};
    for (const std::uint32_t c : texture.argb) {
        for (int ch = 0; ch < 3; ++ch) sum[ch] += (c >> (16 - 8 * ch)) & 255;
    }
    const double n = static_cast<double>(texture.argb.size());
    return {static_cast<std::uint8_t>(sum[0] / n + 0.5), static_cast<std::uint8_t>(sum[1] / n + 0.5),
            static_cast<std::uint8_t>(sum[2] / n + 0.5)};
}

Rgb averagePainted(const GroundRule& rule, float r, float g, float b, std::vector<float>& weights) {
    const auto& entries = rule.entries;
    if (entries.empty()) return {};
    if (entries.size() == 1) return entries[0].average;
    const float total = materialWeights(entries, r, g, b, weights);
    float rgb[3] = {0, 0, 0};
    for (std::size_t i = 0; i < entries.size(); ++i) {
        rgb[0] += weights[i] / total * entries[i].average.r;
        rgb[1] += weights[i] / total * entries[i].average.g;
        rgb[2] += weights[i] / total * entries[i].average.b;
    }
    auto channel = [](float v) { return static_cast<std::uint8_t>(std::min(255.0f, v)); };
    return {channel(rgb[0]), channel(rgb[1]), channel(rgb[2])};
}

}  // namespace sf

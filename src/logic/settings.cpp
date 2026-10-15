#include "logic/settings.h"

#include <algorithm>
#include <charconv>
#include <format>

namespace sf {

namespace {

// Click Range sizes in pixels either side of the cursor. The original's
// own sizes are not decoded yet; these are ours, with NORM about a body.
constexpr std::array<int, Settings::kClickSizes> kReach{12, 20, 32, 44, 60};
constexpr int kNoRangeReach = 8;  // Click Range OFF: little more than the cursor

constexpr const char* kTargetNames[Settings::kTargetCount] = {"enemy", "item", "object", "person", "companion"};

bool number(std::string_view text, int& out) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    return error == std::errc{} && end == text.data() + text.size();
}

}  // namespace

int Settings::clickReach() const {
    return clickRange ? kReach[static_cast<std::size_t>(std::clamp(clickSize, 0, kClickSizes - 1))] : kNoRangeReach;
}

void Settings::putFirst(int target) {
    const auto it = std::ranges::find(clickPriority, target);
    if (it != clickPriority.end()) std::rotate(clickPriority.begin(), it, it + 1);
}

int Settings::rank(int target) const {
    return static_cast<int>(std::ranges::find(clickPriority, target) - clickPriority.begin());
}

int Settings::clampXpRate(int percent) {
    int rate = kXpRates.front();
    for (const int r : kXpRates) {
        if (r <= percent) rate = r;
    }
    return rate;
}

int Settings::clampLuck(int percent) {
    int luck = kLuckRates.front();
    for (const int r : kLuckRates) {
        if (r <= percent) luck = r;
    }
    return luck;
}

int Settings::clampGoldRate(int percent) {
    int rate = kGoldRates.front();
    for (const int r : kGoldRates) {
        if (r <= percent) rate = r;
    }
    return rate;
}

std::string Settings::write() const {
    std::string out = "# ShadowFlare open edition options\n";
    out += std::format("fullscreen={}\n", fullScreen ? 1 : 0);
    out += std::format("see_through_player={}\n", seeThroughPlayer ? 1 : 0);
    out += std::format("see_through_shadows={}\n", seeThroughShadows ? 1 : 0);
    out += std::format("darkness={}\n", darkness ? 1 : 0);
    out += std::format("save_image_at_end={}\n", saveImageAtEnd ? 1 : 0);
    out += std::format("click_range={}\n", clickRange ? 1 : 0);
    out += std::format("click_size={}\n", clickSize);
    out += "click_priority=";
    for (std::size_t i = 0; i < clickPriority.size(); ++i) {
        out += std::format("{}{}", i ? "," : "", kTargetNames[clickPriority[i]]);
    }
    out += "\n";
    out += std::format("effect_volume={}\n", effectVolume);
    out += std::format("music_volume={}\n", musicVolume);
    out += std::format("original={}\n", originalMode ? 1 : 0);
    out += std::format("our_art={}\n", ourArt ? 1 : 0);
    out += std::format("xp_rate={}\n", xpRate / 100);
    out += std::format("luck={}\n", luck / 100);
    out += std::format("gold_rate={}\n", goldRate / 100);
    out += std::format("show_fps={}\n", showFps ? 1 : 0);
    out += std::format("zoom={}\n", zoom);
    out += std::format("item_names={}\n", itemNames ? 1 : 0);
    return out;
}

Settings Settings::parse(std::string_view text) {
    Settings s;
    while (!text.empty()) {
        const auto newline = text.find('\n');
        std::string_view line = text.substr(0, newline);
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        const auto key = line.substr(0, equals);
        const auto value = line.substr(equals + 1);
        if (key == "click_priority") {
            // A list of all five, each once; anything else keeps the default.
            std::array<int, kTargetCount> order{};
            std::array<bool, kTargetCount> seen{};
            std::size_t count = 0;
            bool ok = true;
            for (std::string_view rest = value; ok && !rest.empty();) {
                const auto comma = rest.find(',');
                const auto name = rest.substr(0, comma);
                rest.remove_prefix(comma == std::string_view::npos ? rest.size() : comma + 1);
                const auto found = std::ranges::find(kTargetNames, name);
                const auto t = static_cast<std::size_t>(found - std::begin(kTargetNames));
                ok = t < kTargetCount && !seen[t] && count < order.size();
                if (ok) {
                    seen[t] = true;
                    order[count++] = static_cast<int>(t);
                }
            }
            if (ok && count == order.size()) s.clickPriority = order;
            continue;
        }
        if (key == "xp_rate") {
            // Written as a multiplier, such as 2; older files have 1.5 and so on.
            double rate = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), rate);
            if (error == std::errc{} && end == value.data() + value.size()) s.xpRate = clampXpRate(static_cast<int>(rate * 100 + 0.5));
            continue;
        }
        int v = 0;
        if (!number(value, v)) continue;
        if (key == "fullscreen") s.fullScreen = v != 0;
        else if (key == "see_through_player") s.seeThroughPlayer = v != 0;
        else if (key == "see_through_shadows") s.seeThroughShadows = v != 0;
        else if (key == "darkness") s.darkness = v != 0;
        else if (key == "save_image_at_end") s.saveImageAtEnd = v != 0;
        else if (key == "click_range") s.clickRange = v != 0;
        else if (key == "luck") s.luck = clampLuck(std::clamp(v, 0, 1000) * 100);
        else if (key == "gold_rate") s.goldRate = clampGoldRate(std::clamp(v, 0, 1000) * 100);
        else if (key == "click_size") s.clickSize = std::clamp(v, 0, kClickSizes - 1);
        else if (key == "effect_volume") s.effectVolume = std::clamp(v, 0, kMaxVolume);
        else if (key == "music_volume") s.musicVolume = std::clamp(v, 0, kMaxVolume);
        else if (key == "original") s.originalMode = v != 0;
        else if (key == "our_art") s.ourArt = v != 0;
        else if (key == "show_fps") s.showFps = v != 0;
        else if (key == "zoom") s.zoom = std::clamp(v, kMinZoom, kMaxZoom);
        else if (key == "item_names") s.itemNames = v != 0;
    }
    return s;
}

}  // namespace sf

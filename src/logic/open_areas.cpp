#include "logic/open_areas.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iterator>
#include <cstdio>
#include <optional>

#include "formats/map_layers.h"
#include "logic/save_game.h"

namespace sf {
namespace {

constexpr OpenArea kAreas[] = {
#include "logic/open_area_table.inc"
};

// The number of a "Scenario/<8 digits>" folder.
std::optional<int> scenarioNumber(std::string_view dir) {
    constexpr std::string_view kPrefix = "Scenario/";
    if (!dir.starts_with(kPrefix)) return std::nullopt;
    dir.remove_prefix(kPrefix.size());
    int n = 0;
    const auto [end, error] = std::from_chars(dir.data(), dir.data() + dir.size(), n);
    if (error != std::errc{} || end != dir.data() + dir.size()) return std::nullopt;
    return n;
}

std::string scenarioDir(int number) {
    char dir[32];
    std::snprintf(dir, sizeof dir, "Scenario/%08d", number);
    return dir;
}

// The area and which way it goes: true from the original's to ours.
struct Move {
    const OpenArea* area = nullptr;
    bool toOpen = false;
};

std::optional<Move> moveOf(std::string_view dir) {
    const auto n = scenarioNumber(dir);
    if (!n) return std::nullopt;
    const bool open = *n >= kOpenScenarioOffset;
    const OpenArea* area = openAreaOf(open ? *n - kOpenScenarioOffset : *n);
    if (!area) return std::nullopt;
    return Move{area, !open};
}

// The area map's mask of one area in the other's: both are 10 screen pixels
// to a cell from the ground's top left corner. The original's ground starts
// at (0, 0) on screen; ours is a diamond whose far left corner is (0, height).
ExploredMap movedMask(const ExploredMap& from, const Move& m) {
    const OpenArea& a = *m.area;
    int left, top, right, bottom, unused;
    worldToScreen(0, a.height, left, unused);
    worldToScreen(a.width, 0, right, unused);
    worldToScreen(0, 0, unused, top);
    worldToScreen(a.width, a.height, unused, bottom);
    int cornerX, cornerY;  // our map's (0, 0) on the original's screen
    worldToScreen(a.x0, a.y0, cornerX, cornerY);
    // Our ground's top left corner, in the original's mask cells.
    const double shiftX = (cornerX + left) / static_cast<double>(kMapScale);
    const double shiftY = (cornerY + top) / static_cast<double>(kMapScale);
    const int dx = static_cast<int>(std::lround(m.toOpen ? -shiftX : shiftX));
    const int dy = static_cast<int>(std::lround(m.toOpen ? -shiftY : shiftY));
    ExploredMap to = m.toOpen ? ExploredMap((right - left) / kMapScale, (bottom - top) / kMapScale)
                              : ExploredMap(a.originalWidth / kMapScale, a.originalHeight / kMapScale);
    for (int y = 0; y < from.height; ++y) {
        for (int x = 0; x < from.width; ++x) {
            if (from.seenAt(x, y)) to.reveal(x + dx, y + dy, 1, 1);
        }
    }
    return to;
}

}  // namespace

const OpenArea* openAreaOf(int original) {
    const auto* found = std::ranges::find(kAreas, original, &OpenArea::original);
    return found == std::end(kAreas) ? nullptr : found;
}

bool moveSaveToOtherData(SaveGame& save) {
    const auto hero = moveOf(save.scenario);
    if (!hero) return false;
    const OpenArea& a = *hero->area;
    const int sign = hero->toOpen ? -1 : 1;
    save.scenario = scenarioDir(hero->toOpen ? a.original + kOpenScenarioOffset : a.original);
    save.x += sign * a.x0;
    save.y += sign * a.y0;
    // The map names are the same in both (first_area.py names ours after
    // the original's). Areas seen in the other data's areas only are left
    // as they are: they do no harm and come back if the save goes back.
    std::map<std::string, ExploredMap> explored;
    for (auto& [dir, mask] : save.explored) {
        const auto m = moveOf(dir);
        if (m && m->toOpen == hero->toOpen) {
            explored[scenarioDir(m->toOpen ? m->area->original + kOpenScenarioOffset : m->area->original)] = movedMask(mask, *m);
        } else {
            explored.emplace(dir, std::move(mask));
        }
    }
    save.explored = std::move(explored);
    return true;
}

}  // namespace sf

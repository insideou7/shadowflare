#include "formats/missions.h"

#include "formats/table_data.h"

namespace sf {

Missions Missions::fromTables(const GameTables& tables) {
    Missions m;
    const auto* titles = tables.find(41);
    if (!titles || titles->cols < 1) return m;
    for (int row = 0; row < titles->rows; ++row) {
        Mission mission;
        mission.title = titles->string(row, 0);
        if (const auto* text = tables.find(700 + row); text && text->cols >= 1) {
            for (int line = 0; line < text->rows; ++line) mission.lines.push_back(text->string(line, 0));
        }
        m.list.push_back(std::move(mission));
    }
    return m;
}

}  // namespace sf

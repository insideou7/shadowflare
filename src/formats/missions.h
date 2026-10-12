#pragma once

#include <string>
#include <vector>

namespace sf {

struct GameTables;

// The missions' words: what the mission list and the HUD show for each
// mission number (the QUEST flags SETQUESTFLAG sets). The original keeps
// the titles in Table.Tbd table 41, one row per mission, and each
// mission's description in table 700 + its number, one line per row
// (ShadowFlare.exe 0x40cea0). The open content has missions.txt.
struct Missions {
    struct Mission {
        std::string title;
        std::vector<std::string> lines;
    };
    std::vector<Mission> list;  // by mission number

    const Mission* find(int number) const {
        return number >= 0 && number < static_cast<int>(list.size()) ? &list[static_cast<std::size_t>(number)] : nullptr;
    }
    int count() const { return static_cast<int>(list.size()); }

    static Missions fromTables(const GameTables& tables);
};

}  // namespace sf

#pragma once

#include <string>
#include <vector>

namespace sf {

struct GameTables;

// The warp gates: what the GATE window lists for each gate number (the
// WARP flags) and where picking it leads. The original keeps them in
// Table.Tbd table 40, one row per gate: the name, then the scenario and
// the entry point (ShadowFlare.exe 0x40c950). The open content has
// gates.txt.
struct WarpGates {
    struct Gate {
        std::string name;
        int scenario = 0;
        int entry = 0;
    };
    std::vector<Gate> list;  // by gate number

    const Gate* find(int number) const {
        return number >= 0 && number < static_cast<int>(list.size()) ? &list[static_cast<std::size_t>(number)] : nullptr;
    }
    int count() const { return static_cast<int>(list.size()); }

    static WarpGates fromTables(const GameTables& tables);
};

}  // namespace sf

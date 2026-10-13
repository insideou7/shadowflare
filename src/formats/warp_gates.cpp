#include "formats/warp_gates.h"

#include "formats/table_data.h"

namespace sf {

WarpGates WarpGates::fromTables(const GameTables& tables) {
    WarpGates g;
    const auto* table = tables.find(40);
    if (!table || table->cols < 3) return g;
    for (int row = 0; row < table->rows; ++row) {
        g.list.push_back({table->string(row, 0), table->value(row, 1), table->value(row, 2)});
    }
    return g;
}

}  // namespace sf

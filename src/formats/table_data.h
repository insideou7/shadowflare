#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace sf {

// System/Game/Parameter/Table.Tbd: numbered tables of integers, each cell
// optionally with a string. From RKC_RPG_TABLE::ReadBinaryFile.
//
// Known tables: 40 area names, 41 quest titles, 60 partner names,
// 600-621 spell descriptions, 700-747 quest descriptions. The numeric
// tables (experience curves, shop and drop lists...) are not identified yet.
struct GameTables {
    struct Table {
        int rows = 0, cols = 0;
        std::vector<std::int32_t> values;  // row-major
        std::vector<std::string> strings;  // row-major, empty when unused

        std::int32_t value(int row, int col) const { return values[static_cast<std::size_t>(row) * cols + col]; }
        const std::string& string(int row, int col) const { return strings[static_cast<std::size_t>(row) * cols + col]; }
    };

    std::map<int, Table> tables;  // by table number

    const Table* find(int number) const {
        auto it = tables.find(number);
        return it == tables.end() ? nullptr : &it->second;
    }

    static GameTables parse(std::span<const std::uint8_t> data);
};

}  // namespace sf

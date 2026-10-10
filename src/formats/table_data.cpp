#include "formats/table_data.h"

#include "formats/binary_reader.h"
#include "formats/rclib.h"

namespace sf {

GameTables GameTables::parse(std::span<const std::uint8_t> data) {
    BinaryReader file(data);
    if (!file.chars(16).starts_with("TABLE DATA V000")) throw FormatError("not a table file");
    std::vector<std::uint8_t> body;
    if (file.i32() != 0) {
        body = rclib::readBlock(file);
    } else {
        const auto size = file.i32();
        if (size < 0) throw FormatError("bad table size");
        auto raw = file.bytes(static_cast<std::size_t>(size));
        body.assign(raw.begin(), raw.end());
    }

    BinaryReader in(body);
    GameTables result;
    const std::int32_t count = in.i32();
    if (count < 0) throw FormatError("bad table count");
    for (std::int32_t t = 0; t < count; ++t) {
        const int number = in.i32();
        Table table;
        table.rows = in.i32();
        table.cols = in.i32();
        if (table.rows < 0 || table.cols < 0) throw FormatError("bad table shape");
        const std::size_t cells = static_cast<std::size_t>(table.rows) * table.cols;
        if (cells > in.remaining() / 4) throw FormatError("table larger than its file");
        table.values.resize(cells);
        for (auto& v : table.values) v = in.i32();
        // Strings are stored with every byte inverted.
        table.strings.resize(cells);
        for (auto& s : table.strings) {
            const std::int32_t length = in.i32();
            if (length < 0) throw FormatError("bad table string");
            for (std::uint8_t b : in.bytes(static_cast<std::size_t>(length))) s.push_back(static_cast<char>(~b));
        }
        result.tables[number] = std::move(table);
    }
    return result;
}

}  // namespace sf

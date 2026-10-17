// Fuzz target for the data file readers: original files from the player's
// own copy and the open pack's text files are both read by these, and in
// the browser the player picks the folder. The first byte chooses the
// reader, the rest is the file. A reader may refuse bad data only with
// FormatError (what the game catches); anything else, or a sanitizer
// report, is a bug. See docs/fuzzing.md.
#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <span>

#include "formats/ai_control.h"
#include "formats/animation.h"
#include "formats/binary_reader.h"
#include "formats/item_data.h"
#include "formats/map_layers.h"
#include "formats/open_content.h"
#include "formats/rclib.h"
#include "formats/scenario.h"
#include "formats/scenario_script.h"
#include "formats/sprite_bank.h"
#include "formats/table_data.h"
#include "formats/voice_bank.h"

namespace {

void readWith(std::uint8_t reader, std::span<const std::uint8_t> data) {
    switch (reader % 18) {
    case 0: sf::AiControl::parse(data); break;
    case 1: sf::AnimationSet::parse(data); break;
    case 2: {
        std::array<std::uint8_t, 256> identity{};
        std::iota(identity.begin(), identity.end(), std::uint8_t{0});
        sf::ItemTable::parse(data, identity);
        break;
    }
    case 3: sf::ItemTable::parseRecords(data); break;
    case 4: sf::GroundLayer::parse(data); break;
    case 5: sf::ObjectLayer::parse(data); break;
    case 6: sf::ScenarioControl::parse(data); break;
    case 7: sf::ScenarioScript::parse(data); break;
    case 8: sf::SpriteBank::parse(data); break;
    case 9: sf::GameTables::parse(data); break;
    case 10: sf::VoiceBank::parse(data); break;
    case 11: {
        // The unpacked size comes from the archive's directory; keep it to
        // what the archive itself could hold so a test is not just a big
        // allocation.
        if (data.size() < 2) return;
        const std::size_t size = (std::size_t{data[0]} << 8 | data[1]) * 16;
        sf::rclib::lzDecode(data.subspan(2), size);
        break;
    }
    case 12: sf::open::decodeImage(data); break;
    case 13: sf::open::parseSheet(data); break;
    case 14: sf::open::parseAnimation(data); break;
    case 15: sf::open::parseMap(data); break;
    case 16: sf::open::parseScenario(data); break;
    case 17: sf::open::parseItems(data); break;
    }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* bytes, std::size_t size) {
    if (size == 0) return 0;
    try {
        readWith(bytes[0], std::span(bytes + 1, size - 1));
    } catch (const sf::FormatError&) {
    }
    return 0;
}

#include "game/map.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "core/log.h"
#include "formats/binary_reader.h"
#include "formats/open_content.h"
#include "game/content.h"
#include "platform/filesystem.h"

namespace sf {

namespace {

std::vector<std::string> parseList(const std::vector<std::uint8_t>& raw) {
    std::vector<std::string> lines;
    std::string line;
    for (std::uint8_t c : raw) {
        if (c == '\n') {
            lines.push_back(line);
            line.clear();
        } else if (c != '\r') {
            line.push_back(static_cast<char>(c));
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

bool endsWithIgnoreCase(const std::string& s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    return std::equal(suffix.begin(), suffix.end(), s.end() - suffix.size(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

}  // namespace

std::optional<Map> Map::load(const FileSystem& data, const std::string& name) {
    // An open content pack keeps the whole map in one text file.
    if (auto text = data.read("Map/" + name + ".txt")) {
        Map map;
        map.data_ = &data;
        map.name_ = name;
        map.open_ = true;
        try {
            auto parsed = open::parseMap(*text);
            map.bankFiles_ = std::move(parsed.banks);
            map.ground_ = std::move(parsed.ground);
            map.objects_ = std::move(parsed.objects);
        } catch (const FormatError& e) {
            log::error("Map {}: {}", name, e.what());
            return std::nullopt;
        }
        map.banks_.resize(map.bankFiles_.size());
        map.bankTried_.assign(map.bankFiles_.size(), false);
        log::info("Loaded map {}: {}x{} chips, {} objects, {} sheets", name, map.ground_.width, map.ground_.height,
                  map.objects_.objects.size(), map.bankFiles_.size());
        return map;
    }
    auto lst = data.read("Map/Pattern/" + name + ".lst");
    auto gnd = data.read("Map/Ground/" + name + ".gnd");
    auto obl = data.read("Map/Object/" + name + ".obl");
    if (!lst || !gnd || !obl) {
        log::error("Map {} is missing files (lst {}, gnd {}, obl {})", name, lst.has_value(), gnd.has_value(),
                   obl.has_value());
        return std::nullopt;
    }
    Map map;
    map.data_ = &data;
    map.name_ = name;
    map.bankFiles_ = parseList(*lst);
    map.banks_.resize(map.bankFiles_.size());
    map.bankTried_.assign(map.bankFiles_.size(), false);
    try {
        map.ground_ = GroundLayer::parse(*gnd);
        map.objects_ = ObjectLayer::parse(*obl);
    } catch (const FormatError& e) {
        log::error("Map {}: {}", name, e.what());
        return std::nullopt;
    }
    log::info("Loaded map {}: {}x{} chips, {} objects, {} banks", name, map.ground_.width, map.ground_.height,
              map.objects_.objects.size(), map.bankFiles_.size());
    return map;
}

const SpriteBank* Map::bank(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= bankFiles_.size()) return nullptr;
    if (!bankTried_[index]) {
        bankTried_[index] = true;
        const std::string& file = bankFiles_[index];
        if (open_) {
            if (auto sheet = loadSheet(*data_, file)) banks_[index] = std::make_unique<SpriteBank>(std::move(*sheet));
        } else if (!file.empty() && file != "?") {
            if (auto raw = data_->read("Map/Pattern/" + file)) {
                try {
                    banks_[index] = std::make_unique<SpriteBank>(SpriteBank::parse(*raw));
                } catch (const FormatError& e) {
                    log::warn("{}: {}", file, e.what());
                }
            } else {
                log::warn("Missing sprite bank {}", file);
            }
        }
    }
    return banks_[index].get();
}

bool Map::isShadowBank(int index) const {
    return index >= 0 && static_cast<std::size_t>(index) < bankFiles_.size() &&
           endsWithIgnoreCase(bankFiles_[index], ".sdw");
}

}  // namespace sf

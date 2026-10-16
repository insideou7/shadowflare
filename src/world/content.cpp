#include "world/content.h"

#include "core/log.h"
#include "formats/binary_reader.h"
#include "formats/open_content.h"
#include "platform/filesystem.h"

namespace sf {

std::string folderOf(const std::string& path) {
    const auto slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::optional<SpriteBank> loadSheet(const FileSystem& data, const std::string& path, bool trim) {
    auto text = data.read(path);
    if (!text) {
        log::warn("Missing sprite sheet {}", path);
        return std::nullopt;
    }
    try {
        const auto sheet = open::parseSheet(*text);
        const std::string imagePath = folderOf(path) + sheet.image;
        auto png = data.read(imagePath);
        if (!png) {
            log::warn("{}: missing image {}", path, imagePath);
            return std::nullopt;
        }
        return open::makeSpriteBank(sheet, open::decodeImage(*png), trim);
    } catch (const FormatError& e) {
        log::warn("{}: {}", path, e.what());
        return std::nullopt;
    }
}

}  // namespace sf

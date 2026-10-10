#include "platform/filesystem.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace sf {

namespace {

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](unsigned char x, unsigned char y) {
        return std::tolower(x) == std::tolower(y);
    });
}

}  // namespace

DirectoryFileSystem::DirectoryFileSystem(std::filesystem::path root) : root_(std::move(root)) {}

// Walks the path one component at a time so "DATA/Map01.BIN" finds
// "data/map01.bin" on case-sensitive filesystems.
std::optional<std::filesystem::path> DirectoryFileSystem::resolve(std::string_view path) const {
    std::filesystem::path current = root_;
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find_first_of("/\\", start);
        if (end == std::string_view::npos) end = path.size();
        std::string_view part = path.substr(start, end - start);
        start = end + 1;
        if (part.empty() || part == ".") continue;
        if (part == "..") return std::nullopt;

        std::filesystem::path exact = current / std::string(part);
        std::error_code ec;
        if (std::filesystem::exists(exact, ec)) {
            current = std::move(exact);
            continue;
        }
        bool found = false;
        for (const auto& entry : std::filesystem::directory_iterator(current, ec)) {
            if (equalsIgnoreCase(entry.path().filename().string(), part)) {
                current = entry.path();
                found = true;
                break;
            }
        }
        if (!found) return std::nullopt;
    }
    return current;
}

bool DirectoryFileSystem::exists(std::string_view path) const {
    return resolve(path).has_value();
}

std::optional<std::vector<std::uint8_t>> DirectoryFileSystem::read(std::string_view path) const {
    auto resolved = resolve(path);
    if (!resolved) return std::nullopt;
    std::ifstream file(*resolved, std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;
    const auto size = file.tellg();
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), size);
    if (!file) return std::nullopt;
    return data;
}

std::vector<std::string> DirectoryFileSystem::list(std::string_view directory) const {
    std::vector<std::string> names;
    auto resolved = resolve(directory);
    if (!resolved) return names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(*resolved, ec)) {
        names.push_back(entry.path().filename().string());
    }
    std::ranges::sort(names);
    return names;
}

}  // namespace sf

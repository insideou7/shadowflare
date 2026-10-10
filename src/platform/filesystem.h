#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

// All game data goes through this interface so the engine never touches
// the OS directly. A browser build can supply its own implementation
// backed by files the player picked.
class FileSystem {
public:
    virtual ~FileSystem() = default;

    // Paths use '/' separators and are matched case-insensitively,
    // like the original game expects on Windows.
    virtual bool exists(std::string_view path) const = 0;
    virtual std::optional<std::vector<std::uint8_t>> read(std::string_view path) const = 0;
};

// Reads from the player's own copy of the game on disk.
class DirectoryFileSystem final : public FileSystem {
public:
    explicit DirectoryFileSystem(std::filesystem::path root);

    bool exists(std::string_view path) const override;
    std::optional<std::vector<std::uint8_t>> read(std::string_view path) const override;

    const std::filesystem::path& root() const { return root_; }

private:
    std::optional<std::filesystem::path> resolve(std::string_view path) const;

    std::filesystem::path root_;
};

}  // namespace sf

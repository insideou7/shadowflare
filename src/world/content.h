#pragma once

#include <optional>
#include <string>

#include "formats/sprite_bank.h"

namespace sf {

class FileSystem;

// Helpers for the open content pack (see docs/content.md).

// The folder part of a path, with its trailing '/', or empty.
std::string folderOf(const std::string& path);

// Loads a .sheet and the image it names as a sprite bank; logs and
// returns nothing when either is missing or broken. `trim` as for
// open::makeSpriteBank.
std::optional<SpriteBank> loadSheet(const FileSystem& data, const std::string& path, bool trim = false);

}  // namespace sf

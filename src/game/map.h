#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "formats/map_layers.h"
#include "formats/sprite_bank.h"

namespace sf {

class FileSystem;

// One playable area: Map/<name>.map plus its ground, object and
// pattern-list files, or Map/<name>.txt from an open content pack. Sprite
// banks are loaded on first use.
class Map {
public:
    static std::optional<Map> load(const FileSystem& data, const std::string& name);

    const std::string& name() const { return name_; }
    const GroundLayer& ground() const { return ground_; }
    const ObjectLayer& objects() const { return objects_; }

    // Returns null for unused list slots or banks that fail to load.
    const SpriteBank* bank(int index) const;

    // True when the list entry is a shadow bank (.sdw).
    bool isShadowBank(int index) const;

private:
    const FileSystem* data_ = nullptr;
    std::string name_;
    bool open_ = false;  // from an open content pack: banks are .sheet files
    std::vector<std::string> bankFiles_;
    mutable std::vector<std::unique_ptr<SpriteBank>> banks_;
    mutable std::vector<bool> bankTried_;
    GroundLayer ground_;
    ObjectLayer objects_;
};

}  // namespace sf

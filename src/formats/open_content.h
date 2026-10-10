#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "formats/ai_control.h"
#include "formats/animation.h"
#include "formats/map_layers.h"
#include "formats/scenario.h"
#include "formats/scenario_script.h"
#include "formats/sprite_bank.h"

// Readers for the open content pack: PNG images and small text files that
// stand in for the original game's data, so the engine can run without it.
// Each reader fills the same structures as the matching original format,
// so the rest of the engine does not care which one it was given. The
// formats are described in docs/content.md.
namespace sf::open {

// A decoded image, 0xAARRGGBB, top row first.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> argb;
};

// Decodes a PNG (or any format stb_image reads). Throws FormatError.
Image decodeImage(std::span<const std::uint8_t> data);

// A .sheet file: one image cut into frames, each with an anchor point.
struct Sheet {
    struct Frame {
        int x = 0, y = 0, width = 0, height = 0;
        int anchorX = 0, anchorY = 0;  // inside the frame; drawn at the sprite's position
    };
    std::string image;  // file name, relative to the .sheet
    std::vector<Frame> frames;
};
Sheet parseSheet(std::span<const std::uint8_t> text);

// One pattern per frame, each a single full-colour part.
SpriteBank makeSpriteBank(const Sheet& sheet, const Image& image);

// A .anim file: the charts of a character, and the sheet holding its frames.
struct Animation {
    std::string sheet;  // relative to the .anim
    AnimationSet set;
};
Animation parseAnimation(std::span<const std::uint8_t> text);

// Map/<name>.txt: ground chips, walkability and objects.
struct Map {
    std::vector<std::string> banks;  // .sheet paths, relative to the pack root
    GroundLayer ground;
    ObjectLayer objects;
};
Map parseMap(std::span<const std::uint8_t> text);

// Scenario/<number>/scenario.txt: what stands on a map and where its exits
// lead. Exits become ON JUDGE triggers running a CHANGESCENARIO sentence,
// the way the original's scripts do it.
struct Scenario {
    ScenarioControl control;
    ScenarioScript script;
};
Scenario parseScenario(std::span<const std::uint8_t> text);

// The monster behaviour lists, standing in for Control.aid.
AiControl parseAi(std::span<const std::uint8_t> text);

// content.txt at the pack root.
struct PackInfo {
    std::string name;
    std::string startMap;
    Projection projection{20, 10};
};
PackInfo parsePackInfo(std::span<const std::uint8_t> text);

}  // namespace sf::open

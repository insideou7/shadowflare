#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "formats/ai_control.h"
#include "formats/drop_tables.h"
#include "formats/animation.h"
#include "formats/item_data.h"
#include "formats/map_layers.h"
#include "formats/missions.h"
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
        std::string name;  // optional seventh field, used to look props up by name
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

// Scenario/<number>/script.txt: the scenario's events, written in the
// original's command names (see docs/content.md). Adds its people,
// objects, messages, sentences and triggers to a parsed scenario.txt.
// Throws FormatError.
void parseScript(std::span<const std::uint8_t> text, Scenario& scenario);

// items.txt: item definitions, standing in for Item.Ibn.
ItemTable parseItems(std::span<const std::uint8_t> text);

// shops.txt: the goods INITTRADE stocks a shop with, by list number,
// standing in for Table.Tbd tables 32 and 33.
struct ShopGoods {
    ItemDef::Class cls = ItemDef::Class::Weapon;
    std::int32_t code = 0;
    int count = 1;
};
using ShopLists = std::map<int, std::vector<ShopGoods>>;
ShopLists parseShops(std::span<const std::uint8_t> text);

// drops.txt: what monsters leave behind, by the drop list number in their
// enemy= line, standing in for Table.Tbd tables 30 and 31.
DropTables parseDrops(std::span<const std::uint8_t> text);

// missions.txt: each mission's title and description, standing in for
// Table.Tbd table 41 and tables 700 on.
Missions parseMissions(std::span<const std::uint8_t> text);

// The monster behaviour lists, standing in for Control.aid.
AiControl parseAi(std::span<const std::uint8_t> text);

// skin.txt: how to draw the original game's maps with a pack's art, so
// a player with the original game sees its maps in our art. See
// docs/content.md.
struct Skin {
    // A ground texture: `period` x `period` isometric tiles of one sheet,
    // starting at frame `first`, which repeat seamlessly.
    struct Material {
        std::string sheet;
        int first = 0;
        int period = 4;
        int tile = 320;  // world units per tile edge
    };
    // One of the materials an original ground bank is painted with, and
    // its typical colour there. "none" draws black.
    struct GroundMaterial {
        std::string material;
        Rgb colour;
    };
    // A prop from the props sheet, drawn with its anchor this many world
    // units from where the original piece stands.
    struct Piece {
        std::string prop;
        int dx = 0, dy = 0;
        bool mirror = false;  // flipped left to right ("<prop>!")
    };
    std::string props;  // the .sheet whose named frames [objects] uses
    std::map<std::string, Material> materials;
    // By lower-case bank file name, as in the maps' .lst files.
    std::map<std::string, std::vector<GroundMaterial>> ground;
    // Bank, then pattern: what to draw instead. No pieces = draw nothing.
    std::map<std::string, std::map<int, std::vector<Piece>>> objects;
};
Skin parseSkin(std::span<const std::uint8_t> text);

// content.txt at the pack root.
struct PackInfo {
    std::string name;
    std::string startMap;
    Projection projection{20, 10};
};
PackInfo parsePackInfo(std::span<const std::uint8_t> text);

}  // namespace sf::open

#pragma once

#include <SDL.h>

#include <array>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "formats/ground_skin.h"
#include "formats/open_content.h"
#include "game/character.h"

namespace sf {

class FileSystem;
class Map;
struct MapObject;

// Draws the original game's maps with an open content pack's art: its
// ground textures, props, characters and hero. The pack says how in its
// skin.txt (see docs/content.md); whatever it does not cover is drawn
// from the original files as before.
class Skin {
public:
    // Null when the pack has no usable skin.txt.
    static std::unique_ptr<Skin> load(std::unique_ptr<FileSystem> pack, SDL_Renderer* renderer);
    ~Skin();
    Skin(const Skin&) = delete;
    Skin& operator=(const Skin&) = delete;

    // The pack's version of a character ("Character/ENEMY/00000001/Animation",
    // "Player/Male/Animation00"), if it has one.
    std::optional<CharacterModel> character(const std::string& stem) const;

    // The pack's .sheet picture bank at a path stem ("System/Title/Pattern/
    // Title"), or null when it has none.
    const SpriteBank* sheet(const std::string& stem);

    // Our picture for an original item's bag picture (bank, pattern of
    // System/Game/Pattern/Item<bank>) or ground picture (folder, picture
    // of Character/ITEM/<folder>/Pattern), by the pack's item_skin.txt: a
    // null bank when the pack has not drawn it, and the original's is drawn.
    struct ItemPicture {
        const SpriteBank* bank = nullptr;
        int pattern = -1;
    };
    ItemPicture itemIcon(int bank, int pattern);
    ItemPicture itemGround(int folder, int picture);

    // Call when a new map is shown: forgets the last map's ground.
    void setMap(const Map* map);

    // Draws the ground in place of the original's chips.
    void renderGround(int camX, int camY);

    // A number that changes when the pack's skin.txt or the pictures it
    // draws the maps with change, to tell pictures made with them apart.
    std::uint64_t fingerprint() const;

    // A w x h picture of the ground, each pixel the average of a scale x
    // scale block from map pixel (left, top): what renderGround draws
    // there, near enough, without painting it all, which takes long.
    std::vector<std::uint32_t> groundOverview(int left, int top, int scale, int w, int h);

    // What to draw instead of an original map object: null when the
    // original is drawn, else the props (an empty list draws nothing). A
    // shadow (.sdw) piece goes with its .njp piece, since our props carry
    // their own shadows.
    const std::vector<open::Skin::Piece>* replacement(const MapObject& object) const;

    // Draws an object's replacement with its props anchored at the
    // original piece's footing.
    void renderObject(const MapObject& object, const std::vector<open::Skin::Piece>& pieces, int camX, int camY);

    // The same for a scenario object (a chest, door or lever) drawn with
    // pattern `pattern` of Character/OBJECT/<kind>/Pattern.njp; skin.txt
    // names that bank [objects object/<kind, 8 digits>]. Its props are
    // anchored on the object's own position, as the original's pictures
    // are, so a pack draws them to fit that anchor (tools/art/objects.py).
    const std::vector<open::Skin::Piece>* scenarioObject(int kind, int pattern) const;
    // `glow` (per mille) adds a brighter copy on top, as for the one under
    // the pointer, and `outline` rings it in that colour first.
    void renderScenarioObject(int worldX, int worldY, const std::vector<open::Skin::Piece>& pieces, int camX, int camY, int glow = 0,
                              const SDL_Color* outline = nullptr);

private:
    Skin() = default;
    const MaterialTexture* material(const std::string& name);
    SDL_Texture* groundChunk(int cx, int cy);
    const SpriteBank::Picture* chip(int bank, int pattern);
    // A chip's colours as running sums over its picture: entry (x, y) of a
    // (width + 1) x (height + 1) table adds up the pixels above and left of
    // it, so any rectangle's total takes four lookups.
    struct ChipSums {
        int width = 0, height = 0;
        std::vector<std::array<std::uint32_t, 3>> rgb;
    };
    const ChipSums* chipSums(int bank, int pattern);
    // Draws props with the original piece's foot at (footX, footY), in map pixels.
    void renderPieces(const std::vector<open::Skin::Piece>& pieces, int footX, int footY, int camX, int camY, int glow = 0,
                      const SDL_Color* outline = nullptr);
    // Where an original picture's foot is, from its anchor.
    static std::pair<int, int> footOf(const SpriteBank& bank, int pattern);
    SDL_Texture* propTexture(const std::string& prop, int& frame);
    SDL_Texture* propOutline(const std::string& prop);

    ItemPicture itemPicture(const std::map<std::pair<int, int>, std::pair<int, int>>& table, int a, int b,
                            const char* format);

    std::unique_ptr<FileSystem> pack_;
    SDL_Renderer* renderer_ = nullptr;
    std::map<std::pair<int, int>, std::pair<int, int>> itemIcons_, itemGrounds_;  // item_skin.txt
    open::Skin file_;
    std::unique_ptr<SpriteBank> props_;
    std::map<std::string, int> propFrames_;
    std::map<std::string, std::unique_ptr<SpriteBank>> sheets_;  // by path stem; null = missing
    std::map<std::string, SDL_Texture*> propTextures_;  // by prop name
    std::map<std::string, SDL_Texture*> propOutlines_;  // their outlines (kOutlineWidth wider each side)
    std::map<std::string, std::optional<MaterialTexture>> materials_;
    std::map<std::tuple<std::string, int, int, int>, MaterialTexture> tinted_;  // material, colour

    // The current map: a ground rule per .lst slot (-1 = none), the
    // original chips decoded on first use and the painted ground cached
    // in chunks, most recently used first.
    const Map* map_ = nullptr;
    std::vector<int> bankRule_;
    std::vector<std::string> bankName_;  // lower case, .njp for .sdw
    std::vector<GroundRule> rules_;
    std::map<std::pair<int, int>, SpriteBank::Picture> chips_;
    std::map<std::pair<int, int>, ChipSums> chipSums_;
    std::map<std::pair<int, int>, std::pair<int, int>> feet_;  // bank, pattern -> foot in the picture
    std::list<std::pair<std::pair<int, int>, SDL_Texture*>> chunks_;
};

}  // namespace sf

#include "game/skin.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "core/log.h"
#include "formats/binary_reader.h"
#include "formats/map_layers.h"
#include "game/content.h"
#include "game/map.h"
#include "platform/filesystem.h"

namespace sf {

namespace {

constexpr int kChunkWidth = 256;
constexpr int kChunkHeight = 192;
constexpr int kMaxChunks = 64;
constexpr int kSmoothing = 4;  // pixels of blur before matching colours

std::string lowerCase(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A shadow bank's pieces share their numbers with the bank they shade.
std::string shadedBank(std::string name) {
    name = lowerCase(std::move(name));
    if (name.size() > 4 && name.ends_with(".sdw")) name.replace(name.size() - 4, 4, ".njp");
    return name;
}

}  // namespace

std::unique_ptr<Skin> Skin::load(std::unique_ptr<FileSystem> pack, SDL_Renderer* renderer) {
    auto text = pack->read("skin.txt");
    if (!text) return nullptr;
    std::unique_ptr<Skin> skin(new Skin);
    try {
        skin->file_ = open::parseSkin(*text);
    } catch (const FormatError& e) {
        log::error("skin.txt: {}", e.what());
        return nullptr;
    }
    skin->pack_ = std::move(pack);
    skin->renderer_ = renderer;
    if (!skin->file_.props.empty()) {
        if (auto sheet = loadSheet(*skin->pack_, skin->file_.props)) {
            skin->props_ = std::make_unique<SpriteBank>(std::move(*sheet));
            for (std::size_t i = 0; i < skin->props_->names.size(); ++i) {
                if (!skin->props_->names[i].empty()) skin->propFrames_[skin->props_->names[i]] = static_cast<int>(i);
            }
        }
    }
    // Report props the rules name that the sheet lacks, once.
    for (const auto& [bank, rules] : skin->file_.objects) {
        for (const auto& [pattern, pieces] : rules) {
            for (const auto& piece : pieces) {
                if (!skin->propFrames_.contains(piece.prop)) {
                    log::warn("skin.txt: [objects {}] {} uses unknown prop {}", bank, pattern, piece.prop);
                    skin->propFrames_[piece.prop] = -1;
                }
            }
        }
    }
    log::info("Drawing the original maps with the pack's art: {} ground banks, {} object banks", skin->file_.ground.size(),
              skin->file_.objects.size());
    return skin;
}

Skin::~Skin() {
    setMap(nullptr);
    for (auto& [name, texture] : propTextures_) {
        if (texture) SDL_DestroyTexture(texture);
    }
}

std::optional<CharacterModel> Skin::character(const std::string& stem) const {
    if (!pack_->exists(stem + ".anim")) return std::nullopt;
    return CharacterModel::load(*pack_, stem);
}

const MaterialTexture* Skin::material(const std::string& name) {
    if (name == "none") return nullptr;
    auto [it, inserted] = materials_.try_emplace(name);
    if (inserted) {
        const auto found = file_.materials.find(name);
        if (found == file_.materials.end()) {
            log::warn("skin.txt: unknown material {}", name);
        } else if (auto sheet = loadSheet(*pack_, found->second.sheet)) {
            const auto& m = found->second;
            auto texture = buildMaterialTexture(*sheet, m.first, m.period, m.tile * projection.x / 100,
                                                m.tile * projection.y / 100);
            if (texture.argb.empty()) {
                log::warn("skin.txt: material {} needs frames {} to {} of {}", name, m.first,
                          m.first + m.period * m.period - 1, m.sheet);
            } else {
                it->second = std::move(texture);
            }
        }
    }
    return it->second ? &*it->second : nullptr;
}

void Skin::setMap(const Map* map) {
    for (auto& [key, texture] : chunks_) SDL_DestroyTexture(texture);
    chunks_.clear();
    chips_.clear();
    feet_.clear();
    bankRule_.clear();
    bankName_.clear();
    rules_.clear();
    map_ = map;
    if (!map) return;
    std::map<std::string, int> ruleOf;
    int replaced = 0, total = 0;
    for (std::size_t i = 0; i < map->bankCount(); ++i) {
        bankName_.push_back(shadedBank(map->bankFile(static_cast<int>(i))));
        const auto found = file_.ground.find(lowerCase(map->bankFile(static_cast<int>(i))));
        int index = -1;
        if (found != file_.ground.end()) {
            auto [at, inserted] = ruleOf.try_emplace(found->first, static_cast<int>(rules_.size()));
            if (inserted) {
                GroundRule rule;
                for (const auto& m : found->second) {
                    const MaterialTexture* texture = material(m.material);
                    if (texture && (m.colour.r || m.colour.g || m.colour.b)) {
                        const std::tuple key{m.material, int{m.colour.r}, int{m.colour.g}, int{m.colour.b}};
                        auto [at, made] = tinted_.try_emplace(key);
                        if (made) at->second = tinted(*texture, m.colour);
                        texture = &at->second;
                    }
                    rule.entries.push_back({texture, m.colour});
                }
                rules_.push_back(std::move(rule));
            }
            index = at->second;
        }
        bankRule_.push_back(index);
    }
    for (const auto& o : map->objects().objects) {
        if (map->isShadowBank(o.bank)) continue;
        ++total;
        if (replacement(o)) ++replaced;
    }
    log::info("Skin: {} of {} objects on {} drawn with the pack's props", replaced, total, map->name());
}

const SpriteBank::Picture* Skin::chip(int bank, int pattern) {
    auto [it, inserted] = chips_.try_emplace({bank, pattern});
    if (inserted) {
        if (const SpriteBank* b = map_->bank(bank)) it->second = b->render(pattern, true);
    }
    return it->second.argb.empty() ? nullptr : &it->second;
}

SDL_Texture* Skin::groundChunk(int cx, int cy) {
    for (auto it = chunks_.begin(); it != chunks_.end(); ++it) {
        if (it->first == std::pair{cx, cy}) {
            chunks_.splice(chunks_.begin(), chunks_, it);
            return it->second;
        }
    }
    const auto& g = map_->ground();
    const int x0 = cx * kChunkWidth, y0 = cy * kChunkHeight;
    const int m = kSmoothing, fw = kChunkWidth + 2 * m, fh = kChunkHeight + 2 * m;
    std::vector<std::uint32_t> original(static_cast<std::size_t>(fw) * fh, 0xff000000u);
    std::vector<std::int16_t> rule(original.size(), -1);
    for (int y = 0; y < fh; ++y) {
        const int py = y0 - m + y;
        if (py < 0 || py >= g.height * g.chipHeight) continue;
        const int chipY = py / g.chipHeight;
        for (int x = 0; x < fw; ++x) {
            const int px = x0 - m + x;
            if (px < 0 || px >= g.width * g.chipWidth) continue;
            const int chipX = px / g.chipWidth;
            const auto& cell = g.at(chipX, chipY);
            const SpriteBank::Picture* pic = chip(cell.bank, cell.pattern);
            if (!pic) continue;
            const int ix = px - chipX * g.chipWidth, iy = py - chipY * g.chipHeight;
            if (ix >= pic->width || iy >= pic->height) continue;
            const std::size_t at = static_cast<std::size_t>(y) * fw + x;
            original[at] = pic->argb[static_cast<std::size_t>(iy) * pic->width + ix];
            if (cell.bank >= 0 && static_cast<std::size_t>(cell.bank) < bankRule_.size()) {
                rule[at] = static_cast<std::int16_t>(bankRule_[cell.bank]);
            }
        }
    }
    const auto pixels = paintGround(x0, y0, kChunkWidth, kChunkHeight, m, original, rule, rules_);
    SDL_Texture* texture =
        SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, kChunkWidth, kChunkHeight);
    if (texture) SDL_UpdateTexture(texture, nullptr, pixels.data(), kChunkWidth * 4);
    if (chunks_.size() >= kMaxChunks) {
        SDL_DestroyTexture(chunks_.back().second);
        chunks_.pop_back();
    }
    chunks_.emplace_front(std::pair{cx, cy}, texture);
    return texture;
}

void Skin::renderGround(int camX, int camY) {
    if (!map_) return;
    const auto& g = map_->ground();
    const int right = std::min(camX + 640, g.width * g.chipWidth), bottom = std::min(camY + 480, g.height * g.chipHeight);
    const int cx0 = std::max(0, camX) / kChunkWidth, cy0 = std::max(0, camY) / kChunkHeight;
    for (int cy = cy0; cy * kChunkHeight < bottom; ++cy) {
        for (int cx = cx0; cx * kChunkWidth < right; ++cx) {
            if (SDL_Texture* texture = groundChunk(cx, cy)) {
                SDL_Rect dst{cx * kChunkWidth - camX, cy * kChunkHeight - camY, kChunkWidth, kChunkHeight};
                SDL_RenderCopy(renderer_, texture, nullptr, &dst);
            }
        }
    }
}

const std::vector<open::Skin::Piece>* Skin::replacement(const MapObject& object) const {
    if (object.bank < 0 || static_cast<std::size_t>(object.bank) >= bankName_.size()) return nullptr;
    const auto bank = file_.objects.find(bankName_[object.bank]);
    if (bank == file_.objects.end()) return nullptr;
    const auto rule = bank->second.find(object.pattern);
    return rule == bank->second.end() ? nullptr : &rule->second;
}

void Skin::renderObject(const MapObject& object, const std::vector<open::Skin::Piece>& pieces, int camX, int camY) {
    if (!props_ || pieces.empty()) return;
    // Our props stand where the original piece's picture ends at the
    // bottom, in the middle: the foot of a tree, post or building.
    auto [foot, found] = feet_.try_emplace({object.bank, object.pattern}, std::pair{0, 0});
    if (found) {
        if (const SpriteBank* bank = map_->bank(object.bank)) {
            const auto pic = bank->render(object.pattern, false);
            int left = pic.width, right = -1, bottom = -1;
            for (int y = 0; y < pic.height; ++y) {
                for (int x = 0; x < pic.width; ++x) {
                    if (!(pic.argb[static_cast<std::size_t>(y) * pic.width + x] >> 24)) continue;
                    left = std::min(left, x);
                    right = std::max(right, x);
                    bottom = y;
                }
            }
            if (right >= 0) foot->second = {pic.x + (left + right + 1) / 2, pic.y + bottom + 1};
        }
    }
    int footX, footY;
    worldToScreen(object.x, object.y, footX, footY);
    footX += foot->second.first;
    footY += foot->second.second;
    for (const auto& piece : pieces) {
        const auto frame = propFrames_.find(piece.prop);
        if (frame == propFrames_.end() || frame->second < 0) continue;
        auto [it, inserted] = propTextures_.try_emplace(piece.prop, nullptr);
        if (inserted) {
            const auto pic = props_->render(frame->second, false);
            if (!pic.argb.empty()) {
                it->second = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, pic.width, pic.height);
                if (it->second) {
                    SDL_UpdateTexture(it->second, nullptr, pic.argb.data(), pic.width * 4);
                    SDL_SetTextureBlendMode(it->second, SDL_BLENDMODE_BLEND);
                }
            }
        }
        if (!it->second) continue;
        int w = 0, h = 0, x = 0, y = 0;
        props_->bounds(props_->patterns[frame->second], x, y, w, h);
        int dx, dy;
        worldToScreen(piece.dx, piece.dy, dx, dy);
        // Mirrored, the anchor stays put and the picture turns about it.
        SDL_Rect dst{footX + dx + (piece.mirror ? -x - w : x) - camX, footY + dy + y - camY, w, h};
        if (dst.x + dst.w < 0 || dst.y + dst.h < 0 || dst.x >= 640 || dst.y >= 480) continue;
        SDL_RenderCopyEx(renderer_, it->second, nullptr, &dst, 0, nullptr, piece.mirror ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    }
}

}  // namespace sf

#include "game/skin.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>

#include "core/log.h"
#include "formats/binary_reader.h"
#include "formats/map_layers.h"
#include "game/content.h"
#include "game/map.h"
#include "platform/filesystem.h"
#include "render/sprite_cache.h"

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
    for (auto* textures : {&propTextures_, &propOutlines_}) {
        for (auto& [name, texture] : *textures) {
            if (texture) SDL_DestroyTexture(texture);
        }
    }
}

std::uint64_t Skin::fingerprint() const {
    // FNV-1a over skin.txt and each sheet it names, with its picture.
    std::uint64_t hash = 0xcbf29ce484222325ull;
    auto add = [&](const std::string& path) {
        const auto bytes = pack_->read(path);
        if (!bytes) return;
        for (const std::uint8_t b : *bytes) hash = (hash ^ b) * 0x100000001b3ull;
    };
    auto addSheet = [&](const std::string& path) {
        add(path);
        if (const auto text = pack_->read(path)) {
            try {
                const auto slash = path.find_last_of('/');
                add((slash == std::string::npos ? std::string() : path.substr(0, slash + 1)) + open::parseSheet(*text).image);
            } catch (const FormatError&) {
            }
        }
    };
    add("skin.txt");
    if (!file_.props.empty()) addSheet(file_.props);
    for (const auto& [name, material] : file_.materials) addSheet(material.sheet);
    return hash;
}

std::optional<CharacterModel> Skin::character(const std::string& stem) const {
    if (!pack_->exists(stem + ".anim")) return std::nullopt;
    return CharacterModel::load(*pack_, stem);
}

const SpriteBank* Skin::sheet(const std::string& stem) {
    auto [it, inserted] = sheets_.try_emplace(stem);
    if (inserted && pack_->exists(stem + ".sheet")) {
        if (auto loaded = loadSheet(*pack_, stem + ".sheet")) it->second = std::make_unique<SpriteBank>(std::move(*loaded));
    }
    return it->second.get();
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
    chipSums_.clear();
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
                    rule.entries.push_back({texture, m.colour, texture ? averageColour(*texture) : Rgb{}});
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

const Skin::ChipSums* Skin::chipSums(int bank, int pattern) {
    auto [it, inserted] = chipSums_.try_emplace({bank, pattern});
    if (inserted) {
        if (const SpriteBank::Picture* pic = chip(bank, pattern)) {
            ChipSums& sums = it->second;
            sums.width = pic->width;
            sums.height = pic->height;
            const std::size_t stride = static_cast<std::size_t>(pic->width) + 1;
            sums.rgb.assign(stride * (static_cast<std::size_t>(pic->height) + 1), {0, 0, 0});
            for (int y = 0; y < pic->height; ++y) {
                std::array<std::uint32_t, 3> row{0, 0, 0};
                for (int x = 0; x < pic->width; ++x) {
                    const std::uint32_t c = pic->argb[static_cast<std::size_t>(y) * pic->width + x];
                    row[0] += (c >> 16) & 255;
                    row[1] += (c >> 8) & 255;
                    row[2] += c & 255;
                    const auto& above = sums.rgb[static_cast<std::size_t>(y) * stride + x + 1];
                    sums.rgb[(static_cast<std::size_t>(y) + 1) * stride + x + 1] = {above[0] + row[0], above[1] + row[1], above[2] + row[2]};
                }
            }
        }
    }
    return it->second.rgb.empty() ? nullptr : &it->second;
}

std::vector<std::uint32_t> Skin::groundOverview(int left, int top, int scale, int w, int h) {
    std::vector<std::uint32_t> out(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0xff000000u);
    if (!map_ || scale < 1) return out;
    const auto& g = map_->ground();
    if (g.chipWidth <= 0 || g.chipHeight <= 0) return out;
    const int mapW = g.width * g.chipWidth, mapH = g.height * g.chipHeight;
    // Each block's original colours added up by the rule painting them
    // (none: kept as they are), a chip at a time from the chip's running
    // sums, then painted as paintGround paints them on average.
    struct Part {
        std::size_t rule;
        float r, g, b, count;
    };
    const std::size_t none = rules_.size();
    std::vector<Part> parts;
    std::vector<float> weights;
    const float n = static_cast<float>(scale * scale);
    for (int by = 0; by < h; ++by) {
        const int y0 = std::max(0, top + by * scale), y1 = std::min(mapH, top + (by + 1) * scale);
        for (int bx = 0; bx < w; ++bx) {
            const int x0 = std::max(0, left + bx * scale), x1 = std::min(mapW, left + (bx + 1) * scale);
            parts.clear();
            for (int cy = y0 / g.chipHeight; cy * g.chipHeight < y1; ++cy) {
                for (int cx = x0 / g.chipWidth; cx * g.chipWidth < x1; ++cx) {
                    const auto& cell = g.at(cx, cy);
                    const ChipSums* sums = chipSums(cell.bank, cell.pattern);
                    if (!sums) continue;
                    // The block's part of this chip, in the chip's pixels,
                    // as far as its picture goes (past it is black).
                    const int ax = std::max(x0 - cx * g.chipWidth, 0), ay = std::max(y0 - cy * g.chipHeight, 0);
                    const int bx1 = std::min({x1 - cx * g.chipWidth, g.chipWidth, sums->width});
                    const int by1 = std::min({y1 - cy * g.chipHeight, g.chipHeight, sums->height});
                    if (bx1 <= ax || by1 <= ay) continue;
                    const std::size_t stride = static_cast<std::size_t>(sums->width) + 1;
                    auto at = [&](int x, int y) -> const auto& { return sums->rgb[static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x)]; };
                    const auto &a = at(ax, ay), &b = at(bx1, ay), &c = at(ax, by1), &d = at(bx1, by1);
                    const std::size_t rule = cell.bank >= 0 && static_cast<std::size_t>(cell.bank) < bankRule_.size() && bankRule_[cell.bank] >= 0
                                                 ? static_cast<std::size_t>(bankRule_[cell.bank])
                                                 : none;
                    const float count = static_cast<float>((bx1 - ax) * (by1 - ay));
                    const float r = static_cast<float>(d[0] - b[0] - c[0] + a[0]), gr = static_cast<float>(d[1] - b[1] - c[1] + a[1]),
                                bl = static_cast<float>(d[2] - b[2] - c[2] + a[2]);
                    const auto same = std::ranges::find(parts, rule, &Part::rule);
                    if (same == parts.end()) {
                        parts.push_back({rule, r, gr, bl, count});
                    } else {
                        same->r += r;
                        same->g += gr;
                        same->b += bl;
                        same->count += count;
                    }
                }
            }
            float rgb[3] = {0, 0, 0};
            for (const Part& p : parts) {
                const float r = p.r / p.count, g = p.g / p.count, b = p.b / p.count;
                Rgb c{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b)};
                if (p.rule != none && !rules_[p.rule].entries.empty()) c = averagePainted(rules_[p.rule], r, g, b, weights);
                rgb[0] += c.r * p.count / n;
                rgb[1] += c.g * p.count / n;
                rgb[2] += c.b * p.count / n;
            }
            out[static_cast<std::size_t>(by) * static_cast<std::size_t>(w) + static_cast<std::size_t>(bx)] =
                0xff000000u | static_cast<std::uint32_t>(std::min(255.0f, rgb[0])) << 16 |
                static_cast<std::uint32_t>(std::min(255.0f, rgb[1])) << 8 | static_cast<std::uint32_t>(std::min(255.0f, rgb[2]));
        }
    }
    return out;
}

void Skin::renderGround(int camX, int camY) {
    if (!map_) return;
    const auto& g = map_->ground();
    // As much as the target shows: more than the screen when zoomed out.
    SDL_Rect view;
    SDL_RenderGetViewport(renderer_, &view);
    const int right = std::min(camX + view.w, g.width * g.chipWidth), bottom = std::min(camY + view.h, g.height * g.chipHeight);
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
    auto [foot, found] = feet_.try_emplace({object.bank, object.pattern}, std::pair{0, 0});
    if (found) {
        if (const SpriteBank* bank = map_->bank(object.bank)) foot->second = footOf(*bank, object.pattern);
    }
    int footX, footY;
    worldToScreen(object.x, object.y, footX, footY);
    renderPieces(pieces, footX + foot->second.first, footY + foot->second.second, camX, camY);
}

const std::vector<open::Skin::Piece>* Skin::scenarioObject(int kind, int pattern) const {
    const auto bank = file_.objects.find(std::format("object/{:08d}", kind));
    if (bank == file_.objects.end()) return nullptr;
    const auto rule = bank->second.find(pattern);
    return rule == bank->second.end() ? nullptr : &rule->second;
}

void Skin::renderScenarioObject(int worldX, int worldY, const std::vector<open::Skin::Piece>& pieces, int camX, int camY, int glow,
                                const SDL_Color* outline) {
    if (!props_ || pieces.empty()) return;
    int footX, footY;
    worldToScreen(worldX, worldY, footX, footY);
    renderPieces(pieces, footX, footY, camX, camY, glow, outline);
}

// Our props stand where the original piece's picture ends at the bottom,
// in the middle: the foot of a tree, post or building.
std::pair<int, int> Skin::footOf(const SpriteBank& bank, int pattern) {
    const auto pic = bank.render(pattern, false);
    int left = pic.width, right = -1, bottom = -1;
    for (int y = 0; y < pic.height; ++y) {
        for (int x = 0; x < pic.width; ++x) {
            if (!(pic.argb[static_cast<std::size_t>(y) * pic.width + x] >> 24)) continue;
            left = std::min(left, x);
            right = std::max(right, x);
            bottom = y;
        }
    }
    if (right < 0) return {0, 0};
    return {pic.x + (left + right + 1) / 2, pic.y + bottom + 1};
}

SDL_Texture* Skin::propTexture(const std::string& prop, int& frame) {
    const auto found = propFrames_.find(prop);
    frame = found == propFrames_.end() ? -1 : found->second;
    if (frame < 0 || !props_) return nullptr;
    auto [it, inserted] = propTextures_.try_emplace(prop, nullptr);
    if (inserted) {
        const auto pic = props_->render(frame, false);
        if (!pic.argb.empty()) {
            it->second = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, pic.width, pic.height);
            if (it->second) {
                SDL_UpdateTexture(it->second, nullptr, pic.argb.data(), pic.width * 4);
                SDL_SetTextureBlendMode(it->second, SDL_BLENDMODE_BLEND);
            }
        }
    }
    return it->second;
}

void Skin::renderPieces(const std::vector<open::Skin::Piece>& pieces, int footX, int footY, int camX, int camY, int glow,
                        const SDL_Color* outline) {
    SDL_Rect view;
    SDL_RenderGetViewport(renderer_, &view);
    struct Placed {
        const open::Skin::Piece* piece;
        SDL_Texture* texture;
        SDL_Rect dst;
        SDL_RendererFlip flip;
    };
    std::vector<Placed> placed;
    for (const auto& piece : pieces) {
        int frame;
        SDL_Texture* texture = propTexture(piece.prop, frame);
        if (!texture) continue;
        int w = 0, h = 0, x = 0, y = 0;
        props_->bounds(props_->patterns[frame], x, y, w, h);
        int dx, dy;
        worldToScreen(piece.dx, piece.dy, dx, dy);
        // Mirrored, the anchor stays put and the picture turns about it.
        SDL_Rect dst{footX + dx + (piece.mirror ? -x - w : x) - camX, footY + dy + y - camY, w, h};
        if (dst.x + dst.w < 0 || dst.y + dst.h < 0 || dst.x >= view.w || dst.y >= view.h) continue;
        placed.push_back({&piece, texture, dst, piece.mirror ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE});
    }
    // The outline goes under all the pieces, so it rings only their shape
    // as a whole.
    if (outline) {
        for (const auto& p : placed) {
            SDL_Texture* ring = propOutline(p.piece->prop);
            if (!ring) continue;
            const SDL_Rect around{p.dst.x - kOutlineWidth, p.dst.y - kOutlineWidth, p.dst.w + 2 * kOutlineWidth, p.dst.h + 2 * kOutlineWidth};
            SDL_SetTextureColorMod(ring, outline->r, outline->g, outline->b);
            SDL_SetTextureAlphaMod(ring, outline->a);
            SDL_RenderCopyEx(renderer_, ring, nullptr, &around, 0, nullptr, p.flip);
        }
    }
    for (const auto& p : placed) {
        SDL_RenderCopyEx(renderer_, p.texture, nullptr, &p.dst, 0, nullptr, p.flip);
        if (glow > 0) {
            // A second copy added on top brightens it.
            SDL_SetTextureBlendMode(p.texture, SDL_BLENDMODE_ADD);
            SDL_SetTextureAlphaMod(p.texture, static_cast<Uint8>(std::min(1000, glow) * 255 / 1000));
            SDL_RenderCopyEx(renderer_, p.texture, nullptr, &p.dst, 0, nullptr, p.flip);
            SDL_SetTextureBlendMode(p.texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(p.texture, 255);
        }
    }
}

SDL_Texture* Skin::propOutline(const std::string& prop) {
    auto [it, inserted] = propOutlines_.try_emplace(prop, nullptr);
    if (inserted) {
        const auto found = propFrames_.find(prop);
        if (found != propFrames_.end() && props_) it->second = makeTexture(renderer_, outlineOf(props_->render(found->second, false)));
    }
    return it->second;
}

}  // namespace sf

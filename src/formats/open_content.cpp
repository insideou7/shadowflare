#include "formats/open_content.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <format>
#include <map>
#include <optional>
#include <string_view>

#include "formats/binary_reader.h"
#include "stb_image.h"

namespace sf::open {

namespace {

constexpr int kMaxSize = 16384;  // limit on image, map and frame dimensions
constexpr int kMaxAnimationLayers = 32;  // the original hero has 23

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

// A text file split into lines without comments, remembering line numbers
// for error messages.
class Lines {
public:
    explicit Lines(std::span<const std::uint8_t> data) : text_(reinterpret_cast<const char*>(data.data()), data.size()) {}

    // The next non-empty line, or nothing at the end.
    std::optional<std::string_view> next() {
        while (pos_ < text_.size()) {
            auto end = text_.find('\n', pos_);
            if (end == std::string_view::npos) end = text_.size();
            std::string_view line = text_.substr(pos_, end - pos_);
            pos_ = end + 1;
            ++number_;
            if (const auto hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
            line = trim(line);
            if (!line.empty()) return line;
        }
        return std::nullopt;
    }

    [[noreturn]] void fail(std::string_view what) const { throw FormatError(std::format("line {}: {}", number_, what)); }

    int toInt(std::string_view s) const {
        s = trim(s);
        int value = 0;
        const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
        if (ec != std::errc() || end != s.data() + s.size()) fail(std::format("'{}' is not a number", s));
        return value;
    }

    // Comma-separated numbers; exactly `count` of them unless count is -1.
    std::vector<int> ints(std::string_view s, int count = -1) const {
        std::vector<int> values;
        for (const auto& field : split(s, ',')) values.push_back(toInt(field));
        if (count >= 0 && static_cast<int>(values.size()) != count) fail(std::format("expected {} numbers", count));
        return values;
    }

    static std::vector<std::string_view> split(std::string_view s, char separator) {
        std::vector<std::string_view> fields;
        std::size_t start = 0;
        while (true) {
            const auto at = s.find(separator, start);
            fields.push_back(trim(s.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start)));
            if (at == std::string_view::npos) break;
            start = at + 1;
        }
        return fields;
    }

private:
    std::string_view text_;
    std::size_t pos_ = 0;
    int number_ = 0;
};

// "key=value" -> key, value. Section headers are "[name]".
bool keyValue(std::string_view line, std::string_view& key, std::string_view& value) {
    const auto eq = line.find('=');
    if (eq == std::string_view::npos) return false;
    key = trim(line.substr(0, eq));
    value = trim(line.substr(eq + 1));
    return true;
}

const std::map<std::string_view, ItemDef::Class, std::less<>> kClasses = {
    {"weapon", ItemDef::Class::Weapon},         {"armour", ItemDef::Class::Armour},
    {"accessory", ItemDef::Class::Accessory}, {"consumable", ItemDef::Class::Consumable},
    {"special", ItemDef::Class::Special},
};

bool isSection(std::string_view line) { return line.size() >= 2 && line.front() == '[' && line.back() == ']'; }

std::string_view sectionName(std::string_view line) { return trim(line.substr(1, line.size() - 2)); }

}  // namespace

Image decodeImage(std::span<const std::uint8_t> data) {
    int w = 0, h = 0, channels = 0;
    if (data.size() > static_cast<std::size_t>(INT32_MAX)) throw FormatError("image too large");
    stbi_uc* pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &w, &h, &channels, 4);
    if (!pixels) throw FormatError(std::format("cannot decode image: {}", stbi_failure_reason()));
    Image image;
    if (w <= 0 || h <= 0 || w > kMaxSize || h > kMaxSize) {
        stbi_image_free(pixels);
        throw FormatError("bad image size");
    }
    image.width = w;
    image.height = h;
    image.argb.resize(static_cast<std::size_t>(w) * h);
    for (std::size_t i = 0; i < image.argb.size(); ++i) {
        const stbi_uc* p = pixels + i * 4;
        image.argb[i] = (static_cast<std::uint32_t>(p[3]) << 24) | (static_cast<std::uint32_t>(p[0]) << 16) |
                        (static_cast<std::uint32_t>(p[1]) << 8) | p[2];
    }
    stbi_image_free(pixels);
    return image;
}

Sheet parseSheet(std::span<const std::uint8_t> text) {
    Lines lines(text);
    Sheet sheet;
    while (auto line = lines.next()) {
        std::string_view key, value;
        if (!keyValue(*line, key, value)) lines.fail("expected key=value");
        if (key == "image") {
            sheet.image = value;
        } else if (key == "frame") {
            const auto v = lines.ints(value, 6);
            if (v[2] <= 0 || v[3] <= 0 || v[2] > kMaxSize || v[3] > kMaxSize) lines.fail("bad frame size");
            sheet.frames.push_back({v[0], v[1], v[2], v[3], v[4], v[5]});
        } else {
            lines.fail(std::format("unknown key '{}'", key));
        }
    }
    if (sheet.image.empty()) throw FormatError("sheet names no image");
    return sheet;
}

SpriteBank makeSpriteBank(const Sheet& sheet, const Image& image) {
    SpriteBank bank;
    for (const auto& f : sheet.frames) {
        if (f.x < 0 || f.y < 0 || f.x + f.width > image.width || f.y + f.height > image.height) {
            throw FormatError(std::format("frame {},{} {}x{} lies outside the {}x{} image", f.x, f.y, f.width, f.height,
                                          image.width, image.height));
        }
        SpritePart part;
        part.bpp = 32;
        part.width = f.width;
        part.height = f.height;
        part.argb.resize(static_cast<std::size_t>(f.width) * f.height);
        for (int y = 0; y < f.height; ++y) {
            const auto* row = image.argb.data() + static_cast<std::size_t>(f.y + y) * image.width + f.x;
            std::copy(row, row + f.width, part.argb.begin() + static_cast<std::ptrdiff_t>(y) * f.width);
        }
        Pattern pattern;
        PatternPart placed;
        placed.part = static_cast<std::int32_t>(bank.parts.size());
        placed.x = -f.anchorX;
        placed.y = -f.anchorY;
        pattern.parts.push_back(placed);
        bank.parts.push_back(std::move(part));
        bank.patterns.push_back(std::move(pattern));
    }
    return bank;
}

// [chart <number> <loop|once>], then "<direction>=<frame>,<frame>,..."
Animation parseAnimation(std::span<const std::uint8_t> text) {
    Lines lines(text);
    Animation anim;
    AnimationSet::Chart* chart = nullptr;
    while (auto line = lines.next()) {
        if (isSection(*line)) {
            const auto words = Lines::split(sectionName(*line), ' ');
            if (words.size() != 3 || words[0] != "chart") lines.fail("expected [chart <number> loop|once]");
            const int number = lines.toInt(words[1]);
            if (number < 0 || number > 255) lines.fail("bad chart number");
            if (words[2] != "loop" && words[2] != "once") lines.fail("a chart is either loop or once");
            if (anim.set.charts.size() <= static_cast<std::size_t>(number)) anim.set.charts.resize(number + 1);
            chart = &anim.set.charts[number];
            chart->status = words[2] == "loop" ? 1 : 0;
            chart->directions.assign(AnimationSet::kDirections, {});
            continue;
        }
        std::string_view key, value;
        if (!keyValue(*line, key, value)) lines.fail("expected key=value");
        if (key == "sheet") {
            anim.sheet = value;
            continue;
        }
        if (!chart) lines.fail(std::format("unknown key '{}'", key));
        // "<direction>[.<layer>][@<priority>]": layer 0 (the body) when
        // left out; the priority is the default for the line's frames.
        std::int16_t linePriority = 0;
        if (const auto at = key.find('@'); at != std::string_view::npos) {
            linePriority = static_cast<std::int16_t>(lines.toInt(key.substr(at + 1)));
            key = key.substr(0, at);
        }
        int layerNumber = 0;
        if (const auto dot = key.find('.'); dot != std::string_view::npos) {
            layerNumber = lines.toInt(key.substr(dot + 1));
            key = key.substr(0, dot);
        }
        if (layerNumber < 0 || layerNumber >= kMaxAnimationLayers) lines.fail("layers are 0 to 31");
        const int direction = lines.toInt(key);
        if (direction < 0 || direction >= 8) lines.fail("directions are 0 to 7");
        auto& dir = chart->directions[direction];
        AnimationSet::Layer layer;
        // Each frame is a sheet frame, "-" for nothing, optionally followed
        // by "@<priority>".
        for (auto field : Lines::split(value, ',')) {
            AnimationSet::Cell cell;
            cell.priority = linePriority;
            if (const auto at = field.find('@'); at != std::string_view::npos) {
                cell.priority = static_cast<std::int16_t>(lines.toInt(field.substr(at + 1)));
                field = trim(field.substr(0, at));
            }
            cell.pattern = field == "-" ? -1 : lines.toInt(field);
            layer.push_back(cell);
        }
        if (layer.size() > 4096) lines.fail("too many frames");
        if (dir.layers.size() <= static_cast<std::size_t>(layerNumber)) dir.layers.resize(layerNumber + 1);
        dir.layers[layerNumber] = std::move(layer);
        std::size_t frames = 0;
        for (const auto& l : dir.layers) frames = std::max(frames, l.size());
        dir.frameCount = static_cast<std::int16_t>(frames);
    }
    if (anim.sheet.empty()) throw FormatError("animation names no sheet");
    // Directions left out of a chart borrow direction 0.
    for (auto& c : anim.set.charts) {
        if (c.directions.empty()) continue;
        for (int d = 1; d < 8; ++d) {
            if (c.directions[d].frameCount == 0) c.directions[d] = c.directions[0];
        }
    }
    return anim;
}

Map parseMap(std::span<const std::uint8_t> text) {
    Lines lines(text);
    Map map;
    auto& g = map.ground;
    g.width = g.height = 0;
    std::string_view section;
    int groundRow = 0, judgeRow = 0;
    while (auto line = lines.next()) {
        if (isSection(*line)) {
            section = sectionName(*line);
            if (section != "ground" && section != "judge" && section != "objects") lines.fail("unknown section");
            if (section == "ground" && g.cells.empty()) lines.fail("size must come before [ground]");
            if (section == "judge" && g.judge.empty()) lines.fail("judge must come before [judge]");
            continue;
        }
        if (section.empty()) {
            std::string_view key, value;
            if (!keyValue(*line, key, value)) lines.fail("expected key=value");
            if (key == "size") {
                const auto v = lines.ints(value, 2);
                if (v[0] <= 0 || v[1] <= 0 || v[0] > 4096 || v[1] > 4096) lines.fail("bad map size");
                g.width = v[0];
                g.height = v[1];
                g.cells.assign(static_cast<std::size_t>(g.width) * g.height, {});
            } else if (key == "chip") {
                const auto v = lines.ints(value, 2);
                if (v[0] <= 0 || v[1] <= 0 || v[0] > 1024 || v[1] > 1024) lines.fail("bad chip size");
                g.chipWidth = v[0];
                g.chipHeight = v[1];
            } else if (key == "cell") {
                const auto v = lines.ints(value, 2);
                if (v[0] < 10 || v[1] < 10 || v[0] > 10000 || v[1] > 10000) lines.fail("bad cell size");
                g.magX = v[0];
                g.magY = v[1];
            } else if (key == "judge") {
                const auto v = lines.ints(value, 4);
                if (v[2] <= 0 || v[3] <= 0 || v[2] > kMaxSize || v[3] > kMaxSize) lines.fail("bad judge size");
                g.judgeX = v[0];
                g.judgeY = v[1];
                g.judgeWidth = v[2];
                g.judgeHeight = v[3];
                g.judge.assign(static_cast<std::size_t>(v[2]) * v[3], GroundLayer::kBlocked);
            } else if (key == "tiles") {
                const int t = lines.toInt(value);
                if (t < 10 || t > 100000) lines.fail("bad tile size");
                g.isoTile = t;
            } else if (key == "bank") {
                map.banks.emplace_back(value);
            } else {
                lines.fail(std::format("unknown key '{}'", key));
            }
        } else if (section == "ground") {
            // bank:pattern per chip, separated by spaces; '.' leaves it empty.
            if (groundRow >= g.height) lines.fail("more ground rows than the map is high");
            int x = 0;
            for (auto field : Lines::split(*line, ' ')) {
                if (field.empty()) continue;
                if (x >= g.width) lines.fail("ground row is wider than the map");
                auto& cell = g.cells[static_cast<std::size_t>(groundRow) * g.width + x++];
                if (field == ".") continue;
                const auto colon = field.find(':');
                if (colon == std::string_view::npos) lines.fail("ground chips are bank:pattern");
                cell.bank = static_cast<std::int16_t>(lines.toInt(field.substr(0, colon)));
                cell.pattern = static_cast<std::int16_t>(lines.toInt(field.substr(colon + 1)));
            }
            ++groundRow;
        } else if (section == "judge") {
            // One character per cell: 'x' blocked, anything else walkable.
            if (judgeRow >= g.judgeHeight) lines.fail("more judge rows than declared");
            if (static_cast<int>(line->size()) != g.judgeWidth) lines.fail("judge row has the wrong width");
            for (int x = 0; x < g.judgeWidth; ++x) {
                g.judge[static_cast<std::size_t>(judgeRow) * g.judgeWidth + x] = (*line)[x] == 'x' ? GroundLayer::kBlocked : 0;
            }
            ++judgeRow;
        } else {
            // x,y,bank,pattern in world units.
            const auto v = lines.ints(*line, 4);
            MapObject o;
            o.x = v[0];
            o.y = v[1];
            o.bank = static_cast<std::int16_t>(v[2]);
            o.pattern = static_cast<std::int16_t>(v[3]);
            map.objects.objects.push_back(o);
        }
    }
    if (g.cells.empty()) throw FormatError("map has no size");
    return map;
}

// Lines are "<kind of thing>=<fields>"; see docs/content.md.
Scenario parseScenario(std::span<const std::uint8_t> text) {
    using Group = ScenarioControl::Group;
    Lines lines(text);
    Scenario s;
    auto& c = s.control;
    int nextId[4] = {};
    while (auto line = lines.next()) {
        std::string_view key, value;
        if (!keyValue(*line, key, value)) lines.fail("expected key=value");
        if (key == "title") {
            c.title = value;
        } else if (key == "map") {
            c.mapPath = value;
        } else if (key == "entry") {
            // entry number, x, y, direction: the player's spot for that entry
            const auto v = lines.ints(value, 4);
            c.entryPoints.push_back({v[0] * 4, v[1], v[2], v[3]});
        } else if (key == "enemy" || key == "person") {
            // enemy=kind,x,y,direction,hp,attack,wander radius,ai list name
            //       [,level,gold chance,gold min,gold max[,experience[,defense,hit,evasion[,magic defense,element]]]]
            // person=kind,x,y,direction[,name]: the name is what script.txt
            // calls them.
            const bool enemy = key == "enemy";
            auto fields = Lines::split(value, ',');
            if (enemy ? fields.size() != 8u && fields.size() != 12u && fields.size() != 13u && fields.size() != 16u && fields.size() != 18u
                      : fields.size() != 4u && fields.size() != 5u) {
                lines.fail(enemy ? "enemy needs 8, 12, 13, 16 or 18 fields" : "person needs 4 or 5 fields");
            }
            ScenarioControl::Placement p;
            p.group = enemy ? Group::Enemy : Group::Person;
            p.id = nextId[static_cast<int>(p.group)]++;
            p.kind = lines.toInt(fields[0]);
            p.x = lines.toInt(fields[1]);
            p.y = lines.toInt(fields[2]);
            p.direction = lines.toInt(fields[3]);
            if (!enemy && fields.size() == 5) p.name = fields[4];
            // A collision box of about one walkability cell.
            p.rect[0] = p.rect[1] = -60;
            p.rect[2] = p.rect[3] = 60;
            if (enemy) {
                p.stats.hp = lines.toInt(fields[4]);
                p.stats.attacks.fill(lines.toInt(fields[5]));
                const int r = lines.toInt(fields[6]);
                p.stats.area[0] = p.stats.area[1] = -r;
                p.stats.area[2] = p.stats.area[3] = r;
                p.stats.ai = fields[7];
                if (fields.size() >= 12) {
                    p.stats.level = lines.toInt(fields[8]);
                    p.stats.goldChance = lines.toInt(fields[9]);
                    p.stats.goldMin = lines.toInt(fields[10]);
                    p.stats.goldMax = lines.toInt(fields[11]);
                }
                if (fields.size() >= 13) p.stats.experience = lines.toInt(fields[12]);
                if (fields.size() >= 16) {
                    p.stats.defense = lines.toInt(fields[13]);
                    p.stats.hits.fill(lines.toInt(fields[14]));
                    p.stats.evasion = lines.toInt(fields[15]);
                }
                if (fields.size() == 18) {
                    p.stats.magicDefense = lines.toInt(fields[16]);
                    p.stats.element = lines.toInt(fields[17]);
                }
            }
            c.placements.push_back(std::move(p));
        } else if (key == "item") {
            // item=class,code,x,y: an item lying on the ground
            const auto v = lines.ints(value, 4);
            ScenarioControl::Placement p;
            p.group = Group::Item;
            p.id = nextId[3]++;
            p.itemClass = v[0];
            p.itemCode = v[1];
            p.x = v[2];
            p.y = v[3];
            p.direction = 8;
            c.placements.push_back(std::move(p));
        } else if (key == "exit") {
            // exit=left,top,right,bottom,scenario,entry: walking into the
            // world rectangle goes to that scenario's entry point.
            const auto v = lines.ints(value, 6);
            ScenarioControl::Placement area;
            area.group = Group::Object;
            area.id = nextId[0]++;
            area.rect[0] = v[0];
            area.rect[1] = v[1];
            area.rect[2] = v[2];
            area.rect[3] = v[3];
            const auto sentence = static_cast<std::int32_t>(s.script.sentences.size());
            ScenarioScript::Command change;
            change.opcode = ScenarioScript::kChangeScenario;
            change.operands = {{ScenarioScript::kConstant, v[4]}, {ScenarioScript::kConstant, v[5]}};
            s.script.sentences.push_back({change});
            s.script.triggers.push_back({0, ScenarioScript::kOnJudge, ScenarioControl::idBase(Group::Object) + area.id, sentence});
            c.placements.push_back(std::move(area));
        } else {
            lines.fail(std::format("unknown key '{}'", key));
        }
    }
    return s;
}

// Top-level person=, object= and temp= lines, then [message <id>] and
// [sentence <name>] sections; see docs/content.md.
void parseScript(std::span<const std::uint8_t> text, Scenario& scenario) {
    using Group = ScenarioControl::Group;
    using S = ScenarioScript;
    Lines lines(text);
    auto& script = scenario.script;
    auto& control = scenario.control;
    // In-game ids of named people and objects.
    std::map<std::string, std::int32_t, std::less<>> characters;
    int next[4] = {};
    for (const auto& p : control.placements) {
        next[static_cast<int>(p.group)] = std::max(next[static_cast<int>(p.group)], p.id + 1);
        if (!p.name.empty()) characters.emplace(p.name, ScenarioControl::idBase(p.group) + p.id);
    }
    auto place = [&](ScenarioControl::Placement p) {
        p.id = next[static_cast<int>(p.group)]++;
        if (!p.name.empty() && !characters.emplace(p.name, ScenarioControl::idBase(p.group) + p.id).second) {
            lines.fail(std::format("'{}' is named twice", p.name));
        }
        control.placements.push_back(std::move(p));
    };
    auto character = [&](std::string_view word) {
        if (const auto it = characters.find(word); it != characters.end()) return it->second;
        if (!word.empty() && (word.front() == '-' || (word.front() >= '0' && word.front() <= '9'))) return lines.toInt(word);
        lines.fail(std::format("no one is called '{}'", word));
    };

    // Sentence names, and the operands waiting for them.
    std::map<std::string, std::int32_t, std::less<>> sentenceNumbers;
    struct Reference {
        std::size_t sentence, command, operand;
        std::string name;
    };
    std::vector<Reference> references;
    std::string* message = nullptr;
    std::optional<std::size_t> sentence;
    while (auto line = lines.next()) {
        if (isSection(*line)) {
            const auto words = Lines::split(sectionName(*line), ' ');
            message = nullptr;
            sentence.reset();
            if (words.size() == 2 && words[0] == "message") {
                const int id = lines.toInt(words[1]);
                if (script.messages.contains(id)) lines.fail("message defined twice");
                message = &script.messages[id];
            } else if (words.size() == 2 && words[0] == "sentence") {
                if (!sentenceNumbers.emplace(words[1], static_cast<std::int32_t>(script.sentences.size())).second) {
                    lines.fail("sentence defined twice");
                }
                sentence = script.sentences.size();
                script.sentences.emplace_back();
            } else {
                lines.fail("expected [message <id>] or [sentence <name>]");
            }
            continue;
        }
        if (message) {
            // Each line of a message ends in a line break, as in the original.
            *message += std::string(*line) + "\n";
            continue;
        }
        std::string_view key, value;
        if (sentence && line->starts_with("on=")) {
            // on=<event> <character>
            static const std::map<std::string_view, std::int32_t, std::less<>> kEvents = {
                {"talk", S::kOnTalk}, {"talk-end", S::kOnTalkEnd}, {"judge", S::kOnJudge},
                {"enemy-dead", S::kOnEnemyDead}, {"tick", S::kEveryTick}, {"start", S::kOnStart},
            };
            const auto words = Lines::split(trim(line->substr(3)), ' ');
            const auto event = kEvents.find(words[0]);
            if (event == kEvents.end() || words.size() > 2) lines.fail("expected on=<event> [<character>]");
            script.triggers.push_back({0, event->second, words.size() == 2 ? character(words[1]) : -1, static_cast<std::int32_t>(*sentence)});
        } else if (sentence) {
            // A command and its operands, separated by spaces.
            std::vector<std::string_view> words;
            for (auto w : Lines::split(*line, ' ')) {
                if (!w.empty()) words.push_back(w);
            }
            const auto opcode = S::commandNumber(words[0]);
            if (!opcode) lines.fail(std::format("unknown command '{}'", words[0]));
            S::Command command{*opcode, {}};
            static const std::map<std::string_view, std::int32_t, std::less<>> kKinds = {
                {"temp", S::kTemp}, {"save", S::kSave}, {"quest", S::kQuest}, {"net", S::kNet}, {"msg", S::kMessage},
                {"x", S::kCharacterX}, {"y", S::kCharacterY},
            };
            static const std::map<std::string_view, std::int32_t, std::less<>> kOperators = {
                {"=", 0}, {"!", 1}, {"!=", 1}, {">", 2}, {"<", 3},
            };
            for (std::size_t i = 1; i < words.size(); ++i) {
                const auto w = words[i];
                if (const auto op = kOperators.find(w); op != kOperators.end()) {
                    command.operands.push_back({S::kOperator, op->second});
                } else if (w.front() == '@') {
                    references.push_back({*sentence, script.sentences[*sentence].size(), command.operands.size(), std::string(w.substr(1))});
                    command.operands.push_back({S::kOperator, 0});
                } else if (const auto colon = w.find(':'); colon != std::string_view::npos) {
                    const auto kind = kKinds.find(w.substr(0, colon));
                    if (kind == kKinds.end()) lines.fail(std::format("unknown operand '{}'", w));
                    const auto rest = w.substr(colon + 1);
                    const bool position = kind->second == S::kCharacterX || kind->second == S::kCharacterY;
                    command.operands.push_back({kind->second, position ? character(rest) : lines.toInt(rest)});
                } else {
                    command.operands.push_back({S::kConstant, character(w)});
                }
            }
            script.sentences[*sentence].push_back(std::move(command));
        } else if (keyValue(*line, key, value)) {
            if (key == "person") {
                // person=kind,x,y,direction,name
                const auto f = Lines::split(value, ',');
                if (f.size() != 5) lines.fail("person needs kind, x, y, direction and name");
                ScenarioControl::Placement p;
                p.group = Group::Person;
                p.kind = lines.toInt(f[0]);
                p.x = lines.toInt(f[1]);
                p.y = lines.toInt(f[2]);
                p.direction = lines.toInt(f[3]);
                p.name = f[4];
                p.rect[0] = p.rect[1] = -60;
                p.rect[2] = p.rect[3] = 60;
                place(std::move(p));
            } else if (key == "object") {
                // object=left,top,right,bottom,name: an unseen thing to click on
                const auto f = Lines::split(value, ',');
                if (f.size() != 5) lines.fail("object needs left, top, right, bottom and name");
                const int l = lines.toInt(f[0]), t = lines.toInt(f[1]), r = lines.toInt(f[2]), b = lines.toInt(f[3]);
                ScenarioControl::Placement p;
                p.group = Group::Object;
                p.x = (l + r) / 2;
                p.y = (t + b) / 2;
                p.rect[0] = l - p.x;
                p.rect[1] = t - p.y;
                p.rect[2] = r - p.x;
                p.rect[3] = b - p.y;
                p.direction = 8;
                p.name = f[4];
                place(std::move(p));
            } else if (key == "temp") {
                // temp=id,value: a TEMP flag's value when the scenario starts
                const auto v = lines.ints(value, 2);
                script.tempFlags.emplace_back(v[0], v[1]);
            } else {
                lines.fail(std::format("unknown key '{}'", key));
            }
        } else {
            lines.fail("expected key=value, a command, or a [section]");
        }
    }
    for (const auto& r : references) {
        const auto it = sentenceNumbers.find(r.name);
        if (it == sentenceNumbers.end()) throw FormatError(std::format("no sentence called '{}'", r.name));
        script.sentences[r.sentence][r.command].operands[r.operand].value = it->second;
    }
}

// [shop <list>] sections of sell=<class>,<code>[,<count>] lines.
ShopLists parseShops(std::span<const std::uint8_t> text) {
    Lines lines(text);
    ShopLists lists;
    std::vector<ShopLists::mapped_type::value_type>* list = nullptr;
    while (auto line = lines.next()) {
        if (isSection(*line)) {
            const auto words = Lines::split(sectionName(*line), ' ');
            if (words.size() != 2 || words[0] != "shop") lines.fail("expected [shop <list>]");
            const int number = lines.toInt(words[1]);
            if (lists.contains(number)) lines.fail("shop list defined twice");
            list = &lists[number];
            continue;
        }
        std::string_view key, value;
        if (!list || !keyValue(*line, key, value) || key != "sell") lines.fail("expected [shop <list>] then sell= lines");
        const auto f = Lines::split(value, ',');
        if (f.size() != 2 && f.size() != 3) lines.fail("expected sell=<class>,<code>[,<count>]");
        const auto cls = kClasses.find(f[0]);
        if (cls == kClasses.end()) lines.fail(std::format("unknown item class '{}'", f[0]));
        list->push_back({cls->second, lines.toInt(f[1]), f.size() == 3 ? lines.toInt(f[2]) : 1});
    }
    return lists;
}

// [item <class> <code>] sections of key=value lines; see docs/content.md.
ItemTable parseItems(std::span<const std::uint8_t> text) {
    Lines lines(text);
    ItemTable table;
    ItemDef* item = nullptr;
    // Up to `max` comma-separated numbers into `out`; the rest stay 0.
    auto numbers = [&](std::string_view value, std::span<std::int32_t> out) {
        const auto v = lines.ints(value);
        if (v.size() > out.size()) lines.fail(std::format("at most {} numbers", out.size()));
        std::ranges::copy(v, out.begin());
    };
    auto pair = [&](std::string_view value, std::int32_t& a, std::int32_t& b) {
        const auto v = lines.ints(value, 2);
        a = v[0];
        b = v[1];
    };
    while (auto line = lines.next()) {
        if (isSection(*line)) {
            const auto words = Lines::split(sectionName(*line), ' ');
            if (words.size() != 3 || words[0] != "item") lines.fail("expected [item <class> <code>]");
            const auto cls = kClasses.find(words[1]);
            if (cls == kClasses.end()) lines.fail(std::format("unknown item class '{}'", words[1]));
            const int code = lines.toInt(words[2]);
            if (table.find(cls->second, code)) lines.fail("item defined twice");
            item = &table.items.emplace_back();
            item->cls = cls->second;
            item->code = code;
            continue;
        }
        std::string_view key, value;
        if (!item || !keyValue(*line, key, value)) lines.fail("expected [item <class> <code>] then key=value lines");
        if (key == "name") {
            item->name = value;
        } else if (key == "unidentified") {
            item->unidentifiedName = value;
        } else if (key == "durability") {
            item->durability = lines.toInt(value);
        } else if (key == "subtype") {
            item->subtype = lines.toInt(value);
        } else if (key == "quality") {
            item->quality = lines.toInt(value);
        } else if (key == "size") {
            pair(value, item->width, item->height);
            if (item->width < 1 || item->height < 1 || item->width > 9 || item->height > 4) lines.fail("bad item size");
        } else if (key == "price") {
            item->price = lines.toInt(value);
        } else if (key == "icon") {
            pair(value, item->invBank, item->invPattern);
        } else if (key == "ground") {
            pair(value, item->groundFolder, item->groundPicture);
        } else if (key == "level") {
            pair(value, item->itemLevel, item->requiredLevel);
        } else if (key == "weight") {
            item->dropWeight = lines.toInt(value);
        } else if (key == "stats") {
            numbers(value, item->stats);
        } else if (key == "elements") {
            numbers(value, item->elements);
        } else if (key == "layer") {
            item->heroLayer = lines.toInt(value);
        } else if (key == "glow") {
            item->glowLayer = lines.toInt(value);
        } else if (key == "two-handed") {
            item->twoHandedFlag = lines.toInt(value) != 0;
        } else if (key == "effect") {
            numbers(value, item->effect);
        } else {
            lines.fail(std::format("unknown key '{}'", key));
        }
    }
    return table;
}

namespace {

const std::map<std::string_view, int, std::less<>> kEvents = {
    {"decide", AiControl::kDecide},          {"wander-done", AiControl::kWanderDone},
    {"retreat-done", AiControl::kRetreatDone}, {"approach-done", AiControl::kApproachDone},
    {"waiting", AiControl::kWaiting},        {"wandering", AiControl::kWandering},
    {"retreating", AiControl::kRetreating},  {"approaching", AiControl::kApproaching},
    {"animation-done", AiControl::kAnimationDone}, {"hit", AiControl::kHit},
};

const std::map<std::string_view, int, std::less<>> kActions = {
    {"wait", AiControl::kWait},     {"wander", AiControl::kWander},   {"attack", AiControl::kAttack},
    {"attack2", 3},                 {"attack3", 4},                   {"magic", AiControl::kMagic},
    {"magic2", 6},                  {"magic3", 7},                    {"stop", AiControl::kStop},
    {"retreat", AiControl::kRetreat}, {"approach", AiControl::kApproach}, {"home", AiControl::kGoHome},
};

constexpr int kEventCount = 18;

}  // namespace

// [list <name>], walk=<speed>, then
// "on=<event> <action> key=value ..." with keys priority, duration, weight,
// speed, pause, extra, hp=<min>..<max> and range=<min>..<max>.
AiControl parseAi(std::span<const std::uint8_t> text) {
    Lines lines(text);
    AiControl ai;
    AiControl::List* list = nullptr;
    auto range = [&](std::string_view v, std::int32_t& lo, std::int32_t& hi) {
        const auto dots = v.find("..");
        if (dots == std::string_view::npos) lines.fail("ranges are <min>..<max>");
        const auto a = trim(v.substr(0, dots)), b = trim(v.substr(dots + 2));
        lo = a.empty() ? -1 : lines.toInt(a);
        hi = b.empty() ? -1 : lines.toInt(b);
    };
    while (auto line = lines.next()) {
        if (isSection(*line)) {
            const auto name = sectionName(*line);
            if (!name.starts_with("list ")) lines.fail("expected [list <name>]");
            list = &ai.lists.emplace_back();
            list->name = trim(name.substr(5));
            list->events.resize(kEventCount);
            continue;
        }
        std::string_view key, value;
        if (!list || !keyValue(*line, key, value)) lines.fail("expected [list <name>] then key=value lines");
        if (key == "walk") {
            list->walkSpeed = lines.toInt(value);
            continue;
        }
        if (key != "on") lines.fail(std::format("unknown key '{}'", key));
        std::vector<std::string_view> words;
        for (auto w : Lines::split(value, ' ')) {
            if (!w.empty()) words.push_back(w);
        }
        if (words.size() < 2) lines.fail("expected on=<event> <action> ...");
        const auto event = kEvents.find(words[0]);
        if (event == kEvents.end()) lines.fail(std::format("unknown event '{}'", words[0]));
        const auto action = kActions.find(words[1]);
        if (action == kActions.end()) lines.fail(std::format("unknown action '{}'", words[1]));
        AiControl::Entry e;
        e.action = action->second;
        e.param[2] = 1;  // weight
        e.cond[1] = e.cond[2] = e.cond[4] = e.cond[5] = -1;
        for (std::size_t i = 2; i < words.size(); ++i) {
            std::string_view k, v;
            if (!keyValue(words[i], k, v)) lines.fail("expected key=value after the action");
            if (k == "priority") e.param[0] = lines.toInt(v);
            else if (k == "duration") e.param[1] = lines.toInt(v);
            else if (k == "weight") e.param[2] = lines.toInt(v);
            else if (k == "speed") e.param[3] = lines.toInt(v);
            else if (k == "pause") e.param[4] = lines.toInt(v);
            else if (k == "extra") e.param[5] = lines.toInt(v);
            else if (k == "hp") {
                e.cond[0] = 1;
                range(v, e.cond[1], e.cond[2]);
            } else if (k == "range") {
                e.cond[3] = 1;
                range(v, e.cond[4], e.cond[5]);
            } else {
                lines.fail(std::format("unknown key '{}'", k));
            }
        }
        list->events[static_cast<std::size_t>(event->second)].push_back(e);
    }
    return ai;
}

PackInfo parsePackInfo(std::span<const std::uint8_t> text) {
    Lines lines(text);
    PackInfo info;
    while (auto line = lines.next()) {
        std::string_view key, value;
        if (!keyValue(*line, key, value)) lines.fail("expected key=value");
        if (key == "name") {
            info.name = value;
        } else if (key == "start") {
            info.startMap = value;
        } else if (key == "projection") {
            const auto v = lines.ints(value, 2);
            if (v[0] <= 0 || v[1] <= 0 || v[0] > 1000 || v[1] > 1000) lines.fail("bad projection");
            info.projection = {v[0], v[1]};
        } else {
            lines.fail(std::format("unknown key '{}'", key));
        }
    }
    return info;
}

}  // namespace sf::open

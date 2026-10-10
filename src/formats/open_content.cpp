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
        const int direction = lines.toInt(key);
        if (direction < 0 || direction >= 8) lines.fail("directions are 0 to 7");
        auto& dir = chart->directions[direction];
        AnimationSet::Layer layer;
        for (int frame : lines.ints(value)) {
            AnimationSet::Cell cell;
            cell.pattern = frame;
            layer.push_back(cell);
        }
        if (layer.size() > 4096) lines.fail("too many frames");
        dir.frameCount = static_cast<std::int16_t>(layer.size());
        dir.layers.assign(1, std::move(layer));
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
            // person=kind,x,y,direction
            const bool enemy = key == "enemy";
            auto fields = Lines::split(value, ',');
            if (fields.size() != (enemy ? 8u : 4u)) lines.fail(enemy ? "enemy needs 8 fields" : "person needs 4 fields");
            ScenarioControl::Placement p;
            p.group = enemy ? Group::Enemy : Group::Person;
            p.id = nextId[static_cast<int>(p.group)]++;
            p.kind = lines.toInt(fields[0]);
            p.x = lines.toInt(fields[1]);
            p.y = lines.toInt(fields[2]);
            p.direction = lines.toInt(fields[3]);
            // A collision box of about one walkability cell.
            p.rect[0] = p.rect[1] = -60;
            p.rect[2] = p.rect[3] = 60;
            if (enemy) {
                p.stats.hp = lines.toInt(fields[4]);
                p.stats.attack = lines.toInt(fields[5]);
                const int r = lines.toInt(fields[6]);
                p.stats.area[0] = p.stats.area[1] = -r;
                p.stats.area[2] = p.stats.area[3] = r;
                p.stats.ai = fields[7];
            }
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

// Tests for the open content pack readers (src/formats/open_content.h).
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "formats/binary_reader.h"
#include "formats/open_content.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

std::vector<std::uint8_t> bytes(std::string_view text) { return {text.begin(), text.end()}; }

void expectRejected(const char* what, std::string_view text, const std::function<void(std::vector<std::uint8_t>&)>& parse) {
    auto data = bytes(text);
    try {
        parse(data);
        check(false, what);
    } catch (const sf::FormatError&) {
    }
}

// A 2x2 RGBA PNG: red, green / half-transparent blue, clear.
const std::vector<std::uint8_t> kPng = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d,
    0x24, 0x00, 0x00, 0x00, 0x17, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x05, 0xc1, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x82, 0x20, 0xa6, 0xf7, 0xdc, 0x40, 0x24, 0xa1, 0xc1, 0x01, 0x3d, 0x57, 0x05, 0x7c,
    0x16, 0xc2, 0x3f, 0xbd, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

void testImage() {
    const auto image = sf::open::decodeImage(kPng);
    check(image.width == 2 && image.height == 2, "png size");
    check(image.argb[0] == 0xffff0000u && image.argb[1] == 0xff00ff00u, "png first row");
    check(image.argb[2] == 0x800000ffu && image.argb[3] >> 24 == 0, "png alpha");
    const std::vector<std::uint8_t> notPng = {1, 2, 3, 4};
    try {
        sf::open::decodeImage(notPng);
        check(false, "garbage is not an image");
    } catch (const sf::FormatError&) {
    }
}

void testSheet() {
    const auto sheet = sf::open::parseSheet(bytes("# comment\nimage=a.png\nframe=1,0,1,2,0,2  # bottom centre\n"));
    check(sheet.image == "a.png" && sheet.frames.size() == 1, "sheet frames");
    const auto bank = sf::open::makeSpriteBank(sheet, sf::open::decodeImage(kPng));
    check(bank.parts.size() == 1 && bank.parts[0].bpp == 32, "sheet makes a full-colour part");
    check(bank.parts[0].argb == std::vector<std::uint32_t>{0xff00ff00u, 0u}, "sheet crops its frame");
    int x, y, w, h;
    check(bank.bounds(bank.patterns[0], x, y, w, h) && x == 0 && y == -2 && w == 1 && h == 2, "sheet anchor");
    const auto named = sf::open::parseSheet(bytes("image=a.png\nframe=1,0,1,2,0,2,well\n"));
    check(named.frames.size() == 1 && named.frames[0].name == "well", "sheet frame name");
    check(sf::open::makeSpriteBank(named, sf::open::decodeImage(kPng)).names == std::vector<std::string>{"well"},
          "frame names reach the sprite bank");
    const auto outside = sf::open::parseSheet(bytes("image=a.png\nframe=1,1,2,2,0,0\n"));
    try {
        sf::open::makeSpriteBank(outside, sf::open::decodeImage(kPng));
        check(false, "frame outside the image");
    } catch (const sf::FormatError&) {
    }
    expectRejected("sheet without image", "frame=0,0,1,1,0,0\n", [](auto& d) { sf::open::parseSheet(d); });
    expectRejected("sheet bad number", "image=a.png\nframe=0,0,x,1,0,0\n", [](auto& d) { sf::open::parseSheet(d); });
}

void testAnimation() {
    const auto anim = sf::open::parseAnimation(bytes(
        "sheet=body.sheet\n"
        "[chart 0 loop]\n0=0,1,2\n2=3,4\n"
        "[chart 3 once]\n0=5\n"));
    const auto& charts = anim.set.charts;
    check(anim.sheet == "body.sheet" && charts.size() == 4, "animation charts");
    check(charts[0].status == 1 && charts[3].status == 0, "animation loop flags");
    check(charts[0].directions[0].frameCount == 3 && charts[0].directions[2].layers[0][1].pattern == 4, "animation frames");
    check(charts[0].directions[5].frameCount == 3, "missing direction borrows direction 0");
    check(charts[1].directions.empty(), "unused chart stays empty");
    // Several layers per direction: body, then equipment by layer number,
    // with per-line and per-frame priorities and '-' for no picture.
    const auto layered = sf::open::parseAnimation(bytes(
        "sheet=hero.sheet\n"
        "[chart 0 loop]\n0@20=0,1,2\n0.5@10=3,4,5\n0.9=6@3,-,7@15\n"));
    const auto& dir = layered.set.charts[0].directions[0];
    check(dir.layers.size() == 10 && dir.frameCount == 3, "layered animation size");
    check(dir.layers[0][2].pattern == 2 && dir.layers[0][2].priority == 20, "body layer and its priority");
    check(dir.layers[5][1].pattern == 4 && dir.layers[5][1].priority == 10, "equipment layer by number");
    check(dir.layers[7].empty(), "layers not given stay empty");
    check(dir.layers[9][0].priority == 3 && dir.layers[9][1].pattern == -1 && dir.layers[9][2].priority == 15,
          "per-frame priority and missing frames");
    check(layered.set.charts[0].directions[4].layers.size() == 10, "layered direction borrowed");
    check(charts[0].directions[0].layers.size() == 1 && charts[0].directions[0].layers[0][0].priority == 0,
          "single-layer files are unchanged");
    expectRejected("layer 32", "sheet=a\n[chart 0 loop]\n0.32=1\n", [](auto& d) { sf::open::parseAnimation(d); });
    expectRejected("bad priority", "sheet=a\n[chart 0 loop]\n0=1@x\n", [](auto& d) { sf::open::parseAnimation(d); });
    expectRejected("direction 8","sheet=a\n[chart 0 loop]\n8=1\n", [](auto& d) { sf::open::parseAnimation(d); });
    expectRejected("frames outside a chart", "sheet=a\n0=1\n", [](auto& d) { sf::open::parseAnimation(d); });
}

void testMap() {
    const auto map = sf::open::parseMap(bytes(
        "size=3,2\ntiles=480\ncell=160,160\njudge=0,0,4,2\nbank=Map/a.sheet\n"
        "[ground]\n0:1 . 0:2\n0:3 0:4 0:5\n"
        "[judge]\n.x..\n...x\n"
        "[objects]\n240,240,0,7\n"));
    const auto& g = map.ground;
    check(g.width == 3 && g.height == 2 && g.isoTile == 480, "map size");
    check(g.at(0, 0).pattern == 1 && g.at(1, 0).bank == -1 && g.at(2, 1).pattern == 5, "map ground");
    check(g.walkable(10, 10) && !g.walkable(170, 10) && !g.walkable(490, 170), "map judge");
    check(!g.walkable(-10, 10), "off the judge grid is blocked");
    check(map.banks.size() == 1 && map.objects.objects.size() == 1 && map.objects.objects[0].pattern == 7, "map objects");
    expectRejected("ground before size", "[ground]\n0:1\n", [](auto& d) { sf::open::parseMap(d); });
    expectRejected("ground too wide", "size=1,1\n[ground]\n0:1 0:1\n", [](auto& d) { sf::open::parseMap(d); });
    expectRejected("judge row width", "size=1,1\njudge=0,0,2,1\n[judge]\n.\n", [](auto& d) { sf::open::parseMap(d); });
}

void testScenario() {
    const auto s = sf::open::parseScenario(bytes(
        "title=Camp\nmap=camp\nentry=0,100,200,0\nentry=1,300,400,2\n"
        "enemy=1,500,600,4,30,4,480,goblin\nperson=2,10,20,0\n"
        "exit=0,0,960,480,2,1\n"));
    const auto& c = s.control;
    check(c.title == "Camp" && c.mapPath == "camp", "scenario header");
    check(c.entryPoints.size() == 2 && c.entryPoints[1].id == 4 && c.entryPoints[1].x == 300, "scenario entries");
    check(c.placements.size() == 3, "scenario placements");
    const auto& enemy = c.placements[0];
    check(enemy.group == sf::ScenarioControl::Group::Enemy && enemy.stats.hp == 30 && enemy.stats.ai == "goblin" &&
              enemy.stats.area[0] == -480,
          "scenario enemy");
    // The exit becomes an ON JUDGE trigger on an area running CHANGESCENARIO 2 1.
    const auto& area = c.placements[2];
    check(area.kind == -1 && area.rect[2] == 960, "exit area");
    check(s.script.triggers.size() == 1 && s.script.triggers[0].event == sf::ScenarioScript::kOnJudge &&
              s.script.triggers[0].character == sf::ScenarioControl::idBase(area.group) + area.id,
          "exit trigger");
    const auto& cmd = s.script.sentences.at(s.script.triggers[0].sentence).at(0);
    check(cmd.opcode == sf::ScenarioScript::kChangeScenario && cmd.operands[0].value == 2 && cmd.operands[1].value == 1,
          "exit sentence");
    expectRejected("enemy short", "enemy=1,2,3\n", [](auto& d) { sf::open::parseScenario(d); });

    const auto loot = sf::open::parseScenario(bytes("enemy=1,5,6,0,30,4,480,goblin,2,10,1,8\nitem=3,0,700,800\n"));
    const auto& l = loot.control.placements;
    check(l.size() == 2 && l[0].stats.level == 2 && l[0].stats.goldChance == 10 && l[0].stats.goldMax == 8, "enemy loot fields");
    check(l.size() == 2 && l[1].group == sf::ScenarioControl::Group::Item && l[1].itemClass == 3 && l[1].x == 700,
          "scenario item");
}

void testItems() {
    const auto table = sf::open::parseItems(bytes(
        "[item weapon 5]\nname=Great Sword\nsubtype=3\nsize=2,4\nicon=0,3\nground=7,3\nlevel=4,3\n"
        "stats=45,90\nlayer=19\nweight=10\n"
        "[item consumable 0]\nname=Potion\neffect=1,50\n"));
    check(table.items.size() == 2, "item count");
    const auto* sword = table.find(sf::ItemDef::Class::Weapon, 5);
    check(sword && sword->name == "Great Sword" && sword->width == 2 && sword->height == 4 && sword->twoHanded(), "item basics");
    check(sword && sword->invPattern == 3 && sword->groundFolder == 7 && sword->requiredLevel == 3 && sword->itemLevel == 4,
          "item pictures and levels");
    check(sword && sword->stats[0] == 45 && sword->stats[1] == 90 && sword->stats[2] == 0 && sword->heroLayer == 19, "item stats");
    const auto* potion = table.find(sf::ItemDef::Class::Consumable, 0);
    check(potion && potion->effect[1] == 50 && potion->width == 1, "item effect");
    expectRejected("item outside a section", "name=x\n", [](auto& d) { sf::open::parseItems(d); });
    expectRejected("item bad class", "[item hat 1]\n", [](auto& d) { sf::open::parseItems(d); });
    expectRejected("item twice", "[item weapon 1]\n[item weapon 1]\n", [](auto& d) { sf::open::parseItems(d); });
    expectRejected("item too big", "[item weapon 1]\nsize=10,1\n", [](auto& d) { sf::open::parseItems(d); });
    expectRejected("item too many stats", "[item weapon 1]\nstats=1,2,3,4,5,6,7,8,9,10,11\n", [](auto& d) { sf::open::parseItems(d); });
}

void testAi() {
    const auto ai = sf::open::parseAi(bytes(
        "[list goblin]\nwalk=40\n"
        "on=decide attack priority=2 range=..60\n"
        "on=decide approach weight=4 duration=30 speed=45 range=100..1800\n"
        "on=hit retreat hp=..30\n"));
    check(ai.lists.size() == 1 && ai.find("goblin") == 0 && ai.lists[0].walkSpeed == 40, "ai list");
    const auto& decide = ai.lists[0].events[sf::AiControl::kDecide];
    check(decide.size() == 2 && decide[0].action == sf::AiControl::kAttack && decide[0].param[0] == 2, "ai attack");
    check(decide[0].cond[3] == 1 && decide[0].cond[4] == -1 && decide[0].cond[5] == 60, "ai open range");
    check(decide[1].param[1] == 30 && decide[1].param[2] == 4 && decide[1].param[3] == 45 && decide[1].cond[4] == 100,
          "ai approach");
    const auto& hit = ai.lists[0].events[sf::AiControl::kHit];
    check(hit.size() == 1 && hit[0].cond[0] == 1 && hit[0].cond[1] == -1 && hit[0].cond[2] == 30, "ai health");
    expectRejected("ai unknown action", "[list a]\non=decide dance\n", [](auto& d) { sf::open::parseAi(d); });
    expectRejected("ai entry outside a list", "on=decide wait\n", [](auto& d) { sf::open::parseAi(d); });
}

void testPackInfo() {
    const auto info = sf::open::parsePackInfo(bytes("name=Demo\nstart=camp\nprojection=20,10\n"));
    check(info.name == "Demo" && info.startMap == "camp" && info.projection.x == 20 && info.projection.y == 10, "pack info");
    expectRejected("pack bad projection", "projection=0,10\n", [](auto& d) { sf::open::parsePackInfo(d); });
}

void testSkin() {
    const auto skin = sf::open::parseSkin(bytes(
        "props=Map/props.sheet\n"
        "[material grass]\ntiles=Map/ground.sheet,240\n"
        "[material sand]\ntiles=Map/s.sheet,16,2\ntile=480\n"
        "[ground 00F_Ground.njp]\ngrass=44,50,14\nsand=100,80,50\n"
        "[ground blk.njp]\nnone=\n"
        "[objects Bank.njp]\n0=tree1\n2-4,7=fence_x@-80,40 fence_x!@60,0\n9=-\n"));
    check(skin.props == "Map/props.sheet", "skin props sheet");
    check(skin.materials.at("grass").sheet == "Map/ground.sheet" && skin.materials.at("grass").first == 240 &&
              skin.materials.at("grass").period == 4 && skin.materials.at("grass").tile == 320,
          "skin material defaults");
    check(skin.materials.at("sand").period == 2 && skin.materials.at("sand").tile == 480, "skin material settings");
    const auto& ground = skin.ground.at("00f_ground.njp");
    check(ground.size() == 2 && ground[1].material == "sand" && ground[1].colour.r == 100 && ground[1].colour.b == 50,
          "skin ground colours, bank names in lower case");
    check(skin.ground.at("blk.njp").size() == 1 && skin.ground.at("blk.njp")[0].material == "none", "skin black ground");
    const auto& objects = skin.objects.at("bank.njp");
    check(objects.size() == 6 && objects.at(0).size() == 1 && objects.at(0)[0].prop == "tree1", "skin object rules");
    const auto& fence = objects.at(3);
    check(fence.size() == 2 && fence[0].dx == -80 && fence[0].dy == 40 && !fence[0].mirror && fence[1].mirror &&
              fence[1].prop == "fence_x" && fence[1].dx == 60 && objects.contains(7) && !objects.contains(5),
          "skin pieces, ranges and mirroring");
    check(objects.at(9).empty(), "skin - hides a piece");
    expectRejected("skin unknown section", "[thing a]\n", [](auto& d) { sf::open::parseSkin(d); });
    expectRejected("skin bad colour", "[ground a]\ngrass=1,2,300\n", [](auto& d) { sf::open::parseSkin(d); });
    expectRejected("skin bad range", "[objects a]\n5-2=tree\n", [](auto& d) { sf::open::parseSkin(d); });
    expectRejected("skin bad offset", "[objects a]\n1=tree@3\n", [](auto& d) { sf::open::parseSkin(d); });
}

}  // namespace

int main() {
    testImage();
    testSheet();
    testAnimation();
    testMap();
    testScenario();
    testItems();
    testAi();
    testPackInfo();
    testSkin();
    if (failures == 0) std::puts("all open content tests passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

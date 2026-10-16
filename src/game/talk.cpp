// Talking to people, shops and the rest of what scenario scripts do.

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>
#include <vector>

#include "core/log.h"
#include "game/game.h"
#include "render/tiny_font.h"

namespace sf {

namespace {

constexpr int kCell = 32;
// The shop window, where the original draws it: Status.njp patterns 7 and
// 8 (its top and bottom halves) on the left of the screen, with the 9 x 10
// cells of goods from (16, 72).
constexpr SDL_Rect kShopPanel{0, 0, 320, 416};
constexpr int kShopX = 16, kShopY = 72;
constexpr SDL_Color kWhite{235, 230, 215, 255};
constexpr SDL_Color kGold{255, 210, 80, 255};
constexpr SDL_Color kGrey{140, 135, 125, 255};
constexpr int kLineHeight = 14;
constexpr std::size_t kMaxLine = 72;

bool inside(const SDL_Rect& r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

void frame(SDL_Renderer* renderer, const SDL_Rect& r, SDL_Color fill, SDL_Color edge) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &r);
    SDL_SetRenderDrawColor(renderer, edge.r, edge.g, edge.b, edge.a);
    SDL_RenderDrawRect(renderer, &r);
}

int directionTowards(double dx, double dy) {
    // World x runs down-right on screen and y down-left; 0 = south, then
    // counter-clockwise.
    const double sx = dx - dy, sy = (dx + dy) * 2.0 / 3.0;
    const double angle = std::atan2(sx, sy);  // 0 = down the screen, positive = right
    int d = static_cast<int>(std::lround(angle / (3.14159265358979 / 4)));
    return (d + 8) % 8;
}

}  // namespace

// The original's messages are Shift-JIS. Its font draws every
// double-byte character (fontLeadByte), so they are kept, but the English
// release's wide space and middle dot are written half-width, as its
// half-width font lines them up.
std::string toFontText(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (!fontLeadByte(c)) {
            out += static_cast<char>(c);
            continue;
        }
        if (i + 1 >= text.size()) break;  // a lead byte with nothing after it
        const auto next = static_cast<unsigned char>(text[i + 1]);
        // Full width is two half-width cells; 0xa5 is the original
        // font's half-width middle dot.
        if (c == 0x81 && next == 0x40) out += "  ";
        else if (c == 0x81 && next == 0x45) out += "\xa5 ";
        else out += text.substr(i, 2);
        ++i;
    }
    return out;
}

// What the scenario scripts ask of the game.
class GameScript : public ScriptHost {
public:
    // `character` is whose event runs the script (an object, a person, or
    // -1 for none).
    GameScript(Game& game, std::int32_t character) : g_(game), character_(character) {}

    void talk(const Talk& t) override { g_.openDialogue(t); }
    void initTrade(int shop, int list) override {
        if (!g_.tables_.tables.empty()) {
            g_.shops_[shop] = stockShop(g_.tables_, g_.items_, list, g_.world_.random);
            return;
        }
        const auto goods = g_.openShops_.find(list);
        if (goods == g_.openShops_.end()) return;
        auto& stock = g_.shops_[shop];
        stock.goods.clear();
        for (const auto& g : goods->second) {
            const ItemDef* def = g_.items_.find(g.cls, g.code);
            if (!def) continue;
            for (int k = 0; k < g.count; ++k) stock.add(Item::make(def, g_.world_.random));
        }
    }
    void trade(int shop) override { g_.openTrade(shop); }
    void healLife() override {
        g_.playerHp_ = g_.playerMaxHp();
        // It fills the companion's health too, but does not bring it back.
        if (g_.partnerAlive()) g_.partner_->hp = g_.partner_->stats.maxHp;
    }
    void createItem(int cls, int code, int x, int y, int amount) override {
        const ItemDef* def = g_.items_.find(static_cast<ItemDef::Class>(cls), code);
        if (!def) return;
        if (!def->isGold()) {
            g_.dropItem(Item::make(def, g_.world_.random), x, y);
            return;
        }
        // Ours: the Debug tab's gold rate raises gold from objects (chests,
        // pots, barrels), not the rewards people hand over.
        if (fromObject()) amount = scaleGold(amount, g_.goldRate());
        // Gold comes in piles of at most 10,000, spread out a little.
        for (const int pile : goldPiles(amount, Inventory::kGoldStack)) {
            g_.dropItem({def, pile}, x + g_.world_.random.next(200) - 100, y + g_.world_.random.next(200) - 100);
        }
    }
    void createItemTable(int list, int x, int y) override { g_.dropFromList(list, x, y); }
    // x: and y: of a character the scenario does not have are -1, as in
    // the exe (0x4346b0). Some scripts name trap vents their map lacks,
    // such as 10057004 in Vaporous Forest; those shots go off at (-1, -1)
    // and hurt no one.
    std::int32_t characterX(std::int32_t id) const override {
        if (const auto at = g_.characterAt(id)) return static_cast<std::int32_t>(at->first);
        return -1;
    }
    std::int32_t characterY(std::int32_t id) const override {
        if (const auto at = g_.characterAt(id)) return static_cast<std::int32_t>(at->second);
        return -1;
    }
    void stopCharacter(std::int32_t id, bool stop) override {
        auto* a = g_.actorById(id);
        if (!a) return;
        a->stopped = stop;
        // Either ends a scripted action: they stand again (0x45d480).
        if (a->scripted && !a->hostile()) {
            a->scripted.reset();
            a->character.play(chart::kIdle);
        }
    }
    void requestAction(const ActionRequest& r) override { g_.requestAction(r); }
    void openChangeColor() override { g_.openChangeColor(); }
    void toggleCardGame() override { g_.toggleCardGame(); }
    int cardGameResult() const override { return g_.cardGame_.result(); }
    void faceCharacter(std::int32_t id, std::int32_t towards) override {
        auto* a = g_.actorById(id);
        if (!a || !g_.player_) return;
        // 0 turns them to the player.
        double tx = g_.player_->x, ty = g_.player_->y;
        if (const auto* t = towards ? g_.actorById(towards) : nullptr) {
            tx = t->character.x;
            ty = t->character.y;
        }
        a->character.direction = directionTowards(tx - a->character.x, ty - a->character.y);
    }
    void setActive(std::int32_t id, bool active) override {
        if (auto* a = g_.actorById(id)) a->active = active;
    }
    void setObjectStates(std::int32_t id, int shown, int usable, int blocks) override {
        if (auto* o = g_.sceneObject(id)) o->fixed = std::array{shown, usable, blocks};
    }
    bool workingObject(std::int32_t id) const override { return g_.workingObject(id); }
    void setUnlockSwitch(bool on) override { g_.unlockSwitch_ = on; }
    int playerLevel() const override { return g_.playerLevel_; }
    int offeredClass() const override { return static_cast<int>(g_.career_.offered); }
    int playerSex() const override { return static_cast<int>(g_.heroSex_); }
    std::int64_t gold() const override { return g_.inventory_.gold(); }
    void payGold(std::int64_t amount) override { g_.inventory_.addGold(-static_cast<int>(std::min<std::int64_t>(amount, g_.inventory_.gold()))); }
    int random(int n) override { return g_.world_.random.next(n); }
    void showQuestName(int quest) override { g_.showMissionName(quest); }
    void showAreaName(const std::string& name) override { g_.showAreaName(toFontText(name)); }
    int entryPoint() const override { return g_.entryPoint_; }
    void openStorage(bool giant) override { trade(giant ? Game::kGiantStorage : Game::kStorage); }
    void identifyItems() override { g_.inventory_.identifyAll(); }
    bool hasUnidentifiedItems() const override { return g_.inventory_.hasUnidentified(); }
    void repair(int part) override { g_.inventory_.repair(part); }
    int repairPrice(int part) const override { return g_.inventory_.repairPrice(part, g_.modifierPrices_); }
    void showLabel(const Label& l) override { g_.showLabel(l); }
    int partnerKind() const override { return g_.partnerRecord_.kind; }
    void changePartner(int kind) override { g_.changePartner(kind); }
    bool partnerLife(int& hp, int& max) const override {
        if (!g_.partner_) return false;
        const bool alive = g_.partnerAlive();
        hp = alive ? g_.partner_->hp : -1;
        max = alive ? g_.partner_->stats.maxHp : -1;
        return true;
    }
    std::string partnerStatus(int kind) const override { return g_.partnerStatusText(kind); }
    std::optional<bool> enemyAlive(std::int32_t id) const override {
        if (const auto* a = g_.actorById(id)) return a->hostile() ? std::optional(a->alive()) : std::nullopt;
        // A monster the engine could not spawn still counts, as alive.
        for (const auto& p : g_.world_.control.placements) {
            if (p.group == ScenarioControl::Group::Enemy && ScenarioControl::idBase(p.group) + p.id == id) return true;
        }
        return std::nullopt;
    }
    bool hasItem(int cls, int code) const override { return g_.inventory_.has(static_cast<ItemDef::Class>(cls), code); }
    void deleteItem(int cls, int code) override { g_.inventory_.removeOne(static_cast<ItemDef::Class>(cls), code); }
    void giveItem(int cls, int code) override {
        const ItemDef* def = g_.items_.find(static_cast<ItemDef::Class>(cls), code);
        if (!def || g_.inventory_.has(def->cls, code)) return;
        const Item item = Item::make(def, g_.world_.random);
        // SETITEMDIRECT is for items kept in the special items window
        // (0x430ffe case 0x4b); anything else goes in the bag, or on the
        // ground when it is full.
        if (def->specialPage >= 0) {
            g_.inventory_.addSpecial(item);
        } else if (!g_.inventory_.add(item) && g_.player_) {
            g_.dropItem(item, g_.player_->x, g_.player_->y);
        }
    }
    void playerLife(int& hp, int& max) const override {
        hp = g_.playerHp_;
        max = g_.playerMaxHp();
    }
    void playerMental(int& mp, int& max) const override {
        mp = g_.playerMp_;
        max = g_.playerMaxMp();
    }
    void giveExperiencePercent(int percent) override {
        g_.gainExperience(static_cast<int>(static_cast<std::int64_t>(g_.levels_.need(g_.playerLevel_)) * percent / 100));
    }
    void skippedCommand(std::int32_t opcode) override {
        // Once per scenario and command, so a playthrough log lists what
        // each area still lacks without a line every frame.
        if (g_.world_.skippedCommands.insert(opcode).second) {
            log::warn("Script command {} ({}) in {} is not carried out yet", ScenarioScript::commandName(opcode), opcode, g_.world_.scenarioDir);
        }
    }
    void fireTrap(const TrapFire& f) override { g_.fireTrap(f); }
    void createEffect(int number, int x, int y, int height, int direction) override { g_.createScriptEffect(number, x, y, height, direction, -1); }
    void createEffectOn(int number, std::int32_t character) override {
        // It stands where the character is and stays on them (0x433409).
        if (const auto at = g_.characterAt(character)) g_.createScriptEffect(number, at->first, at->second, 0, 8, character);
    }
    bool nearestPlayer(std::int32_t character, int min, int max, int& player, int& x, int& y) const override {
        // Single play: the hero is the only player (0x430b30).
        const auto p = g_.placementById(character);
        const auto at = g_.positionOf(character);
        if (!p || !at || !g_.player_ || g_.playerHp_ <= 0) return false;
        const JudgeRect rect{p->rect[0], p->rect[1], p->rect[2], p->rect[3]};
        const int gap = rectGap(static_cast<int>(g_.player_->x), static_cast<int>(g_.player_->y), kHeroJudge, static_cast<int>(at->first),
                                static_cast<int>(at->second), rect);
        if ((min != -1 && gap < min) || (max != -1 && gap > max)) return false;
        player = 0;
        x = static_cast<int>(g_.player_->x);
        y = static_cast<int>(g_.player_->y);
        return true;
    }
    int playerDistance(std::int32_t character) const override {
        const auto p = g_.placementById(character);
        const auto at = g_.positionOf(character);
        if (!p || !at || !g_.player_) return -1;
        const JudgeRect rect{p->rect[0], p->rect[1], p->rect[2], p->rect[3]};
        return rectGap(static_cast<int>(g_.player_->x), static_cast<int>(g_.player_->y), kHeroJudge, static_cast<int>(at->first),
                       static_cast<int>(at->second), rect);
    }
    void resurrect(std::int32_t enemy, int x, int y, int direction) override { g_.resurrect(enemy, x, y, direction); }
    // The object's opacity at + 0xf4, which 0x45ddd0 draws it with.
    void setObjectOpacity(std::int32_t object, int opacity) override {
        for (auto& p : g_.world_.control.placements) {
            if (p.group == ScenarioControl::Group::Object && ScenarioControl::idBase(p.group) + p.id == object) p.look.opacity = opacity;
        }
    }
    std::optional<int> objectOpacity(std::int32_t object) const override {
        const auto p = g_.placementById(object);
        if (!p || p->group != ScenarioControl::Group::Object) return std::nullopt;
        return p->look.opacity;
    }
    void startVisual(int picture) override { g_.startVisual(picture); }
    void rain(int r, int g, int b, int count) override { g_.addRain(r, g, b, count); }
    void playSound(int voice, bool everywhere, int x, int y) override {
        if (!g_.player_) return;
        if (x == -1) {
            g_.playSound(voice, g_.player_->x, g_.player_->y, everywhere);
        } else {
            g_.playSound(voice, x, y, everywhere);
        }
    }
    void memorizeSpell(int spell) override {
        // The original only marks it known (player + 0x1440 = 3) and casts
        // it at level 1 at the least; ours records it as level 1.
        if (spell < 0 || spell >= kSpellCount || g_.spells_.knows(spell)) return;
        g_.spells_.level[static_cast<std::size_t>(spell)] = 1;
        log::info("{} memorized", g_.magic_.spell(spell).name);
    }
    bool knowsSpell(int spell) const override { return g_.spells_.knows(spell); }
    void openWarpGate(int gate) override { g_.openWarpGate(gate); }
    void closeWarpGate(int gate) override { g_.closeWarpGate(gate); }
    void offerClass(int cls) override {
        g_.career_.offered = static_cast<HeroClass>(cls);
        log::info("{} offered", className(g_.career_.offered, g_.heroSex_));
    }

private:
    bool fromObject() const {
        using C = ScenarioControl;
        return character_ >= C::idBase(C::Group::Object) && character_ < C::idBase(C::Group::Person);
    }

    Game& g_;
    std::int32_t character_;
};

const Actor* Game::actorById(std::int32_t id) const {
    for (const auto& a : world_.actors) {
        if (a.gameId() == id) return &a;
    }
    return nullptr;
}

Actor* Game::actorById(std::int32_t id) { return const_cast<Actor*>(std::as_const(*this).actorById(id)); }

std::optional<std::pair<double, double>> Game::positionOf(std::int32_t id) const {
    if (const auto* a = actorById(id)) return std::pair(a->character.x, a->character.y);
    for (const auto& p : world_.control.placements) {
        if (ScenarioControl::idBase(p.group) + p.id == id) return std::pair(double(p.x), double(p.y));
    }
    return std::nullopt;
}

const ScenarioControl::Placement* Game::placementById(std::int32_t id) const {
    for (const auto& p : world_.control.placements) {
        if (ScenarioControl::idBase(p.group) + p.id == id) return &p;
    }
    return nullptr;
}

std::optional<std::size_t> Game::objectAt(float screenX, float screenY) const {
    return objectAt(screenX, screenY, true);
}

std::optional<std::size_t> Game::usableObjectAt(float screenX, float screenY) const {
    return objectAt(screenX, screenY, false);
}

std::optional<std::size_t> Game::objectAt(float screenX, float screenY, bool talks) const {
    if (!world_.script) return std::nullopt;
    double wx, wy;
    screenToWorld(pixelX(screenX), pixelY(screenY), wx, wy);
    for (std::size_t i = 0; i < world_.control.placements.size(); ++i) {
        const auto& p = world_.control.placements[i];
        if (p.group != ScenarioControl::Group::Object) continue;
        if (wx < p.x + p.rect[0] || wx > p.x + p.rect[2] || wy < p.y + p.rect[1] || wy > p.y + p.rect[3]) continue;
        const std::int32_t id = ScenarioControl::idBase(p.group) + p.id;
        // Only what is shown and usable answers a click (0x45e080).
        const auto* o = sceneObject(id);
        if (o && (!objectState(*o, kShown) || !objectState(*o, kUsable))) continue;
        if (!talks) return i;
        for (const auto& t : world_.script->triggers) {
            if (t.event == ScenarioScript::kOnTalk && t.character == id) return i;
        }
    }
    return std::nullopt;
}

// A newer label on the same character replaces the old one.
void Game::showLabel(const ScriptHost::Label& label) {
    std::erase_if(labels_, [&](const Label& l) { return l.label.character == label.character; });
    labels_.push_back({label, std::max(1, label.duration * kTicksPerSecond / 1000)});
}

void Game::renderLabels() {
    for (const auto& l : labels_) {
        const auto at = positionOf(l.label.character);
        if (!at) continue;
        int sx, sy;
        worldToScreen(static_cast<int>(at->first), static_cast<int>(at->second), sx, sy);
        // Messages end in line breaks; each line is centred.
        std::vector<std::string> lines;
        std::string line;
        for (const char c : toFontText(l.label.text) + '\n') {
            if (c != '\n') {
                line += c;
            } else if (!line.empty()) {
                lines.push_back(std::exchange(line, {}));
            }
        }
        const SDL_FPoint p = onScreen(sx + l.label.dx, sy + l.label.dy);
        int y = static_cast<int>(p.y);
        for (const auto& text : lines) {
            const int x = static_cast<int>(p.x) - uiTextWidth(text) / 2;
            uiText(x + 1, y + 1, text, {0, 0, 0, 255});  // a shadow, as the exe draws text
            uiText(x, y, text, {l.label.r, l.label.g, l.label.b, 255});
            y += kLineHeight;
        }
    }
}

void Game::runScript(std::int32_t sentence, std::int32_t character) {
    if (!world_.script || pendingScenario_) return;
    GameScript host(*this, character);
    ScriptRunner runner(*world_.script, world_.flags, &host, character);
    runner.setRules(rules_);
    if (const auto change = runner.run(sentence)) {
        pendingScenario_.emplace(change->scenario, change->entry);
    }
}

void Game::runEvent(std::int32_t event, std::int32_t character) {
    if (!world_.script) return;
    for (const auto& t : world_.script->triggers) {
        if (t.event != event || (character != -1 && t.character != character)) continue;
        runScript(t.sentence, t.character);
    }
}

std::optional<std::size_t> Game::personAt(float screenX, float screenY) const {
    std::optional<std::size_t> best;
    double bestDistance = INFINITY;
    for (std::size_t i = 0; i < world_.actors.size(); ++i) {
        const auto& a = world_.actors[i];
        if (a.hostile() || !a.active) continue;
        int sx, sy;
        worldToScreen(static_cast<int>(a.character.x), static_cast<int>(a.character.y), sx, sy);
        const SDL_FPoint at = onScreen(sx, sy - 40);
        const double dx = at.x - screenX, dy = at.y - screenY;
        const double d = std::hypot(dx, dy);
        if (inClickRange(dx, dy) && d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

// Walks up to a person; Game::tickPlayer calls this once they are close.
void Game::talkTo(Actor& person) {
    talkTarget_.reset();
    walkTarget_.reset();
    runEvent(ScenarioScript::kOnTalk, person.gameId());
}

void Game::talkToId(std::int32_t id) {
    for (std::size_t i = 0; i < world_.actors.size(); ++i) {
        if (world_.actors[i].gameId() != id) continue;
        if (player_) {
            player_->x = world_.actors[i].character.x + 150;
            player_->y = world_.actors[i].character.y + 150;
        }
        talkTarget_ = i;
        return;
    }
    for (std::size_t i = 0; i < world_.control.placements.size(); ++i) {
        const auto& p = world_.control.placements[i];
        if (p.group != ScenarioControl::Group::Object || ScenarioControl::idBase(p.group) + p.id != id) continue;
        if (player_) {
            player_->x = p.x + p.rect[2] + 100;
            player_->y = p.y + p.rect[3] + 100;
        }
        objectTarget_ = i;
        return;
    }
    log::warn("No character {} here", id);
}

void Game::openDialogue(const ScriptHost::Talk& talk) {
    Dialogue d;
    d.answer = talk.answer;
    d.speaker = talk.speaker;
    // Split into lines; ~text~ marks the options to choose from.
    std::string line;
    bool inOption = false;
    auto endLine = [&] {
        while (!line.empty() && line.back() == ' ') line.pop_back();
        d.lines.push_back(line);
        line.clear();
    };
    bool trail = false;  // the second byte of a double-byte character
    for (const char c : toFontText(talk.text)) {
        if (trail || fontLeadByte(static_cast<unsigned char>(c))) {
            // Never an option mark or a line break, though a trail byte can
            // be '~'.
            trail = !trail;
            line += c;
        } else if (c == '\n') {
            endLine();
        } else if (c == '~') {
            if (!inOption) {
                d.options.push_back({static_cast<int>(d.lines.size()), line.size(), line.size()});
            } else {
                auto& o = d.options.back();
                o.to = line.size();
                while (o.to > o.from && line[o.to - 1] == ' ') --o.to;
            }
            inOption = !inOption;
        } else {
            if (line.size() >= kMaxLine && c == ' ' && !inOption) {
                endLine();
                continue;
            }
            line += c;
        }
    }
    if (!line.empty()) endLine();
    while (!d.lines.empty() && d.lines.back().empty()) d.lines.pop_back();
    d.choice = talk.defaultChoice >= 0 && talk.defaultChoice < static_cast<int>(d.options.size()) ? talk.defaultChoice : 0;
    log::info("{} says: {}{}", talk.speaker, d.lines.empty() ? std::string() : d.lines[0], d.options.empty() ? "" : std::format(" ({} options)", d.options.size()));
    dialogue_ = std::move(d);
    walkTarget_.reset();
    mouseHeld_ = false;
}

bool Game::applyTalkRequest(const TalkRequest& r) {
    using K = TalkRequest::Kind;
    if (!r.valid()) return false;
    const auto index = static_cast<std::size_t>(r.index);
    switch (r.kind) {
        case K::TalkTo:
            // Someone here to talk to: not a monster, and not hidden.
            if (!player_ || dialogue_ || index >= world_.actors.size() || world_.actors[index].hostile() || !world_.actors[index].active) return false;
            talkTarget_ = index;
            attackTarget_.reset();
            return true;
        case K::UseObject: {
            // An object here that is shown and usable, as a click finds it.
            if (!player_ || dialogue_ || index >= world_.control.placements.size()) return false;
            const auto& p = world_.control.placements[index];
            if (p.group != ScenarioControl::Group::Object) return false;
            const auto* o = sceneObject(ScenarioControl::idBase(p.group) + p.id);
            if (o && (!objectState(*o, kShown) || !objectState(*o, kUsable))) return false;
            objectTarget_ = index;
            attackTarget_.reset();
            return true;
        }
        case K::Answer: {
            // One of the options shown, or none when the box has none.
            if (!dialogue_) return false;
            const int count = static_cast<int>(dialogue_->options.size());
            if (count ? r.index < 0 || r.index >= count : r.index != -1) return false;
            closeDialogue(r.index);
            return true;
        }
    }
    return false;
}

void Game::closeDialogue(int answer) {
    if (!dialogue_) return;
    const Dialogue d = std::move(*dialogue_);
    dialogue_.reset();
    if (!d.scripted) return;
    if (world_.script) {
        ScriptRunner(*world_.script, world_.flags).store(d.answer, answer);
    }
    runEvent(ScenarioScript::kOnTalkEnd, d.speaker);
}

// The original's half-width font: pattern 0 of Font00.njp is the
// character set as a 16 x 16 grid of 6 x 12 cells, by byte value.
const SpriteTexture* Game::uiFont() {
    const SpriteBank* bank = itemBank("System/Common/Pattern/Font00");
    if (!bank || bank->patterns.empty()) return nullptr;
    const SpriteTexture* t = sprites_.get(*bank, 0, false);
    return t && t->width >= 96 && t->height >= 192 ? t : nullptr;
}

// A double-byte character takes two half-width cells, so text is as wide
// as it is long either way.
int Game::uiTextWidth(std::string_view text) {
    return uiFont() ? static_cast<int>(text.size()) * 6 : textWidth(text);
}

void Game::uiText(int x, int y, std::string_view text, SDL_Color color) {
    const SpriteTexture* font = uiFont();
    if (!font) {
        std::string plain(text);
        std::ranges::replace(plain, '\xa5', '-');
        drawText(renderer_, x, y, plain, color);
        return;
    }
    fontText(*itemBank("System/Common/Pattern/Font00"), *font, x, y, text, color);
}

// Text in one of the original's fonts, as RKC_UPDIB.dll writes it
// (0x10001cce): a lead byte (fontLeadByte) and the byte after it are one
// character, the after byte's 12 x 12 cell of a 16 x 16 grid in pattern
// lead - 0x7f (0x80-0x9f) or lead - 0xbf (0xe0-0xff), two half cells wide.
// Fonts without those patterns (the open pack's) show a question mark.
void Game::fontText(const SpriteBank& bank, const SpriteTexture& half, int x, int y, std::string_view text, SDL_Color color) {
    SDL_SetTextureColorMod(half.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(half.texture, color.a);
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto c = static_cast<unsigned char>(text[i]);
        int width = 6;
        if (fontLeadByte(c) && i + 1 < text.size()) {
            const auto trail = static_cast<unsigned char>(text[++i]);
            const int pattern = c <= 0x9f ? c - 0x7f : c - 0xbf;
            const SpriteTexture* full =
                pattern < static_cast<int>(bank.patterns.size()) ? sprites_.get(bank, pattern, false) : nullptr;
            if (full && full->width >= 192 && full->height >= 192) {
                SDL_SetTextureColorMod(full->texture, color.r, color.g, color.b);
                SDL_SetTextureAlphaMod(full->texture, color.a);
                const SDL_Rect src{(trail % 16) * 12, (trail / 16) * 12, 12, 12};
                const SDL_Rect dst{x, y, 12, 12};
                SDL_RenderCopy(renderer_, full->texture, &src, &dst);
                SDL_SetTextureColorMod(full->texture, 255, 255, 255);
                SDL_SetTextureAlphaMod(full->texture, 255);
                x += 12;
                continue;
            }
            c = '?';
            width = 12;  // a missing character keeps its width
        }
        const SDL_Rect src{(c % 16) * 6, (c / 16) * 12, 6, 12};
        const SDL_Rect dst{x, y, 6, 12};
        SDL_RenderCopy(renderer_, half.texture, &src, &dst);
        x += width;
    }
    SDL_SetTextureColorMod(half.texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(half.texture, 255);
}

// Where the box goes: above the speaker, kept on screen.
SDL_Rect Game::dialogueBox() {
    const auto& d = *dialogue_;
    int ax = kScreenWidth / 2, ay = kScreenHeight / 2;
    if (const auto* a = actorById(d.speaker)) {
        worldToScreen(static_cast<int>(a->character.x), static_cast<int>(a->character.y), ax, ay);
        const SDL_FPoint at = onScreen(ax, ay - 110);
        ax = static_cast<int>(at.x);
        ay = static_cast<int>(at.y);
    }
    int w = 0;
    for (const auto& l : d.lines) w = std::max(w, uiTextWidth(l));
    w += 24;
    const int h = static_cast<int>(d.lines.size()) * kLineHeight + 18;
    SDL_Rect r{ax - w / 2, ay - h, w, h};
    r.x = std::clamp(r.x, 4, kScreenWidth - 4 - r.w);
    r.y = std::clamp(r.y, 4, kScreenHeight - 70 - r.h);
    return r;
}

SDL_Rect Game::dialogueOptionRect(const SDL_Rect& box, int option) {
    const auto& o = dialogue_->options[static_cast<std::size_t>(option)];
    const std::string& l = dialogue_->lines[static_cast<std::size_t>(o.line)];
    const int x = box.x + 12 + uiTextWidth(std::string_view(l).substr(0, o.from));
    const int y = box.y + 9 + o.line * kLineHeight;
    return {x - 3, y - 1, uiTextWidth(std::string_view(l).substr(o.from, o.to - o.from)) + 6, kLineHeight};
}

int Game::dialogueOptionAt(int x, int y) {
    if (!dialogue_) return -1;
    const SDL_Rect box = dialogueBox();
    for (int i = 0; i < static_cast<int>(dialogue_->options.size()); ++i) {
        if (inside(dialogueOptionRect(box, i), x, y)) return i;
    }
    return -1;
}

void Game::dialogueEvent(const SDL_Event& event) {
    auto& d = *dialogue_;
    const int count = static_cast<int>(d.options.size());
    switch (event.type) {
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
                case SDLK_UP:
                case SDLK_w:
                    if (count) d.choice = (d.choice + count - 1) % count;
                    break;
                case SDLK_DOWN:
                case SDLK_s:
                    if (count) d.choice = (d.choice + 1) % count;
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                    playSound(0x39);  // (0x458080)
                    applyTalkRequest(TalkRequest::answer(count ? d.choice : -1));
                    break;
                case SDLK_ESCAPE: applyTalkRequest(TalkRequest::answer(count ? count - 1 : -1)); break;
                default: break;
            }
            break;
        case SDL_MOUSEMOTION:
            if (const int o = dialogueOptionAt(event.motion.x, event.motion.y); o >= 0) d.choice = o;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (!count) {
                applyTalkRequest(TalkRequest::answer(-1));
            } else if (const int o = dialogueOptionAt(event.button.x, event.button.y); o >= 0) {
                playSound(0x39);  // (0x457fa0)
                applyTalkRequest(TalkRequest::answer(o));
            }
            break;
        default: break;
    }
}

void Game::renderDialogue() {
    if (!dialogue_) return;
    const auto& d = *dialogue_;
    const SDL_Rect box = dialogueBox();
    const SpriteBank* balloon = itemBank("System/Game/Pattern/Hukidasi");
    SDL_Color ink = kWhite, lit = kGold, dim = kGrey;
    if (balloon && balloon->patterns.size() >= 5) {
        // The original's speech balloon: white with a black edge, its
        // corners are patterns 0-3 (top left, bottom left, top right,
        // bottom right) and the edges between them repeat their last
        // row or column; pattern 4 is the tail pointing at the speaker.
        ink = {0, 0, 0, 255};
        // ShadowFlare.exe's option colours (0x457a40, drawn at 0x4572ef):
        // the chosen one dark red, the others grey.
        lit = {192, 0, 0, 255};
        dim = {128, 128, 128, 255};
        const SpriteTexture* c[4];
        for (int k = 0; k < 4; ++k) c[k] = sprites_.get(*balloon, k, false);
        if (c[0] && c[1] && c[2] && c[3]) {
            const int n = c[0]->width;  // 9
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(renderer_, 248, 248, 248, 255);
            const SDL_Rect inner{box.x + 4, box.y + 4, box.w - 8, box.h - 8};
            SDL_RenderFillRect(renderer_, &inner);
            auto blit = [&](const SpriteTexture* t, SDL_Rect src, SDL_Rect dst) { SDL_RenderCopy(renderer_, t->texture, &src, &dst); };
            const int right = box.x + box.w - n, bottom = box.y + box.h - n;
            blit(c[0], {0, 0, n, n}, {box.x, box.y, n, n});
            blit(c[1], {0, 0, n, n}, {box.x, bottom, n, n});
            blit(c[2], {0, 0, n, n}, {right, box.y, n, n});
            blit(c[3], {0, 0, n, n}, {right, bottom, n, n});
            blit(c[0], {n - 1, 0, 1, n}, {box.x + n, box.y, box.w - 2 * n, n});      // top
            blit(c[1], {n - 1, 0, 1, n}, {box.x + n, bottom, box.w - 2 * n, n});      // bottom
            blit(c[0], {0, n - 1, n, 1}, {box.x, box.y + n, n, box.h - 2 * n});      // left
            blit(c[2], {0, n - 1, n, 1}, {right, box.y + n, n, box.h - 2 * n});      // right
        }
        if (const SpriteTexture* t = sprites_.get(*balloon, 4, false); t && actorById(d.speaker)) {
            const SDL_Rect dst{box.x + box.w / 2 - t->width / 2, box.y + box.h - 4, t->width, t->height};
            SDL_RenderCopy(renderer_, t->texture, nullptr, &dst);
        }
    } else {
        frame(renderer_, box, {10, 12, 30, 215}, {170, 150, 100, 255});
    }
    for (std::size_t i = 0; i < d.lines.size(); ++i) {
        uiText(box.x + 12, box.y + 9 + static_cast<int>(i) * kLineHeight, d.lines[i], ink);
    }
    // Options over the text: the chosen one lit up, the others dimmed.
    for (int i = 0; i < static_cast<int>(d.options.size()); ++i) {
        const auto& o = d.options[static_cast<std::size_t>(i)];
        const std::string& l = d.lines[static_cast<std::size_t>(o.line)];
        const SDL_Rect r = dialogueOptionRect(box, i);
        uiText(r.x + 3, r.y + 1, std::string_view(l).substr(o.from, o.to - o.from), i == d.choice ? lit : dim);
    }
}

// The shop's half of the screen; the bag stays open on the right.
bool Game::shopClick(int x, int y, bool right) {
    using K = ItemRequest::Kind;
    if (!openShop_) return false;
    if (!inside(kShopPanel, x, y)) return false;
    if (openShop_ == kGiantStorage && !right && giantStorageHeaderClick(x, y)) return true;
    auto& stock = openStock();
    const SDL_Rect grid{kShopX, kShopY, ShopStock::kWidth * kCell, ShopStock::kHeight * kCell};
    const int cx = (x - kShopX) / kCell, cy = (y - kShopY) / kCell;
    if (storageOpen()) {
        // A warehouse: items are put down and picked up as in the bag.
        if (right) {
            // A right click off the cells leaves. On a stack of gold it asks
            // how much to take, and on a potion or other consumable it uses
            // it, as in the bag (0x44a9f0, 0x44ab60). Ours puts anything
            // else in the bag instead (Rules::rightClickStores).
            if (!inside(grid, x, y)) {
                if (!hand_) applyItemRequest(ItemRequest::of(K::CloseShop));
            } else if (const int i = stock.indexAt(cx, cy); !hand_ && i >= 0) {
                const auto& placed = stock.goods[static_cast<std::size_t>(i)];
                if (placed.item.def->isGold()) {
                    openGoldSplit(openShop_ == kGiantStorage ? GoldSplit::From::GiantStorage : GoldSplit::From::Storage, placed.x,
                                  placed.y, x, y);
                } else {
                    applyItemRequest(ItemRequest::at(K::FromStorage, cx, cy));
                }
            }
        } else if (hand_ && inside(grid, x, y)) {
            const double left = x - hand_->def->width * kCell / 2.0 - kShopX;
            const double top = y - hand_->def->height * kCell / 2.0 - kShopY;
            applyItemRequest(ItemRequest::at(K::StorageClick, static_cast<int>(std::lround(left / kCell)), static_cast<int>(std::lround(top / kCell))));
        } else if (inside(grid, x, y)) {
            applyItemRequest(ItemRequest::at(K::StorageClick, cx, cy));
        }
        return true;
    }
    if (hand_) {
        applyItemRequest(ItemRequest::of(K::SellHand));
        return true;
    }
    // A right click anywhere else in the window leaves the shop.
    const int i = inside(grid, x, y) ? stock.indexAt(cx, cy) : -1;
    if (right && i < 0) {
        applyItemRequest(ItemRequest::of(K::CloseShop));
        return true;
    }
    if (i < 0 || right) return true;
    applyItemRequest(ItemRequest::at(K::Buy, cx, cy));
    return true;
}

// Buys the item on a shop cell: it goes in the bag, the special items
// window or onto the mine count, if there is room and gold enough.
void Game::buyAt(int x, int y) {
    if (!openShop_ || storageOpen() || hand_) return;
    auto& stock = openStock();
    const int i = stock.indexAt(x, y);
    if (i < 0) return;
    const Item item = stock.goods[static_cast<std::size_t>(i)].item;
    const int price = buyPrice(item, modifierPrices_);
    if (inventory_.gold() < price) {
        showMessage("You're running short of money.");
        return;
    }
    // A mine goes onto the mine count, never the bag, and isn't sold while
    // the hero carries as many as they can (0x44b020, 0x446320 case 9).
    if (item.def->isMine()) {
        if (!pickUpMine()) {
            showMessage("No room for more mines");
            return;
        }
    } else if (item.def->specialPage >= 0) {
        // Into the special items window; not sold when one like it is
        // there already (0x4480c0).
        if (!inventory_.addSpecial(item)) {
            showMessage("You already have one");
            return;
        }
    } else if (!inventory_.add(item)) {
        showMessage("No room in the bag");
        return;
    }
    inventory_.addGold(-price);
    playSound(itemSound(*item.def, kItemTake));  // as the original takes it into the hand (0x4480c0)
    stock.goods.erase(stock.goods.begin() + i);
}

// Selling: the shop takes the item and pays a quarter of its value.
bool Game::sellHand() {
    if (!openShop_ || storageOpen() || !hand_) return false;
    if (hand_->def->quality == 3 || hand_->def->isGold()) {
        showMessage("Not for sale");
        return false;
    }
    const int price = sellPrice(*hand_, modifierPrices_) * std::max(1, hand_->count);
    // The gold goes in the bag; with no room for it there is no sale.
    if (!inventory_.addGold(price)) {
        showMessage("No room in the bag");
        return false;
    }
    playSound(itemSound(*hand_->def, kItemTake));  // (0x4480c0)
    shops_[*openShop_].add(*hand_);
    showMessage(std::format("Sold for {} gold", price));
    hand_.reset();
    return true;
}

// Takes the item up and puts it on the shop in one click; if the shop will
// not have it, it goes back where it was.
void Game::sellFromBag(int index) {
    if (hand_ || index < 0 || static_cast<std::size_t>(index) >= inventory_.bag().size()) return;
    const auto& placed = inventory_.bag()[static_cast<std::size_t>(index)];
    const int x = placed.x, y = placed.y;
    if (inventory_.clickBag(hand_, x, y) != Inventory::Result::Done || !hand_) return;
    if (!sellHand()) inventory_.clickBag(hand_, x, y);
}

// Takes the item up and puts it in the open warehouse's first free place
// in one click; with no room it goes back where it was.
void Game::storeFromBag(int index) {
    if (hand_ || !storageOpen() || index < 0 || static_cast<std::size_t>(index) >= inventory_.bag().size()) return;
    const auto& placed = inventory_.bag()[static_cast<std::size_t>(index)];
    const int x = placed.x, y = placed.y;
    if (inventory_.clickBag(hand_, x, y) != Inventory::Result::Done || !hand_) return;
    const ItemDef& held = *hand_->def;
    openStock().takeIn(hand_, !rules_.noEmptyGoldInWarehouse);
    if (hand_) {
        inventory_.clickBag(hand_, x, y);
        showMessage("The warehouse is full");
    } else {
        playSound(itemSound(held, kItemPut));
    }
}

// A warehouse item goes to the bag's first free place.
void Game::takeFromStorage(int index) {
    auto& stock = openStock();
    if (hand_ || index < 0 || static_cast<std::size_t>(index) >= stock.goods.size()) return;
    const Item item = stock.goods[static_cast<std::size_t>(index)].item;
    if (!inventory_.add(item)) {
        showMessage("No room in the bag");
        return;
    }
    stock.goods.erase(stock.goods.begin() + index);
    playSound(itemSound(*item.def, kItemTake));
}

void Game::closeShop() {
    if (!openShop_ || hand_) return;
    openShop_.reset();
    showInventory_ = false;
}

// The shop window's background: the open pack's Shop.sheet or the
// original's Status.njp patterns 7 and 8, at their own screen positions.
bool Game::drawShopPicture() {
    // The warehouse has its own copy of the window in the original
    // (patterns 14 and 15); the open pack uses the shop's.
    const bool storage = storageOpen();
    const SpriteBank* bank = itemBank("System/Game/Pattern/Shop");
    std::vector<int> patterns = {0};
    if (!bank) {
        bank = itemBank("System/Game/Pattern/Status");
        patterns = storage ? std::vector<int>{14, 15} : std::vector<int>{7, 8};
    }
    bool drawn = false;
    for (int pattern : patterns) {
        if (const SpriteTexture* sprite = bank ? sprites_.get(*bank, pattern, false) : nullptr) {
            const SDL_Rect dst{sprite->originX, sprite->originY, sprite->width, sprite->height};
            // The original blits its windows whole: colour 0, which sprites
            // leave out, is black here.
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
            SDL_RenderFillRect(renderer_, &dst);
            SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
            drawn = true;
        }
    }
    return drawn;
}

void Game::renderShop() {
    if (!openShop_) return;
    int mx = 0, my = 0;
    if (const auto at = pointerPosition()) {
        mx = static_cast<int>(at->x);
        my = static_cast<int>(at->y);
    } else {
        int wx, wy;
        SDL_GetMouseState(&wx, &wy);
        float lx, ly;
        SDL_RenderWindowToLogical(renderer_, wx, wy, &lx, &ly);
        mx = static_cast<int>(lx);
        my = static_cast<int>(ly);
    }
    const bool picture = drawShopPicture();
    if (!picture) {
        frame(renderer_, kShopPanel, {20, 18, 26, 225}, {150, 130, 90, 255});
        for (int y = 0; y < ShopStock::kHeight; ++y) {
            for (int x = 0; x < ShopStock::kWidth; ++x) {
                frame(renderer_, {kShopX + x * kCell, kShopY + y * kCell, kCell, kCell}, {35, 32, 40, 255}, {80, 74, 64, 255});
            }
        }
    }
    if (openShop_ == kGiantStorage) renderGiantStorageTabs();
    const auto& stock = openStock();
    const Item* hover = nullptr;
    for (const auto& g : stock.goods) {
        const SDL_Rect r{kShopX + g.x * kCell, kShopY + g.y * kCell, g.item.def->width * kCell, g.item.def->height * kCell};
        // Over a picture, a faint wash shows which cells an item takes.
        if (picture) {
            frame(renderer_, r, {90, 80, 110, 40}, {150, 130, 90, 90});
        } else {
            frame(renderer_, r, {60, 55, 70, 255}, {120, 110, 90, 255});
        }
        drawItemPicture(*g.item.def, r);
        drawItemMarks(g.item, r);
        if (inside(r, mx, my)) hover = &g.item;
    }
    // What the item under the mouse costs, or what the held one sells for,
    // in a box by the mouse.
    std::vector<std::pair<std::string, SDL_Color>> lines;
    if (storageOpen()) {
        storageHover_ = hover;
    } else if (hand_ && inside(kShopPanel, mx, my)) {
        if (hand_->def->quality == 3 || hand_->def->isGold()) {
            lines.emplace_back("Not for sale", kGrey);
        } else {
            lines.emplace_back(std::format("Sell for {} gold", sellPrice(*hand_, modifierPrices_) * std::max(1, hand_->count)), kGold);
        }
    } else if (hover && !hand_) {
        lines.emplace_back(hover->shownName(), qualityColor(hover->def->quality));
        lines.emplace_back(std::format("{} gold", buyPrice(*hover, modifierPrices_)), buyPrice(*hover, modifierPrices_) > inventory_.gold() ? kGrey : kGold);
    }
    if (!lines.empty()) {
        int w = 0;
        for (const auto& l : lines) w = std::max(w, uiTextWidth(l.first));
        SDL_Rect box{mx + 14, my + 14, w + 12, static_cast<int>(lines.size()) * kLineHeight + 8};
        box.x = std::max(2, std::min(box.x, kShopPanel.w - box.w - 2));
        box.y = std::min(box.y, kScreenHeight - box.h - 2);
        frame(renderer_, box, {0, 0, 0, 200}, {150, 130, 90, 255});
        for (std::size_t k = 0; k < lines.size(); ++k) {
            uiText(box.x + 6, box.y + 5 + static_cast<int>(k) * kLineHeight, lines[k].first, lines[k].second);
        }
    }
}

// C_ACTIONREQ on a person (0x45d480 sets its state, 0x45d3d0 plays it):
// state 1 stands, 2 walks, 3 talks and 4 on play chart state - 1. Monsters
// and objects take the command through their own code, which no scenario
// uses.
void Game::requestAction(const ScriptHost::ActionRequest& r) {
    Actor* a = actorById(r.character);
    if (!a || a->hostile()) return;
    if (r.action <= 1) {
        a->scripted.reset();
        a->character.play(chart::kIdle);
        return;
    }
    const int chart = r.action - 1;
    if (!a->character.hasChart(chart)) {
        log::info("{} has no chart {} for action {}", r.character, chart, r.action);  // our art may not draw it
        return;
    }
    a->scripted = Actor::ScriptedAction{chart, r.loop, r.from, r.to};
    log::info("{} plays action {} (chart {}, {} frames){}", r.character, r.action, chart, a->character.frameCount(), r.loop ? ", looping" : "");
    a->character.restart(chart);
}

// A scripted action's frame: played once it ends on its last frame and the
// person stands; looping, the frame after `to` (or the last) is `from` (or
// the first) again (0x45d850).
void Game::tickScriptedAction(Actor& a) {
    auto& s = *a.scripted;
    Character& c = a.character;
    if (c.chart() != s.chart) {
        a.scripted.reset();
        return;
    }
    const int last = c.frameCount() - 1;
    if (s.loop) {
        const int end = s.to == -1 ? last : std::min(s.to, last);
        if (c.frame() >= end) {
            c.setFrame(s.from == -1 ? 0 : s.from);
            return;
        }
        c.tick();
        return;
    }
    if (c.frame() >= last) {
        a.scripted.reset();
        c.play(chart::kIdle);
        return;
    }
    c.tick();
}

void Game::openTrade(int shop) {
    if (shop != kGiantStorage) shops_.try_emplace(shop);
    openShop_ = shop;
    showStatus_ = false;  // the shop takes the left of the screen
    closeMagicWindow();
    closeWarpWindow();
    openItemWindow();
}

ShopStock& Game::openStock() {
    if (openShop_ == kGiantStorage) return giantStorage_[static_cast<std::size_t>(std::clamp(giantPage_, 0, kGiantStoragePages - 1))];
    return shops_[openShop_.value_or(kStorage)];
}

bool Game::giantPageOpen(int page) const {
    if (page < 0 || page >= kGiantStoragePages) return false;
    if (page < 2) return true;
    const auto flag = world_.flags.find({ScenarioScript::kExtraStorage, page});
    return flag != world_.flags.end() && flag->second == 1;
}

int Game::unlockAllGiantPages() {
    int unlocked = 0;
    for (int page = 2; page < kGiantStoragePages; ++page) {
        auto& flag = world_.flags[{ScenarioScript::kExtraStorage, page}];
        if (flag != 1) ++unlocked;
        flag = 1;
    }
    log::info("Unlocked {} Giant Warehouse pages", unlocked);
    return unlocked;
}

namespace {

// The Giant Warehouse's header (clicks 0x447ca0, drawn by 0x404760):
// tabs I-X 24 pixels apart from (24, 41), and the close button.
constexpr int kGiantTabX = 24, kGiantTabY = 41, kGiantTabStep = 24;
constexpr SDL_Rect kGiantTabArea{kGiantTabX, kGiantTabY, kGiantTabStep, 24};
constexpr SDL_Rect kGiantClose{272, 40, 24, 16};

}  // namespace

bool Game::giantStorageHeaderClick(int x, int y) {
    for (int page = 0; page < kGiantStoragePages; ++page) {
        SDL_Rect tab = kGiantTabArea;
        tab.x += page * kGiantTabStep;
        if (!inside(tab, x, y)) continue;
        applyItemRequest(ItemRequest::of(ItemRequest::Kind::GiantPage, page));
        return true;
    }
    if (inside(kGiantClose, x, y)) {
        playSound(0x3a);
        applyItemRequest(ItemRequest::of(ItemRequest::Kind::CloseShop));
        return true;
    }
    return false;
}

// The header with its close button over the window, then the tabs: the
// page in view lit, the others plain, and a blank for those not open yet.
// The original's Status.njp patterns 0x49-0x5e, or the open pack's
// Tabs.sheet frames 0-21 in the same order.
void Game::renderGiantStorageTabs() {
    const SpriteBank* open = itemBank("System/Game/Pattern/Tabs");
    const SpriteBank* bank = open ? open : itemBank("System/Game/Pattern/Status");
    auto picture = [&](int pattern) { return bank ? sprites_.get(*bank, open ? pattern - 0x49 : pattern, false) : nullptr; };
    if (const SpriteTexture* header = picture(0x49)) {
        const SDL_Rect dst{header->originX, header->originY, header->width, header->height};
        SDL_RenderCopy(renderer_, header->texture, nullptr, &dst);
    }
    for (int page = 0; page < kGiantStoragePages; ++page) {
        const int pattern = !giantPageOpen(page) ? 0x4a : page == giantPage_ ? 0x55 + page : 0x4b + page;
        const int x = kGiantTabX + page * kGiantTabStep;
        if (const SpriteTexture* tab = picture(pattern)) {
            const SDL_Rect dst{x + tab->originX, kGiantTabY + tab->originY, tab->width, tab->height};
            SDL_RenderCopy(renderer_, tab->texture, nullptr, &dst);
        } else {
            const SDL_Rect r{x, kGiantTabY, 20, 23};
            frame(renderer_, r, page == giantPage_ ? SDL_Color{120, 90, 40, 255} : SDL_Color{40, 36, 44, 255}, {150, 130, 90, 255});
            if (giantPageOpen(page)) uiText(x + 4, kGiantTabY + 6, std::to_string(page + 1), kWhite);
        }
    }
    if (!picture(0x49)) {
        frame(renderer_, kGiantClose, {40, 36, 44, 255}, {150, 130, 90, 255});
        uiText(kGiantClose.x + 9, kGiantClose.y + 2, "X", kWhite);
    }
}

}  // namespace sf

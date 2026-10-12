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

// The original's messages are Shift-JIS. The English release only uses a
// few double-byte characters: the wide space and the middle dot.
std::string toAscii(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if ((c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc)) {
            const auto next = i + 1 < text.size() ? static_cast<unsigned char>(text[i + 1]) : 0;
            // Full width is two half-width cells; 0xa5 is the original
            // font's half-width middle dot.
            if (c == 0x81 && next == 0x40) out += "  ";
            if (c == 0x81 && next == 0x45) out += "\xa5 ";
            ++i;
        }
    }
    return out;
}

// What the scenario scripts ask of the game.
class GameScript : public ScriptHost {
public:
    explicit GameScript(Game& game) : g_(game) {}

    void talk(const Talk& t) override { g_.openDialogue(t); }
    void initTrade(int shop, int list) override {
        if (!g_.tables_.tables.empty()) {
            g_.shops_[shop] = stockShop(g_.tables_, g_.items_, list, g_.random_);
            return;
        }
        const auto goods = g_.openShops_.find(list);
        if (goods == g_.openShops_.end()) return;
        auto& stock = g_.shops_[shop];
        stock.goods.clear();
        for (const auto& g : goods->second) {
            const ItemDef* def = g_.items_.find(g.cls, g.code);
            if (!def) continue;
            for (int k = 0; k < g.count; ++k) stock.add(Item::make(def, g_.random_));
        }
    }
    void trade(int shop) override {
        g_.shops_.try_emplace(shop);
        g_.openShop_ = shop;
        g_.showStatus_ = false;  // the shop takes the left of the screen
        g_.closeMagicWindow();
        g_.showInventory_ = true;
    }
    void healLife() override {
        g_.playerHp_ = g_.playerMaxHp();
        // It fills the companion's health too, but does not bring it back.
        if (g_.partnerAlive()) g_.partner_->hp = g_.partner_->stats.maxHp;
    }
    void createItem(int cls, int code, int x, int y, int amount) override {
        const ItemDef* def = g_.items_.find(static_cast<ItemDef::Class>(cls), code);
        if (!def) return;
        if (!def->isGold()) {
            g_.dropItem(Item::make(def, g_.random_), x, y);
            return;
        }
        // Gold comes in piles of at most 10,000, spread out a little.
        for (int k = 0; amount > 0; ++k) {
            const int pile = std::min(amount, 10000);
            g_.dropItem({def, pile}, x + g_.random_.next(200) - 100, y + g_.random_.next(200) - 100);
            amount -= pile;
        }
    }
    void createItemTable(int list, int x, int y) override { g_.dropFromList(list, x, y); }
    std::int32_t characterX(std::int32_t id) const override {
        if (const auto at = g_.positionOf(id)) return static_cast<std::int32_t>(at->first);
        return g_.player_ ? static_cast<std::int32_t>(g_.player_->x) : 0;
    }
    std::int32_t characterY(std::int32_t id) const override {
        if (const auto at = g_.positionOf(id)) return static_cast<std::int32_t>(at->second);
        return g_.player_ ? static_cast<std::int32_t>(g_.player_->y) : 0;
    }
    void stopCharacter(std::int32_t id, bool stop) override {
        if (auto* a = g_.actorById(id)) a->stopped = stop;
    }
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
    int playerLevel() const override { return g_.playerLevel_; }
    int offeredClass() const override { return static_cast<int>(g_.career_.offered); }
    int playerSex() const override { return static_cast<int>(g_.heroSex_); }
    std::int64_t gold() const override { return g_.inventory_.gold(); }
    void payGold(std::int64_t amount) override { g_.inventory_.addGold(-static_cast<int>(std::min<std::int64_t>(amount, g_.inventory_.gold()))); }
    int random(int n) override { return g_.random_.next(n); }
    void showQuestName(int quest) override { g_.showMissionName(quest); }
    void showAreaName(const std::string& name) override { g_.showAreaName(toAscii(name)); }
    int entryPoint() const override { return g_.entryPoint_; }
    void openStorage() override { trade(Game::kStorage); }
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
    void offerClass(int cls) override {
        g_.career_.offered = static_cast<HeroClass>(cls);
        log::info("{} offered", className(g_.career_.offered, g_.heroSex_));
    }

private:
    Game& g_;
};

const Game::Actor* Game::actorById(std::int32_t id) const {
    for (const auto& a : actors_) {
        if (a.gameId() == id) return &a;
    }
    return nullptr;
}

Game::Actor* Game::actorById(std::int32_t id) { return const_cast<Actor*>(std::as_const(*this).actorById(id)); }

std::optional<std::pair<double, double>> Game::positionOf(std::int32_t id) const {
    if (const auto* a = actorById(id)) return std::pair(a->character.x, a->character.y);
    for (const auto& p : control_.placements) {
        if (ScenarioControl::idBase(p.group) + p.id == id) return std::pair(double(p.x), double(p.y));
    }
    return std::nullopt;
}

std::optional<std::size_t> Game::objectAt(float screenX, float screenY) const {
    if (!script_) return std::nullopt;
    double wx, wy;
    screenToWorld(cameraX_ + screenX, cameraY_ + screenY, wx, wy);
    for (std::size_t i = 0; i < control_.placements.size(); ++i) {
        const auto& p = control_.placements[i];
        if (p.group != ScenarioControl::Group::Object) continue;
        if (wx < p.x + p.rect[0] || wx > p.x + p.rect[2] || wy < p.y + p.rect[1] || wy > p.y + p.rect[3]) continue;
        const std::int32_t id = ScenarioControl::idBase(p.group) + p.id;
        for (const auto& t : script_->triggers) {
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

void Game::renderLabels(int camX, int camY) {
    for (const auto& l : labels_) {
        const auto at = positionOf(l.label.character);
        if (!at) continue;
        int sx, sy;
        worldToScreen(static_cast<int>(at->first), static_cast<int>(at->second), sx, sy);
        // Messages end in line breaks; each line is centred.
        std::vector<std::string> lines;
        std::string line;
        for (const char c : toAscii(l.label.text) + '\n') {
            if (c != '\n') {
                line += c;
            } else if (!line.empty()) {
                lines.push_back(std::exchange(line, {}));
            }
        }
        int y = sy - camY + l.label.dy;
        for (const auto& text : lines) {
            const int x = sx - camX + l.label.dx - uiTextWidth(text) / 2;
            uiText(x + 1, y + 1, text, {0, 0, 0, 255});  // a shadow, as the exe draws text
            uiText(x, y, text, {l.label.r, l.label.g, l.label.b, 255});
            y += kLineHeight;
        }
    }
}

void Game::runScript(std::int32_t sentence, std::int32_t character) {
    if (!script_ || pendingScenario_) return;
    GameScript host(*this);
    ScriptRunner runner(*script_, flags_, &host, character);
    runner.setRules(rules_);
    if (const auto change = runner.run(sentence)) {
        pendingScenario_.emplace(change->scenario, change->entry);
    }
}

void Game::runEvent(std::int32_t event, std::int32_t character) {
    if (!script_) return;
    for (const auto& t : script_->triggers) {
        if (t.event != event || (character != -1 && t.character != character)) continue;
        runScript(t.sentence, t.character);
    }
}

std::optional<std::size_t> Game::personAt(float screenX, float screenY) const {
    std::optional<std::size_t> best;
    double bestDistance = INFINITY;
    for (std::size_t i = 0; i < actors_.size(); ++i) {
        const auto& a = actors_[i];
        if (a.hostile() || !a.active) continue;
        int sx, sy;
        worldToScreen(static_cast<int>(a.character.x), static_cast<int>(a.character.y), sx, sy);
        const double dx = sx - cameraX_ - screenX, dy = sy - 40 - cameraY_ - screenY;
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
    for (std::size_t i = 0; i < actors_.size(); ++i) {
        if (actors_[i].gameId() != id) continue;
        if (player_) {
            player_->x = actors_[i].character.x + 150;
            player_->y = actors_[i].character.y + 150;
        }
        talkTarget_ = i;
        return;
    }
    for (std::size_t i = 0; i < control_.placements.size(); ++i) {
        const auto& p = control_.placements[i];
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
    for (const char c : toAscii(talk.text)) {
        if (c == '\n') {
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

void Game::closeDialogue(int answer) {
    if (!dialogue_) return;
    const Dialogue d = std::move(*dialogue_);
    dialogue_.reset();
    if (!d.scripted) return;
    if (script_) {
        ScriptRunner(*script_, flags_).store(d.answer, answer);
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
    SDL_SetTextureColorMod(font->texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(font->texture, color.a);
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        const SDL_Rect src{(c % 16) * 6, (c / 16) * 12, 6, 12};
        const SDL_Rect dst{x, y, 6, 12};
        SDL_RenderCopy(renderer_, font->texture, &src, &dst);
        x += 6;
    }
    SDL_SetTextureColorMod(font->texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(font->texture, 255);
}

// Where the box goes: above the speaker, kept on screen.
SDL_Rect Game::dialogueBox() {
    const auto& d = *dialogue_;
    int ax = kScreenWidth / 2, ay = kScreenHeight / 2;
    if (const auto* a = actorById(d.speaker)) {
        worldToScreen(static_cast<int>(a->character.x), static_cast<int>(a->character.y), ax, ay);
        ax -= static_cast<int>(cameraX_);
        ay -= static_cast<int>(cameraY_) + 110;
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
                case SDLK_SPACE: closeDialogue(count ? d.choice : -1); break;
                case SDLK_ESCAPE: closeDialogue(count ? count - 1 : -1); break;
                default: break;
            }
            break;
        case SDL_MOUSEMOTION:
            if (const int o = dialogueOptionAt(event.motion.x, event.motion.y); o >= 0) d.choice = o;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (!count) {
                closeDialogue(-1);
            } else if (const int o = dialogueOptionAt(event.button.x, event.button.y); o >= 0) {
                closeDialogue(o);
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
    if (!openShop_) return false;
    if (!inside(kShopPanel, x, y)) return false;
    auto& stock = shops_[*openShop_];
    const SDL_Rect grid{kShopX, kShopY, ShopStock::kWidth * kCell, ShopStock::kHeight * kCell};
    if (*openShop_ == kStorage) {
        // The warehouse: items are put down and picked up as in the bag; a
        // right click on an empty spot leaves.
        const int cx = (x - kShopX) / kCell, cy = (y - kShopY) / kCell;
        if (right) {
            if (!hand_ && (!inside(grid, x, y) || stock.indexAt(cx, cy) < 0)) closeShop();
        } else if (hand_ && inside(grid, x, y)) {
            const double left = x - hand_->def->width * kCell / 2.0 - kShopX;
            const double top = y - hand_->def->height * kCell / 2.0 - kShopY;
            const int tx = static_cast<int>(std::lround(left / kCell)), ty = static_cast<int>(std::lround(top / kCell));
            if (stock.click(hand_, tx, ty) == Inventory::Result::NoRoom) showMessage("No room there");
        } else if (inside(grid, x, y)) {
            stock.click(hand_, cx, cy);
        }
        return true;
    }
    if (hand_) {
        // Selling: the shop takes the item and pays a quarter of its value.
        if (hand_->def->quality == 3 || hand_->def->isGold()) {
            showMessage("Not for sale");
            return true;
        }
        const int price = sellPrice(*hand_, modifierPrices_) * std::max(1, hand_->count);
        inventory_.addGold(price);
        stock.add(*hand_);
        showMessage(std::format("Sold for {} gold", price));
        hand_.reset();
        return true;
    }
    // A right click anywhere else in the window leaves the shop.
    const int i = inside(grid, x, y) ? stock.indexAt((x - kShopX) / kCell, (y - kShopY) / kCell) : -1;
    if (right && i < 0) {
        closeShop();
        return true;
    }
    if (i < 0 || right) return true;
    const Item item = stock.goods[static_cast<std::size_t>(i)].item;
    const int price = buyPrice(item, modifierPrices_);
    if (inventory_.gold() < price) {
        showMessage("You're running short of money.");
        return true;
    }
    if (!inventory_.add(item)) {
        showMessage("No room in the bag");
        return true;
    }
    inventory_.addGold(-price);
    stock.goods.erase(stock.goods.begin() + i);
    return true;
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
    const bool storage = openShop_ == kStorage;
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
    {
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
    const auto& stock = shops_[*openShop_];
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
        if (inside(r, mx, my)) hover = &g.item;
    }
    // What the item under the mouse costs, or what the held one sells for,
    // in a box by the mouse.
    std::vector<std::pair<std::string, SDL_Color>> lines;
    if (*openShop_ == kStorage) {
        if (hover && !hand_) lines.emplace_back(hover->shownName(), kWhite);
    } else if (hand_ && inside(kShopPanel, mx, my)) {
        if (hand_->def->quality == 3 || hand_->def->isGold()) {
            lines.emplace_back("Not for sale", kGrey);
        } else {
            lines.emplace_back(std::format("Sell for {} gold", sellPrice(*hand_, modifierPrices_) * std::max(1, hand_->count)), kGold);
        }
    } else if (hover && !hand_) {
        lines.emplace_back(hover->shownName(), kWhite);
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

}  // namespace sf

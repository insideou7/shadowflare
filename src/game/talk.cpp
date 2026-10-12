// Talking to people, shops and the rest of what scenario scripts do.

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

#include "core/log.h"
#include "game/game.h"
#include "render/tiny_font.h"

namespace sf {

namespace {

constexpr int kCell = 32;
constexpr SDL_Rect kShopPanel{0, 0, 320, 416};
constexpr int kShopX = 16, kShopY = 40;
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
            if (c == 0x81 && next == 0x40) out += ' ';
            if (c == 0x81 && next == 0x45) out += '-';
            ++i;
        }
    }
    return out;
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

// What the scenario scripts ask of the game.
class GameScript : public ScriptHost {
public:
    explicit GameScript(Game& game) : g_(game) {}

    void talk(const Talk& t) override { g_.openDialogue(t); }
    void initTrade(int shop, int list) override {
        if (g_.tables_.tables.empty()) return;
        g_.shops_[shop] = stockShop(g_.tables_, g_.items_, list, g_.random_);
    }
    void trade(int shop) override {
        g_.shops_.try_emplace(shop);
        g_.openShop_ = shop;
        g_.showStatus_ = false;  // the shop takes the left of the screen
        g_.showInventory_ = true;
    }
    void healLife() override { g_.playerHp_ = g_.playerMaxHp(); }
    void createItem(int cls, int code, int x, int y, int amount) override {
        const ItemDef* def = g_.items_.find(static_cast<ItemDef::Class>(cls), code);
        if (!def) return;
        if (!def->isGold()) {
            g_.dropItem({def, 1}, x, y);
            return;
        }
        // Gold comes in piles of at most 10,000, spread out a little.
        for (int k = 0; amount > 0; ++k) {
            const int pile = std::min(amount, 10000);
            g_.dropItem({def, pile}, x + g_.random_.next(200) - 100, y + g_.random_.next(200) - 100);
            amount -= pile;
        }
    }
    std::int32_t characterX(std::int32_t id) const override {
        if (const auto* a = g_.actorById(id)) return static_cast<std::int32_t>(a->character.x);
        return g_.player_ ? static_cast<std::int32_t>(g_.player_->x) : 0;
    }
    std::int32_t characterY(std::int32_t id) const override {
        if (const auto* a = g_.actorById(id)) return static_cast<std::int32_t>(a->character.y);
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
    std::int64_t gold() const override { return g_.inventory_.gold(); }
    void payGold(std::int64_t amount) override { g_.inventory_.addGold(-static_cast<int>(std::min<std::int64_t>(amount, g_.inventory_.gold()))); }
    int random(int n) override { return g_.random_.next(n); }
    void showQuestName(int quest) override {
        const auto* titles = g_.tables_.find(41);
        if (titles && quest >= 0 && quest < titles->rows) g_.showMessage(toAscii(titles->string(quest, 0)));
    }
    int entryPoint() const override { return g_.entryPoint_; }
    void offerClass(int cls) override {
        g_.career_.offered = static_cast<HeroClass>(cls);
        log::info("{} offered", className(g_.career_.offered));
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

void Game::runScript(std::int32_t sentence, std::int32_t character) {
    if (!script_ || pendingScenario_) return;
    GameScript host(*this);
    if (const auto change = ScriptRunner(*script_, flags_, &host, character).run(sentence)) {
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
    double bestDistance = 40;
    for (std::size_t i = 0; i < actors_.size(); ++i) {
        const auto& a = actors_[i];
        if (a.hostile() || !a.active) continue;
        int sx, sy;
        worldToScreen(static_cast<int>(a.character.x), static_cast<int>(a.character.y), sx, sy);
        const double d = std::hypot(sx - cameraX_ - screenX, sy - 40 - cameraY_ - screenY);
        if (d < bestDistance) {
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
    if (script_) {
        ScriptRunner(*script_, flags_).store(d.answer, answer);
    }
    runEvent(ScenarioScript::kOnTalkEnd, d.speaker);
}

namespace {

// Where the box goes: above the speaker, kept on screen.
SDL_Rect dialogueBox(const std::vector<std::string>& lines, int anchorX, int anchorY) {
    int w = 0;
    for (const auto& l : lines) w = std::max(w, textWidth(l));
    w += 24;
    const int h = static_cast<int>(lines.size()) * kLineHeight + 18;
    SDL_Rect r{anchorX - w / 2, anchorY - h, w, h};
    r.x = std::clamp(r.x, 4, kScreenWidth - 4 - r.w);
    r.y = std::clamp(r.y, 4, kScreenHeight - 70 - r.h);
    return r;
}

}  // namespace

int Game::dialogueOptionAt(int x, int y) const {
    if (!dialogue_) return -1;
    int ax = kScreenWidth / 2, ay = kScreenHeight / 2;
    if (const auto* a = actorById(dialogue_->speaker)) {
        worldToScreen(static_cast<int>(a->character.x), static_cast<int>(a->character.y), ax, ay);
        ax -= static_cast<int>(cameraX_);
        ay -= static_cast<int>(cameraY_) + 100;
    }
    const SDL_Rect box = dialogueBox(dialogue_->lines, ax, ay);
    for (std::size_t i = 0; i < dialogue_->options.size(); ++i) {
        const auto& o = dialogue_->options[i];
        const std::string& l = dialogue_->lines[static_cast<std::size_t>(o.line)];
        const int x0 = box.x + 12 + textWidth(l.substr(0, o.from)) - 4;
        const int x1 = box.x + 12 + textWidth(l.substr(0, o.to)) + 4;
        const int y0 = box.y + 10 + o.line * kLineHeight - 3;
        if (x >= x0 && x < x1 && y >= y0 && y < y0 + kLineHeight) return static_cast<int>(i);
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
    int ax = kScreenWidth / 2, ay = kScreenHeight / 2;
    if (const auto* a = actorById(d.speaker)) {
        worldToScreen(static_cast<int>(a->character.x), static_cast<int>(a->character.y), ax, ay);
        ax -= static_cast<int>(cameraX_);
        ay -= static_cast<int>(cameraY_) + 100;
    }
    const SDL_Rect box = dialogueBox(d.lines, ax, ay);
    frame(renderer_, box, {10, 12, 30, 215}, {170, 150, 100, 255});
    for (std::size_t i = 0; i < d.lines.size(); ++i) {
        drawText(renderer_, box.x + 12, box.y + 10 + static_cast<int>(i) * kLineHeight, d.lines[i], kWhite);
    }
    // The chosen option is lit up.
    if (!d.options.empty()) {
        const auto& o = d.options[static_cast<std::size_t>(d.choice)];
        const std::string& l = d.lines[static_cast<std::size_t>(o.line)];
        const int x = box.x + 12 + textWidth(l.substr(0, o.from));
        const int y = box.y + 10 + o.line * kLineHeight;
        const SDL_Rect hl{x - 4, y - 3, textWidth(l.substr(o.from, o.to - o.from)) + 8, kLineHeight};
        frame(renderer_, hl, {200, 170, 80, 70}, {230, 200, 110, 255});
        drawText(renderer_, x, y, l.substr(o.from, o.to - o.from), kGold);
    }
}

// The shop's half of the screen; the bag stays open on the right.
bool Game::shopClick(int x, int y, bool right) {
    if (!openShop_) return false;
    if (!inside(kShopPanel, x, y)) return false;
    auto& stock = shops_[*openShop_];
    const SDL_Rect quit{kShopPanel.w / 2 - 30, kShopPanel.h - 30, 60, 20};
    if (inside(quit, x, y) && !hand_) {
        openShop_.reset();
        showInventory_ = false;
        return true;
    }
    if (hand_) {
        // Selling: the shop takes the item and pays a quarter of its price.
        if (hand_->def->quality == 3 || hand_->def->isGold()) {
            showMessage("Not for sale");
            return true;
        }
        const int price = sellPrice(*hand_->def) * std::max(1, hand_->count);
        inventory_.addGold(price);
        stock.add(*hand_);
        showMessage(std::format("Sold for {} gold", price));
        hand_.reset();
        return true;
    }
    const SDL_Rect grid{kShopX, kShopY, ShopStock::kWidth * kCell, ShopStock::kHeight * kCell};
    if (!inside(grid, x, y) || right) return true;
    const int i = stock.indexAt((x - kShopX) / kCell, (y - kShopY) / kCell);
    if (i < 0) return true;
    const Item item = stock.goods[static_cast<std::size_t>(i)].item;
    const int price = buyPrice(*item.def);
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
    frame(renderer_, kShopPanel, {20, 18, 26, 225}, {150, 130, 90, 255});
    drawText(renderer_, kShopX, 16, "SHOP", kGold);
    const auto& stock = shops_[*openShop_];
    for (int y = 0; y < ShopStock::kHeight; ++y) {
        for (int x = 0; x < ShopStock::kWidth; ++x) {
            frame(renderer_, {kShopX + x * kCell, kShopY + y * kCell, kCell, kCell}, {35, 32, 40, 255}, {80, 74, 64, 255});
        }
    }
    const ItemDef* hover = nullptr;
    for (const auto& g : stock.goods) {
        const SDL_Rect r{kShopX + g.x * kCell, kShopY + g.y * kCell, g.item.def->width * kCell, g.item.def->height * kCell};
        frame(renderer_, r, {60, 55, 70, 255}, {120, 110, 90, 255});
        drawItemPicture(*g.item.def, r);
        if (inside(r, mx, my)) hover = g.item.def;
    }
    if (hand_) {
        drawText(renderer_, kShopX, kShopY + ShopStock::kHeight * kCell + 8, std::format("SELL FOR {}", sellPrice(*hand_->def) * std::max(1, hand_->count)), kGold);
    } else if (hover) {
        std::string text = std::format("{}  {} GOLD", hover->name, buyPrice(*hover));
        if (text.size() > 36) text = std::format("{} GOLD", buyPrice(*hover));
        drawText(renderer_, kShopX, kShopY + ShopStock::kHeight * kCell + 8, text, buyPrice(*hover) > inventory_.gold() ? kGrey : kWhite);
    }
    const SDL_Rect quit{kShopPanel.w / 2 - 30, kShopPanel.h - 30, 60, 20};
    frame(renderer_, quit, {45, 40, 50, 255}, {150, 130, 90, 255});
    drawText(renderer_, quit.x + (quit.w - textWidth("QUIT")) / 2, quit.y + 5, "QUIT", inside(quit, mx, my) ? kGold : kWhite);
}

}  // namespace sf

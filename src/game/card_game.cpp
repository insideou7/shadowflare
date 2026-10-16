// The card game: Black Jack against a dealer, over the whole screen above
// the panel. STARTCARDGAME opens it (ShadowFlare.exe 0x434297) unless a
// message box is up, and closes it if it is open already; 0x40da90 plays a
// tick of it and draws it, at the places used here. The rules are in
// logic/card_game.cpp; see docs/card_game.md.
#include <algorithm>

#include "core/log.h"
#include "game/game.h"
#include "render/character_draw.h"

namespace sf {

namespace {

// Card.Njp's pictures (our pack's Card sheet numbers them the same, with
// the frame round the table as kFrame).
constexpr int kBack = 0x35, kCardShadow = 0x36, kDealer = 0x37, kDealerShadow = 0x38;
constexpr int kBlackJack = 0x39, kBreak = 0x3a, kHit = 0x3b, kStay = 0x3c, kWinner = 0x3d, kLoser = 0x3e;
constexpr int kButtonShadow = 0x3f, kLabelShadow = 0x40, kTable = 0x41, kDraw = 0x42;
constexpr int kOpenFrame = 69;
// The frame comes from Status.njp in the original.
constexpr int kStatusFrame = 0x77;

}  // namespace

void Game::toggleCardGame() {
    if (dialogue_) return;  // not while a message box is up (0x48ce44)
    if (cardGame_.open()) {
        cardGame_.close();
        return;
    }
    // Every other window closes (0x4170f0).
    if (showInventory_) closeInventory();
    showStatus_ = false;
    closeMissions();
    closeAreaMap();
    closeMagicWindow();
    closeWarpWindow();
    if (changeColor_) closeChangeColor(false);
    helpOpen_ = false;
    walkTarget_.reset();
    attackTarget_.reset();
    mouseHeld_ = false;
    cardGame_.start();
    cardTickCarry_ = 0;
    cardClick_.reset();
    log::info("The card game starts");
}

// At the original's pace, 30 ticks a second.
void Game::tickCardGame() {
    for (cardTickCarry_ += 30.0 / kTicksPerSecond; cardTickCarry_ >= 1.0 && cardGame_.open(); cardTickCarry_ -= 1.0) {
        const auto click = cardClick_;
        cardClick_.reset();
        const auto ev = cardGame_.tick(click.has_value(), click ? click->x : 0, click ? click->y : 0);
        if (ev.dealt) playSound(0x2c);
        if (ev.won) playSound(0x40);
        if (ev.lost) playSound(0x41);
        if (ev.ended) {
            log::info("The card game ends: {}", cardGame_.result() == CardGame::kPlayerWins   ? "the player wins"
                                                : cardGame_.result() == CardGame::kDealerWins ? "the dealer wins"
                                                                                               : "a draw");
            runEvent(ScenarioScript::kOnCardGameEnd);
        }
    }
}

// The game takes every click and key while it is open; a left click is
// kept for its next tick.
bool Game::cardGameEvent(const SDL_Event& event) {
    if (!cardGame_.open()) return false;
    switch (event.type) {
        case SDL_QUIT: return false;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT) cardClick_ = SDL_Point{event.button.x, event.button.y};
            return true;
        case SDL_KEYDOWN:
            // Print Screen still takes a picture.
            return event.key.keysym.sym != SDLK_PRINTSCREEN;
        default: return true;
    }
}

void Game::renderCardGame() {
    if (!cardGame_.open()) return;
    const SpriteBank* cards = itemBank("System/Game/Pattern/Card");
    const SpriteBank* status = itemBank("System/Game/Pattern/Status");
    const bool original = cards && cards->patterns.size() > kDraw && !(cards->patterns.size() > kOpenFrame);
    auto draw = [&](const SpriteBank* bank, int pattern, int x, int y, int alpha = 255) {
        const SpriteTexture* t = bank && pattern >= 0 ? sprites_.get(*bank, pattern, false) : nullptr;
        if (!t) return false;
        const SDL_Rect dst{x + t->originX, y + t->originY, t->width, t->height};
        SDL_SetTextureAlphaMod(t->texture, static_cast<Uint8>(alpha));
        SDL_RenderCopy(renderer_, t->texture, nullptr, &dst);
        SDL_SetTextureAlphaMod(t->texture, 255);
        return true;
    };
    auto card = [&](int pattern, int x, int y) {
        draw(cards, kCardShadow, x - 3, y + 3, 128);
        if (!draw(cards, pattern, x, y)) {
            // No pictures: a plain card.
            SDL_SetRenderDrawColor(renderer_, 230, 220, 190, 255);
            const SDL_Rect r{x, y, 63, 98};
            SDL_RenderFillRect(renderer_, &r);
        }
    };

    // The table, the frame round it and the deck.
    if (!draw(cards, kTable, 0x20, 0x28)) {
        SDL_SetRenderDrawColor(renderer_, 40, 32, 24, 255);
        const SDL_Rect r{0x20, 0x28, 576, 336};
        SDL_RenderFillRect(renderer_, &r);
    }
    if (original) {
        draw(status, kStatusFrame, 0, 0);
    } else {
        draw(cards, kOpenFrame, 0, 0);
    }
    for (int x = 0x232; x > CardGame::kDeckX; x -= 10) draw(cards, kBack, x, CardGame::kDeckY);

    for (const auto side : {CardGame::kDealer, CardGame::kPlayer}) {
        const auto& hand = cardGame_.hand(side);
        const int n = static_cast<int>(hand.size());
        for (int i = 0; i < n; ++i) {
            int x, y;
            CardGame::cardPosition(side, i, n, x, y);
            const auto& c = hand[static_cast<std::size_t>(i)];
            card(c.hidden ? kBack : c.suit * 13 - 1 + c.rank, x, y);
        }
    }

    // The dealer, the hero and their companion.
    draw(cards, kDealerShadow, 0x50, 0x78, 128);
    draw(cards, kDealer, 0x50, 0x78);
    if (player_) {
        Character hero = *player_;
        hero.direction = 5;
        hero.restart(hero.hasChart(9) ? 9 : chart::kIdle);
        if (hero.frameCount() > 0) hero.setFrame(static_cast<int>(ticks_ * 30 / kTicksPerSecond) % hero.frameCount());
        hero.x = hero.y = 0;
        drawCharacter(renderer_, sprites_, hero, -0x1f8, -0x140);
    }
    if (partner_ && partnerAlive()) {
        Character dog = partner_->character;
        dog.direction = 5;
        dog.restart(chart::kIdle);
        if (dog.frameCount() > 0) dog.setFrame(static_cast<int>(ticks_ * 30 / kTicksPerSecond) % dog.frameCount());
        dog.x = dog.y = 0;
        drawCharacter(renderer_, sprites_, dog, -0x234, -0x154);
    }

    if (cardGame_.buttonsShown()) {
        draw(cards, kButtonShadow, 0xe3, 0x155, 128);
        draw(cards, kHit, 0xe6, 0x152);
        draw(cards, kButtonShadow, 0x14d, 0x155, 128);
        draw(cards, kStay, 0x150, 0x152);
    }
    if (const auto f = cardGame_.flight()) {
        draw(cards, kCardShadow, f->x - 3, f->y + 3, 128);
        draw(cards, kBack, f->x, f->y);
    }

    if (cardGame_.endTicks() == 0) return;
    // Black Jack or Break by each hand, and who won.
    auto handLabel = [&](CardGame::Side side, int x, int y) {
        const auto& hand = cardGame_.hand(side);
        const int v = CardGame::value(hand);
        const int label = v == -1 ? kBreak : hand.size() == 2 && v == 21 ? kBlackJack : -1;
        draw(cards, label, x, y);
    };
    handLabel(CardGame::kPlayer, 0xc4, 0xf2);
    handLabel(CardGame::kDealer, 0xdc, 0x66);
    int dealerLabel = kDraw, playerLabel = kDraw;
    if (cardGame_.result() == CardGame::kPlayerWins) {
        dealerLabel = kLoser;
        playerLabel = kWinner;
    } else if (cardGame_.result() == CardGame::kDealerWins) {
        dealerLabel = kWinner;
        playerLabel = kLoser;
    }
    draw(cards, kLabelShadow, 0x24, 0x8f, 128);
    draw(cards, dealerLabel, 0x27, 0x8c);
    draw(cards, kLabelShadow, 0x1e3, 0xcb, 128);
    draw(cards, playerLabel, 0x1e6, 200);
}

}  // namespace sf

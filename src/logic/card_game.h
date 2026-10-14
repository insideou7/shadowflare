#pragma once

#include <functional>
#include <optional>
#include <vector>

namespace sf {

// The card game STARTCARDGAME opens: Black Jack against a dealer, as
// ShadowFlare.exe plays it. The rules are 0x403560-0x4037c0 (its state is
// the 0x48d7f0 block), the play and the picture 0x40da90, which the game
// runs every one of its ticks, 30 a second. See docs/card_game.md.
class CardGame {
public:
    // Suit 0-3 (spades, clubs, diamonds, hearts in Card.njp's order) and
    // rank 1 (ace) to 13 (king); suit 4 rank 0 is the joker, which the game
    // never deals. A hidden card shows its back.
    struct Card {
        int suit = 0;
        int rank = 1;
        bool hidden = false;
    };
    // The two hands: the dealer's along the top, the player's below.
    enum Side { kDealer = 0, kPlayer = 1 };
    // GETCARDGAMERESULT: 0 a draw, 1 the player won, 2 the dealer won.
    enum Result { kDraw = 0, kPlayerWins = 1, kDealerWins = 2 };

    // A card flies from the deck for this many ticks (0x48d878 counts them).
    static constexpr int kStep = 15;
    // The result shows this many ticks, then the game ends; a click cuts it
    // short.
    static constexpr int kEndTicks = 200;
    // Where the pictures go on the 640 x 480 screen.
    static constexpr int kDeckX = 0x1ec, kDeckY = 0x3c;
    // The HIT and STAY buttons take clicks strictly inside these.
    static constexpr int kButtonTop = 0x151, kButtonBottom = 0x172;
    static constexpr int kHitLeft = 0xe5, kHitRight = 0x148;
    static constexpr int kStayLeft = 0x14f, kStayRight = 0x1b2;

    // `random(n)` gives 0..n-1 (the exe calls rand()).
    explicit CardGame(std::function<int(int)> random) : random_(std::move(random)) {}

    // STARTCARDGAME: a new game, the first card about to fly to the dealer
    // (0x403560).
    void start();
    bool open() const { return open_; }
    // STARTCARDGAME while it is open closes it without a result.
    void close() { open_ = false; }

    // What a tick did, for the sounds and the script.
    struct Events {
        bool dealt = false;  // a card landed (voice 0x2c)
        bool won = false;    // the player won (voice 0x40)
        bool lost = false;   // the dealer won (voice 0x41)
        bool ended = false;  // the game is over: run the event 8 triggers
    };
    // One tick; `click` is a left click this tick at (x, y), screen pixels.
    Events tick(bool click = false, int x = 0, int y = 0);

    // A hand's worth (0x4036d0): aces (and the joker) count 11 while that
    // keeps it at 21 or under, else 1; picture cards count 10. -1 is bust.
    static int value(const std::vector<Card>& hand);
    // Who won (0x4037c0): the higher worth; at 21 each, a two-card 21
    // beats a longer one.
    int compare() const;

    const std::vector<Card>& hand(Side side) const { return side == kDealer ? dealer_ : player_; }
    int result() const { return result_; }
    // The HIT and STAY buttons show until the player stays or busts.
    bool buttonsShown() const { return !playerDone_; }
    // Counting down from kEndTicks while the result shows, else 0.
    int endTicks() const { return end_; }

    // Where a hand's n-th card lies (its top left), as 0x40da90 spreads
    // them: 80 apart, or squeezed into 240 pixels from the fifth on.
    static void cardPosition(Side side, int index, int count, int& x, int& y);
    // The card on its way from the deck, if one is flying, and where.
    struct Flight {
        int x = 0, y = 0;
    };
    std::optional<Flight> flight() const;

private:
    void deal(Side side, bool hidden = false);  // 0x4035a0
    void reveal(Side side);                    // 0x403820

    std::function<int(int)> random_;
    bool open_ = false;
    std::vector<Card> dealer_, player_;  // 0x48d834, 0x48d7f0
    int timer_ = 0;          // 0x48d878
    bool dealtAll_ = false;  // 0x48d87c: the four opening cards are out
    int end_ = 0;            // 0x48d880
    int result_ = kDraw;     // 0x48d884
    // 0x48d888: whose card flies next, and whose turn it is (0 the
    // player's, 1 the dealer's).
    int turn_ = 1;
    bool dealerDone_ = false;  // 0x48d88c
    bool playerDone_ = false;  // 0x48d890
    bool hit_ = false;         // 0x48d894: a card is asked for
};

}  // namespace sf

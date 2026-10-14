#include "logic/card_game.h"

#include <algorithm>

namespace sf {

void CardGame::start() {
    open_ = true;
    dealer_.clear();
    player_.clear();
    timer_ = 0;
    dealtAll_ = false;
    end_ = 0;
    result_ = kDraw;
    turn_ = 1;
    dealerDone_ = false;
    playerDone_ = false;
    hit_ = false;
}

void CardGame::deal(Side side, bool hidden) {
    // A card from the 52 that neither hand holds yet.
    auto held = [&](const Card& c) {
        auto same = [&](const Card& h) { return h.suit == c.suit && h.rank == c.rank; };
        return std::ranges::any_of(dealer_, same) || std::ranges::any_of(player_, same);
    };
    Card card;
    do {
        const int r = random_ ? random_(52) : 0;
        card.suit = r / 13;
        card.rank = r % 13 + 1;
    } while (held(card) && dealer_.size() + player_.size() < 52);
    card.hidden = hidden;
    (side == kDealer ? dealer_ : player_).push_back(card);
}

void CardGame::reveal(Side side) {
    for (auto& c : side == kDealer ? dealer_ : player_) c.hidden = false;
}

int CardGame::value(const std::vector<Card>& hand) {
    int sum = 0, aces = 0;
    for (const auto& c : hand) {
        if (c.suit == 4 || c.rank == 1) {
            ++aces;
        } else {
            sum += std::min(c.rank, 10);
        }
    }
    // Each ace takes 11 when the rest, at 1 each, still fit under 21.
    for (int left = aces; left > 0; --left) sum += 21 - sum < left + 10 ? 1 : 11;
    return sum > 21 ? -1 : sum;
}

int CardGame::compare() const {
    const int dealer = value(dealer_), player = value(player_);
    if (dealer < player) return kPlayerWins;
    if (player < dealer) return kDealerWins;
    if (player == 21) {
        if (dealer_.size() == 2) {
            if (player_.size() != 2) return kDealerWins;
        } else if (player_.size() == 2) {
            return kPlayerWins;
        }
    }
    return kDraw;
}

CardGame::Events CardGame::tick(bool click, int x, int y) {
    Events ev;
    if (!open_) return ev;
    // A click while the result shows ends the game on the next tick.
    if (click && end_ > 0 && end_ < kEndTicks) end_ = 1;
    if (end_ == 0) {
        if (dealtAll_) {
            const bool ready = timer_ % kStep == 0;
            const bool onButtons = y > kButtonTop && y < kButtonBottom;
            if (click && turn_ == 0 && ready && onButtons && x > kHitLeft && x < kHitRight) {
                hit_ = true;
                ++timer_;
            }
            if (click && turn_ == 0 && ready && onButtons && x > kStayLeft && x < kStayRight) {
                turn_ = 1;
                playerDone_ = true;
                if (dealerDone_) end_ = kEndTicks;
                timer_ = 1;
                reveal(kDealer);
            }
        } else {
            // The opening deal: dealer, player, the dealer's hidden card,
            // player, a card every kStep ticks.
            ++timer_;
            if (timer_ == kStep) {
                turn_ = 0;
                deal(kDealer);
                ev.dealt = true;
            } else if (timer_ == 2 * kStep) {
                turn_ = 1;
                deal(kPlayer);
                ev.dealt = true;
            } else if (timer_ == 3 * kStep) {
                turn_ = 0;
                deal(kDealer, true);
                ev.dealt = true;
            } else if (timer_ == 4 * kStep) {
                turn_ = 0;
                deal(kPlayer);
                ev.dealt = true;
                dealtAll_ = true;
                timer_ = 0;
            }
        }
    }
    if (!dealtAll_) return ev;

    if (end_ == 0) {
        if (timer_ % kStep != 0) ++timer_;
        if (timer_ % kStep == 0 && turn_ == 1) {
            // The dealer's turn: their hidden card turns over, then they
            // draw while under 17.
            reveal(kDealer);
            if (!hit_) {
                if (!dealerDone_) {
                    const int v = value(dealer_);
                    if (v > 16) dealerDone_ = true;
                    if (v == -1) {
                        dealerDone_ = true;
                        end_ = kEndTicks;
                    } else if (dealerDone_) {
                        end_ = kEndTicks;
                    } else {
                        hit_ = true;
                        ++timer_;
                    }
                }
            } else if (!dealerDone_) {
                deal(kDealer);
                ev.dealt = true;
                hit_ = false;
                ++timer_;
            }
        }
        if (timer_ % kStep == 0 && turn_ == 0 && hit_) {
            // The card the player asked for lands; over 21 ends it.
            deal(kPlayer);
            ev.dealt = true;
            ++timer_;
            if (value(player_) == -1) {
                playerDone_ = true;
                dealerDone_ = true;
                end_ = kEndTicks;
                reveal(kDealer);
            }
            hit_ = false;
        }
    } else {
        if (end_ == kEndTicks) {
            result_ = compare();
            ev.won = result_ == kPlayerWins;
            ev.lost = result_ == kDealerWins;
        }
        if (--end_ == 0) {
            open_ = false;
            ev.ended = true;
        }
    }
    return ev;
}

void CardGame::cardPosition(Side side, int index, int count, int& x, int& y) {
    // The dealer's row is 24 pixels further right than the player's.
    const int row = side == kDealer ? 0 : 1;
    y = 0x46 + 0x8c * row;
    if (count < 5) {
        x = 0xb6 - 0x18 * row + 0x50 * index;
    } else if (index == count - 1) {
        x = 0x1a6 - 0x18 * row;
    } else {
        x = 0xb6 - 0x18 * row + index * (0xf0 / (count - 1));
    }
}

std::optional<CardGame::Flight> CardGame::flight() const {
    const int t = timer_ % kStep;
    if (!open_ || t == 0 || !(!dealtAll_ || hit_)) return std::nullopt;
    // Towards the next place in the hand whose card it is.
    const int count = static_cast<int>(turn_ == 0 ? player_.size() : dealer_.size());
    const int toY = turn_ == 0 ? 0xd2 : 0x46;
    const int toX = count + 1 < 5 ? (count + 1) * 0x50 + 0x4e + turn_ * 0x18 : turn_ * 0x18 + 0x18e;
    return Flight{kDeckX + (toX - kDeckX) * t / kStep, kDeckY + (toY - kDeckY) * t / kStep};
}

}  // namespace sf

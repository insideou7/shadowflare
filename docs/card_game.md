# The card game

`STARTCARDGAME` (script command 73) opens a game of Black Jack against a
dealer over the whole screen above the game panel. Only two scenarios use
it (99000018 and 99000023): a man asks for a 10,000 gold stake, opens the
game, and reads `GETCARDGAMERESULT` (74) into a TEMP flag from a sentence
that the game's end runs (trigger event 8). The engine's version is
`src/logic/card_game.cpp` (rules) and `src/game/card_game.cpp` (drawing).
`--card-game` opens it at once, for testing.

## Rules (ShadowFlare.exe 0x403560-0x4037c0)

- The game runs at the original's 30 ticks a second. The state is the
  block at 0x48d7f0.
- The opening deal gives a card every 15 ticks: the dealer, the player,
  the dealer's second card face down, the player. Cards come from the 52
  that neither hand holds.
- Aces count 11 while the hand stays at 21 or under, else 1; picture
  cards count 10. Over 21 is bust (BREAK).
- HIT and STAY take a left click only while no card is flying. HIT deals
  the player a card; a bust ends the game. STAY turns the dealer's card
  over, and the dealer draws while under 17.
- The higher hand wins. At 21 each, a two-card 21 (BLACK JACK) beats a
  longer one; otherwise equal hands are a draw.
- The result shows for 200 ticks, then the game closes and runs the
  event 8 triggers. A click cuts the wait short.
- `GETCARDGAMERESULT`: 0 a draw, 1 the player won, 2 the dealer won.
- Sounds: voice 0x2c as a card lands, 0x40 when the player wins, 0x41
  when the dealer wins.
- `STARTCARDGAME` does nothing while a message box is open, and closes the
  game without a result if it is open already.

## Layout (0x40da90)

On the 640 x 480 screen:

| What | Where |
|------|-------|
| Table (Card.Njp 65, 576 x 336) | (32, 40); the frame round it is Status.njp 0x77 |
| Deck | backs from x 0x232 down to 492, step 10, y 0x3c |
| Dealer's cards | y 0x46, from x 0xb6, 0x50 apart |
| Player's cards | y 0xd2, 24 pixels further left |
| Five cards or more | the first n - 1 spread over 240 pixels, the last at x 0x1a6 (less 24 for the player) |
| HIT / STAY | x 0xe5-0x148 and 0x14f-0x1b2, y 0x151-0x172 |
| Dealer figure (55) | (80, 120) |
| Hero | chart 9, facing 5, at (504, 320); the dog at (564, 340) |
| WINNER / LOSER / DRAW | (0x27, 0x8c) for the dealer, (0x1e6, 200) for the player |

Card.Njp patterns: 0-51 the cards (suit * 13 + rank - 1, suits spades,
clubs, diamonds, hearts), 52 the joker (never dealt), 53 the back, 54 a
card's shadow, 55 the dealer, 56 its shadow, 57 BLACK JACK, 58 BREAK,
59 HIT, 60 STAY, 61 WINNER, 62 LOSER, 63 a button's shadow, 64 a label's
shadow, 65 the table, 66 DRAW, 67-68 other HIT and STAY pictures.

## Open pack

`content/System/Game/Pattern/Card` numbers its pictures the same way and
adds the frame as pattern 69. `tools/art/cards.py` draws it.

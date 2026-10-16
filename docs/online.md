# Online play

Not playable yet. This page is the plan and what is in place so far.

## How the original does it

From ShadowFlare.exe: one player hosts (`RKC_NETWORK_SERVER`, three joining
players, so four in all) and the others join by typing the host's IP. A
joining game sends its whole character from its own save, a 0x8b8-byte
record, and the host checks only three version words (2, 4, 0) before
taking it as is. During play every game keeps sending its full record
(packet 0x11) and the host relays it. So an edited save, or memory changed
while playing, goes straight into the online game.

## Our plan

The same shape (one game hosts, a few join), with the host deciding
everything:

- Online characters are kept by the host, apart from the six offline saves.
  Offline saves stay plain text you can edit, and cannot be brought online.
- A joining game sends only what its player asks for: the hero's input
  (below), and later item moves, shop and talk choices. The host checks
  each one and works out every result: damage, experience, gold, drops,
  quest flags.
- Items get an id from the host, so none can be made up or copied.
- Rules settings (original mode, the Debug tab's rates) are the host's.
- WebSockets carry it, so native and web games can play together, polled
  once a frame with no threads.
- Later, a dedicated server: the same game with no window, which can also
  run a shared hub town with a private copy of each area per party.

The largest job is keeping the game's rules apart from its windows and
drawing, so a host can run a hero it does not draw and a server can run
with no window at all. That is done a piece at a time.

## In place

- **The hero's input** (`src/logic/hero_input.h`). Each tick the game reads
  the mouse and keyboard into a `HeroInput` (button held, Shift, Ctrl,
  arrow keys, the enemy, item or object under the pointer and the pointer
  in world units), and `Game::tickPlayer` acts on that alone, never on SDL.
  It encodes to a fixed 21-byte message; decoding refuses anything out of
  range, and the hero's tick ignores an enemy, item or object that is not
  there. This is what a host will run a joining player's hero from.
- **Item requests** (`src/logic/item_request.h`). Everything the item window
  and belt do with the hero's own items is an `ItemRequest`: a click in the
  bag, on a worn slot or on the belt, using, wearing (right click) or taking
  off an item, dropping the hand, the weapon set tabs, splitting a bag gold
  stack, the Warehouse Transfer Box and the mine slot, buying and selling,
  the warehouse windows (putting down and taking up, right clicks, gold
  splits, Giant Warehouse pages, leaving) and choosing the item for
  Identify. `Game::applyItemRequest` carries each out against the hero's inventory
  after checking it. Items are named by the cell or slot they are in, so a
  request can only move what the hero has. A request encodes to 10 bytes;
  decoding refuses cells off the bag or belt, slots and tabs that do not
  exist, and gold amounts below 1. The game also checks what is open: a
  purchase needs a shop open and gold enough, a warehouse move needs the
  warehouse open, and Identify needs the spell waiting.

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

Decided: a dedicated server written in Rust, with a hub town of our own as
the lobby and instanced areas. The server decides everything:

- Online characters live on the server, apart from the six offline saves.
  Offline saves are sealed so they are not easy to edit
  (src/logic/save_seal.h), but the key ships with the game, so the server
  never trusts them: they cannot be brought online.
  Accounts are a name and a password (an Argon2 hash on the server).
- Players meet in the hub town, form parties with friends, and step
  through the hub's gates into an instance: a private copy of an area that
  only their party can enter.
- Clients send only what their player asks for: the hero's input, item
  requests and talk requests (below). The server checks each one and
  works out every result: damage, experience, gold, drops, quest flags.
  Story flags are kept per character; an instance follows the party
  leader's.
- Items get an id from the server, so none can be made up or copied.
- Rules settings (original mode, the Debug tab's rates) are the server's.
- The game's rules stay in C++. The server links them through a small C
  interface (create a world, add a hero, apply a request, tick, read a
  snapshot), and the client stays C++, so the web build needs no Rust.
- WebSockets carry it, so native and web games can play together. Message
  layouts are written down here with byte test cases that both the C++ and
  the Rust tests check.

The largest job is keeping the game's rules apart from its windows and
drawing, so the server can run them with no window at all. That is done a
piece at a time, with offline play checked against main after each.

Order: finish the split (an SDL-free `World` next), then the Rust server with logins and the character store, then
the hub town, then friends, parties and instances.

## In place

- **The hero's input** (`src/logic/hero_input.h`). Each tick the game reads
  the mouse and keyboard into a `HeroInput` (button held, Shift, Ctrl,
  arrow keys, the enemy, item or object under the pointer and the pointer
  in world units), and `Game::tickPlayer` acts on that alone, never on SDL.
  It encodes to a fixed 21-byte message; decoding refuses anything out of
  range, and the hero's tick ignores an enemy, item or object that is not
  there. This is what the server will run a player's hero from.
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
- **Talk requests** (`src/logic/talk_request.h`). Clicking a person to walk
  up and talk, clicking a chest, switch or other object to use it, and the
  answer chosen when a conversation box closes (by click, Enter or Esc) are
  each a `TalkRequest`, which `Game::applyTalkRequest` checks first: the
  person must be there, not a monster and not hidden; the object must be
  shown and usable; an answer needs a box open and must be one of its
  options (or none, for a box with none). Quest choices are those answers:
  the script stores the answer and decides what follows, so the server runs
  the same script. A request encodes to 6 bytes; decoding refuses unknown
  kinds, numbers below 0 and answers out of range.
- **The world library** (`sf_world`: `src/world/`). Maps, characters and
  their animations, and reading the game's files build without SDL;
  drawing a character moved to `src/render/character_draw.h`.
  `tests/world_test.cpp` links `sf_world` alone, and SDL's headers are not
  on its include path, so anything that pulls SDL into the world fails to
  build.

## The world split, step by step

Each step keeps offline play the same, checked by comparing saves and
screenshots of scripted runs with the build before it.

1. Maps, characters and file loading in `sf_world` (done).
2. What the rules tell the player (sounds, messages on screen) becomes a
   list of events the world hands out each tick, which the game plays and
   shows, so the rules call no audio or drawing code (done:
   `src/world/world_events.h`; `Game::playSound` and `showMessage` queue,
   `Game::deliverEvents` plays and shows after each tick and input event,
   and whether the hero is near enough to hear is decided there).
3. A `World` class in `src/world/` holds one area's state (monsters,
   people, ground items, objects, story flags, scripts) and its ticks
   (monster AI, combat, shots, traps, scripts); `Game` owns one and draws
   what is in it. It moves over a piece at a time.
4. The hero's own state (inventory, stats, spells, companion) becomes a
   hero record in the world, so a world can hold several heroes and take
   each one's `HeroInput`, item and talk requests.
5. The C interface the Rust server links: create a world, add a hero,
   apply a request, tick, read a snapshot or a hero's record.

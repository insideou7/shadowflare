#pragma once

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "audio/mixer.h"
#include "game/character.h"
#include "logic/ai_select.h"
#include "logic/explored_map.h"
#include "logic/lighting.h"
#include "logic/inventory.h"
#include "logic/loot.h"
#include "logic/magic.h"
#include "logic/mines.h"
#include "logic/partner.h"
#include "logic/rules.h"
#include "logic/progression.h"
#include "logic/script_runner.h"
#include "logic/settings.h"
#include "logic/shop.h"
#include "logic/traps.h"
#include "formats/table_data.h"
#include "formats/open_content.h"
#include "formats/ai_control.h"
#include "formats/item_data.h"
#include "formats/missions.h"
#include "formats/scenario.h"
#include "formats/scenario_script.h"
#include "formats/warp_gates.h"
#include "game/map.h"
#include "render/sprite_cache.h"

namespace sf {

class FileSystem;
class Skin;

// The original's Shift-JIS text as the font draws it (talk.cpp): the
// English release only uses a few double-byte characters.
std::string toAscii(const std::string& text);

// An SDL texture the game owns.
struct TextureFree {
    void operator()(SDL_Texture* t) const { SDL_DestroyTexture(t); }
};
using TexturePtr = std::unique_ptr<SDL_Texture, TextureFree>;

// The original runs at 640x480; we render to that and let SDL scale it.
inline constexpr int kScreenWidth = 640;
inline constexpr int kScreenHeight = 480;

// Simulation runs at a fixed rate, separate from rendering, so game logic
// can later be matched tick-for-tick against the original executable.
inline constexpr int kTicksPerSecond = 20;
// The original runs 30 ticks a second, so each engine tick is 1.5 of them.
inline constexpr double kOriginalTicksPerTick = 30.0 / kTicksPerSecond;

// The hero's step in world units per tick. Not yet taken from the original.
inline constexpr double kPlayerWalkSpeed = 70.0;
// Top of the companion's health gauge, as in the original.
inline constexpr int kPartnerGaugeY = 395;
// The belt pocket's cells on the screen, as the original's clicks find them
// (0x445bd0, 0x44a5f0): the top row's left edge, and the bottom row's,
// which sits a cell and a half further right.
inline constexpr int kBeltLeft = 357, kBeltTop = 413, kBeltBottomLeft = 405, kBeltCell = 32;

// For now the game is a map viewer with a walking player: it loads a level
// from the original data and lets you walk around it.
class Game {
public:
    // With a skin, the original's maps and characters are drawn with an
    // open content pack's art where it has some.
    Game(SDL_Renderer* renderer, const FileSystem* data, const std::string& startMap, Skin* skin = nullptr);

    void handleEvent(const SDL_Event& event);
    void tick();
    void render(double interpolation);
    // The sounds and music playing, for the main loop to send to the
    // sound device.
    Mixer& mixer() { return mixer_; }

    bool wantsQuit() const { return quit_; }

    // Starts walking towards a point given relative to the player on screen
    // (used for scripted smoke tests).
    void walkBy(int screenDx, int screenDy);

    // Puts the player on the walkable spot nearest a map pixel.
    void placePlayerAt(double screenX, double screenY);

    // Makes the player attack the nearest monster on their own (for
    // scripted smoke tests of combat).
    void setAutoFight(bool on) { autoFight_ = on; }
    // Swaps in a kind of companion and sets it ACTIVE (for scripted tests).
    void bringPartner(int kind);

    // Puts an item in the bag, on the hero or on the ground next to them
    // (for scripted tests). For gold, `count` is the amount. False when the
    // item is unknown, does not fit or cannot be worn.
    enum class GiveTo { Bag, Wear, Ground };
    bool give(int itemClass, int code, int count, GiveTo to, int wear = 0);  // wear: how worn it starts

    // Shows the inventory panel (toggled with I).
    void setInventoryOpen(bool open) { showInventory_ = open; }

    // Draws the walkability grid over the map (toggled with J).
    void setShowJudge(bool show) { showJudge_ = show; }
    std::uint64_t tickCount() const { return ticks_; }

    // Starts the hero at a level, fully healed (for testing): a warrior from
    // level 5, as the original makes every hero.
    void setLevel(int level) {
        playerLevel_ = std::clamp(level, 1, levels_.maxLevel());
        career_ = Career::at(playerLevel_);
        spells_ = SpellBook::replay(magic_, career_.history);
        playerHp_ = playerMaxHp();
        playerMp_ = playerMaxMp();
    }

    // For screenshots of the panel: the hero's HP drops to `percent` four
    // seconds in, and the experience bar starts part full.
    void hurtLater(int percent) { hurtPercent_ = percent; }
    void setExperiencePercent(int percent) {
        playerExperience_ = levels_.need(playerLevel_) * std::clamp(percent, 0, 100) / 100;
    }

    // Original mode plays by the original game's rules, bugs included
    // (logic/rules.h). Off by default.
    void setOriginalMode(bool on) { rules_ = on ? Rules::original() : Rules::ours(); }
    bool originalMode() const { return rules_.originalMode(); }

    // Chooses the spell on F1-F8 (slot 0-7).
    void selectSpellSlot(int slot);
    // Casts the chosen spell at a point given relative to the player on
    // screen, as the right button does (for scripted tests).
    void castBy(int slot, int screenDx, int screenDy);
    // Teaches a spell at a level and puts it on F1, with MP to spare (for tests).
    void teachSpell(int spell, int level);
    // Gives the companion a Tab order (jump, dash or fetch) at a point
    // relative to the hero in world units; fetch takes the nearest item
    // (for scripted tests).
    void orderPartnerBy(const std::string& kind, int dx, int dy);

    // Increased-power mode (P or the power button): `charge` kills toward
    // it, for tests; startPower() starts it when it is ready.
    void setPowerCharge(int charge) { power_.charge = charge; }
    bool startPower();
    bool powerActive() const { return power_.active(); }
    // The magic window (M), on page 0-3 (for tests).
    void openMagicWindow(int page = 0);

    // Lays a mine where the player stands, as B does (for scripted tests).
    void layMineHere() { layMine(); }

    // Shows the status window (toggled with S).
    void setStatusOpen(bool open) { showStatus_ = open; }

    // The title menu: Continue, New Game, Options and Quit.
    enum class MenuItem { Continue, NewGame, Options, Quit };
    void openMenu();
    bool menuOpen() const { return menuOpen_; }

    // The Create New Character screen that New Game opens (new_character.cpp):
    // the hero or the heroine, a name, then the game mode.
    void openNewCharacter();
    bool newCharacterOpen() const { return newCharacter_.has_value(); }
    // Makes the hero who they are: their sex and name (for scripted runs,
    // and what the Create New Character screen settles on).
    void setHero(HeroSex sex, std::string name);
    HeroSex heroSex() const { return heroSex_; }
    const std::string& heroName() const { return heroName_; }

    // The Settings Menu, as the original's (Esc in the game); from the
    // title menu it is Options, without the save and exit rows.
    void openOptions(bool inGame);
    // Reads the options from this file and writes them back when they change.
    void setSettingsFile(std::string path);
    const Settings& settings() const { return settings_; }
    void setHelpOpen(bool open) { helpOpen_ = open; }

    // Walks the player up to a character and speaks to them (for scripted
    // tests); `answers` close the messages that follow, one each.
    void talkToId(std::int32_t id);
    void setAnswers(std::vector<int> answers) { answers_ = std::move(answers); }
    // Sets a SAVE story flag, for testing scripts part way through the story.
    // Uses a scenario object as a click on it does, from wherever the
    // player is, or holds the left button on one (for scripted tests).
    void useObject(std::int32_t id) { runEvent(ScenarioScript::kOnTalk, id); }
    void holdObject(std::int32_t id) { heldObject_ = id; }
    void setSaveFlag(std::int32_t id, std::int32_t value) { flags_[{ScenarioScript::kSave, id}] = value; }
    // Sets a mission's QUEST flag (1 taken, 2 cleared), for testing.
    void setQuestFlag(std::int32_t id, std::int32_t value) { flags_[{ScenarioScript::kQuest, id}] = value; }
    // Opens the mission list (Q), with one mission's description showing
    // when `shown` is not -1 (for testing).
    void openMissions(int shown = -1);
    // Marks warp gate `gate` found (its WARP flag), for testing.
    void setWarpFlag(std::int32_t gate, std::int32_t value) { flags_[{ScenarioScript::kWarp, gate}] = value; }
    int warpGateCount() const { return warpGates_.count(); }
    // The GATE window, as WARPGATE `gate` opens it, on one of its pages;
    // and picking the n-th gate listed on the page showing (for testing).
    void openWarpGate(int gate, int page = 0);
    void chooseWarpGate(int row);

    // Saves the game to a text file (F10) or loads it back (F11). False, with
    // a message on screen, when that fails.
    void setSaveFile(std::string path) { saveFile_ = std::move(path); }
    bool saveGame();
    bool loadGame();

private:
    // People and monsters placed by the map's scenario file.
    struct Actor {
        enum class State { Idle, Chase, Attack, Hit, Dead };
        Actor(ScenarioControl::Placement p, Character c) : placement(std::move(p)), character(std::move(c)) {}
        ScenarioControl::Placement placement;
        Character character;
        State state = State::Idle;
        int hp = 0, maxHp = 0;
        // A stun holds it in its hit pose this many of the original's ticks;
        // Freeze and Stone are what the spell's stun does besides.
        double stunTicks = 0;
        StunKind stunKind = StunKind::Plain;
        bool stoned() const { return stunTicks > 0 && stunKind == StunKind::Stone; }

        // Control.aid state; aiList -1 = no list, use the simple chase.
        int aiList = -1;
        int action = -1;
        int actionTicks = 0;
        int event = AiControl::kDecide;
        AiControl::Entry entry;
        bool struck = false;  // the current attack has landed
        double wanderX = 0, wanderY = 0;
        int wanderPause = 0;
        bool active = true;   // SETINACTIVE hides a character
        bool stopped = false;  // C_STOP holds it still while it talks
        std::int32_t gameId() const { return ScenarioControl::idBase(placement.group) + placement.id; }
        bool hostile() const { return placement.group == ScenarioControl::Group::Enemy; }
        bool alive() const { return state != State::Dead; }
    };
    // An area whose script runs when the player walks into it, such as an
    // exit to the next map.
    struct TriggerArea {
        double left, top, right, bottom;  // world units
        std::int32_t sentence;
        bool inside = false;  // only entering fires it
    };
    // Talking, shops and the rest of the scenario script (talk.cpp).
    friend class GameScript;
    struct Dialogue {
        std::vector<std::string> lines;
        struct Option {
            int line = 0;
            std::size_t from = 0, to = 0;  // characters of the line
        };
        std::vector<Option> options;
        int choice = 0;
        ScenarioScript::Operand answer;
        std::int32_t speaker = -1;
        bool scripted = true;  // a script waits for the answer
    };
    void runScript(std::int32_t sentence, std::int32_t character = -1);
    void runEvent(std::int32_t event, std::int32_t character = -1);
    void openDialogue(const ScriptHost::Talk& talk);
    void closeDialogue(int answer);
    void dialogueEvent(const SDL_Event& event);
    int dialogueOptionAt(int x, int y);
    SDL_Rect dialogueBox();
    SDL_Rect dialogueOptionRect(const SDL_Rect& box, int option);
    // Text in the original's font when the data has it, else the tiny one.
    const SpriteTexture* uiFont();
    int uiTextWidth(std::string_view text);
    void uiText(int x, int y, std::string_view text, SDL_Color color);
    void renderDialogue();
    // Floating text from MESSAGEBOX (talk.cpp).
    struct Label {
        ScriptHost::Label label;
        int ticks = 0;  // left to show
    };
    void showLabel(const ScriptHost::Label& label);
    void renderLabels(int camX, int camY);
    // Where a character or a map object is, in world units.
    std::optional<std::pair<double, double>> positionOf(std::int32_t id) const;
    const ScenarioControl::Placement* placementById(std::int32_t id) const;
    // A map object with a talk script (the warehouse) under the mouse,
    // as an index into control_.placements.
    std::optional<std::size_t> objectAt(float screenX, float screenY) const;
    bool shopClick(int x, int y, bool right);
    void renderShop();
    bool drawShopPicture();
    void closeShop();
    std::optional<std::size_t> personAt(float screenX, float screenY) const;
    Actor* actorById(std::int32_t id);
    const Actor* actorById(std::int32_t id) const;
    void talkTo(Actor& person);

    void loadMap(std::size_t index, const std::string& scenarioDir = {});
    void loadScenario(const std::string& dir);
    void indexScenarios();
    bool readScenario(const std::string& dir, ScenarioControl& control, std::optional<ScenarioScript>* script) const;
    void spawnActors();
    void changeScenario(int number, int entry);
    void goToStart();
    void checkTriggers(bool fire = true);
    const CharacterModel* model(const std::string& stem);
    void placePlayer();
    void updateCamera();
    bool tryMove(Character& who, double dx, double dy);
    void tickPlayer();
    void swing();
    void tickEnemy(Actor& enemy);
    // Monster behaviour from Control.aid (enemy_ai.cpp).
    void tickEnemyAi(Actor& enemy);
    bool chooseAiAction(Actor& enemy, int event);
    void runAiAction(Actor& enemy);
    double gapToPlayer(const Actor& enemy) const;
    // A blow from the player or from a monster: it may miss, otherwise it
    // does the original's damage.
    void strikeEnemy(Actor& enemy);
    void sayBroken(const std::vector<EquipSlot>& slots);  // a message for each item that broke
    // A monster's attack (slot 0-2) or spell (slot 0-2, `magic`).
    void strikePlayer(Actor& enemy, int slot = 0, bool magic = false);
    // `bySpell` counts the kill as a magic one for the class offer.
    // byPartner: the companion's bite, which the class kill counts skip.
    void hurtEnemy(Actor& enemy, int damage, bool bySpell = false, bool byPartner = false);
    // A blow or spell that got through, before the shields (0x443cb0).
    // `flinch`: whether it plays the hero's hit pose (traps do not, see
    // docs/traps.md).
    void hurtPlayer(int damage, bool magic = false, Actor* attacker = nullptr, bool flinch = true);
    std::optional<std::size_t> enemyAt(float screenX, float screenY) const;
    void renderHealthBars(int camX, int camY);
    void renderMap();
    void renderChips(int camX, int camY);
    void drawObject(const MapObject& o, bool shadows, int camX, int camY, int shadowAlpha);
    void renderIsoGround(int camX, int camY);
    void renderJudge(int camX, int camY);
    void renderPlaceholder(double interpolation);

    // Items, the inventory panel and loot (items.cpp).
    struct GroundItem {
        Item item;
        double x = 0, y = 0;  // world units
        std::optional<Character> sprite;  // when its picture is an animation chart
        bool waitForLeave = false;  // not picked up until the player steps away
    };
    void loadItems();
    void giveStartingKit();
    void spawnGroundItems();
    void dropItem(const Item& item, double x, double y);
    void dropLoot(const Actor& enemy);
    // Drops what a drop list gives around (x, y): monsters and
    // CREATEITEMTABLE.
    void dropFromList(int list, double x, double y);
    void pickUpItems();
    void updateEquipment();
    // The hero's stats with what they wear added, as the original sums them
    // (0x44ea60).
    HeroStats heroStats() const { return sf::heroStats(career_.history, heroSex_); }
    // Everything counted: equipment, element levels and Berserker.
    HeroStats playerStats() const;
    HeroStats playerStats(int& chantSpeed) const;  // and the chant speed with them
    ElementLevels playerElements() const;
    int playerAttack() const { return playerStats()[HeroStats::kAttack]; }
    int playerDefense() const { return playerStats()[HeroStats::kDefense]; }
    int playerHit() const { return playerStats()[HeroStats::kHit]; }
    int playerEvasion() const { return playerStats()[HeroStats::kEvasion]; }
    int playerMaxHp() const { return playerStats()[HeroStats::kHp]; }
    int playerMaxMp() const { return playerStats()[HeroStats::kMp]; }
    void loadLevelCurve();
    void gainExperience(int amount);
    void useItem(int index);
    // A consumable's effect on the hero or companion; false when it would
    // do nothing, so it is not used up.
    bool applyConsumable(const ItemDef& def);
    // The belt pocket on the HUD: keys 1-8, clicks, drawing.
    void useBelt(int x, int y);
    bool beltClick(int x, int y, bool right);
    void renderBelt();
    void showMessage(std::string text);
    bool inventoryClick(int x, int y, bool right);
    void dropHand();
    std::optional<std::size_t> groundItemAt(float screenX, float screenY) const;
    const SpriteBank* itemBank(const std::string& stem);
    void drawItemPicture(const ItemDef& def, const SDL_Rect& area);
    bool drawPanelPicture();
    void renderGroundItems(int camX, int camY);
    void renderInventory();
    void renderItemTotals();
    void renderItemPopup(const Item& item, int mx, int my);
    // The colours the item and status windows write numbers in (0x405590):
    // plain, lowered by what is worn (red) and raised by it (tan).
    enum Tone { kPlain, kLowered, kRaised };
    static Tone toneOf(int value, int base) { return value < base ? kLowered : value > base ? kRaised : kPlain; }
    void windowText(int x, int y, std::string_view text, SDL_Color color);
    void windowNumber(int x, int y, int value, int tone);
    // The status window and its Change Career button (status.cpp).
    bool statusClick(int x, int y);
    void renderStatus();
    void changeCareer();
    void renderHud();

    // Spells (spells.cpp).
    //
    // An effect is one of the original's OPTION animations
    // (Character/OPTION/<id>/Animation) playing in the world: a glow, a
    // missile or a burst. Those that hurt carry the spell's power. Times
    // are the original's ticks, 30 a second; tickSpells runs 1.5 of them
    // per engine tick.
    struct Effect {
        int id = 0;
        int option = -1, chart = 0;  // Character/OPTION folder, -1 for none
        double x = 0, y = 0;
        int age = 0, life = 0;  // life -1: until it hits something or a wall
        double speed = 0, angle = 0;  // world units a tick; radians, in world space
        double turn = 0;              // homing: how far it may turn a tick, 0 = flies straight
        int animSpeed = 1000;         // animation frames per 1000 ticks
        bool directional = false;     // drawn facing its angle
        bool followsPlayer = false;
        int followsActor = -1;  // an index into actors_ (a freeze's ice)
        bool followsPartner = false;
        int then = -1;          // played where it stands when its life runs out
        // Hurting: spell < 0 for effects that only show.
        int spell = -1, level = 1, power = 0, hit = 0;
        double box = 0;            // half the side of the square it hurts in
        bool area = false;         // hurts everyone in its box (each once), else the first
        bool diesOnHit = false;
        bool diesOnWall = false;
        int hitOption = -1;        // played on whoever it hurts
        // The Voice00.Voc sound where it is when a hit lands (struct words
        // 0x76 and 0x77), at most once a tick; -1 for none.
        int hitSound = -1;
        double travelled = 0;
        std::vector<std::size_t> struck;  // actors already hurt
        // A picture of the OPTION folder's Pattern bank instead of its
        // animation (the mine on the ground), -1 = the animation.
        int pattern = -1;
        int light = 1000;   // how bright a Pattern picture is drawn, 0-1000
        double height = 0;  // above the ground, as the original's objects (+0x124)
        int facing = -1;    // a fixed direction 0-8 to draw it in; -1: see `directional`
        std::int32_t follows = -1;  // a character it stays on (CREATEEFFECTCHAR), 0-3 the players
        // A trap's shot (CREATEOPTIONANGLE), which hurts the hero and the
        // companion instead of monsters (traps.cpp).
        struct Trap {
            TrapShot shot;
            int kind = 0;
            int power = 0;
            bool fixedDamage = false;
            ScriptHost::TrapFire::Stun stun;
        };
        std::optional<Trap> trap;
        bool dead = false;
    };
    // A spell on its way: spawns its effects tick by tick, as the
    // original's spell controllers do (0x42b1c0 and its neighbours).
    struct SpellCast {
        int spell = 0, level = 1, power = 0, hit = 0;
        double x = 0, y = 0;  // the caster, when the cast began
        double aim = 0;       // towards the point cast at, radians in world space
        int tick = 0;
        int release = -1;  // the tick the spell left the hand, -1 = not yet
        int step = 0;      // bursts made so far
        std::array<bool, 8> blocked{};
        std::vector<int> tracked;  // effects followed (missiles, orbs, blades)
        std::vector<std::pair<double, double>> trackedAt;
        std::vector<int> stage, stageTicks;  // Elemental Strike, per orb
        bool done = false;
    };
    void startCast(double worldX, double worldY);
    void releaseSpell();
    void tickSpells();
    // Whether the spell beat the monster's magic evasion.
    bool spellHits(Actor& enemy, int spell, int level, int power, int hit, int hitOption);
    void showSpellHit(int spell, double x, double y);
    const CharacterModel* effectModel(int option);
    int effectLength(int option, int chart = 0);
    Effect& spawnEffect(int option, double x, double y, int chart = 0);
    Effect* findEffect(int id);
    void stepCast(SpellCast& cast);
    void stepEffect(Effect& effect);
    void clearSpells();
    void trainSpell(int spell);
    // The level a spell is cast at: its own, two higher in power mode, plus
    // the Magic Level modifiers worn (0x451e60).
    int castLevel(int spell) const {
        return spells_.castLevel(spell, power_.active(), inventory_.modifier(ItemDef::kModMagicLevel));
    }
    // A spell's MP cost, less the Mental Cost modifiers worn (0x44f610).
    int mpCost(int spell, int level) const {
        return spellMpCost(magic_, spell, level, inventory_.modifierEvenBroken(ItemDef::kModMentalCost));
    }
    // The hero's chant speed (player + 0x16c): 80, the Chant Speed
    // modifiers worn, element pairs and Berserker (0x44ea60).
    int playerChantSpeed() const;
    bool hasSpiritStone() const { return inventory_.carries(ItemDef::Class::Special, PowerMode::kSpiritStone); }
    // The power button on the panel (power.cpp) and the glow round the hero
    // while the mode runs.
    void renderPowerButton();
    bool powerButtonClick(int x, int y);
    const CharacterModel* powerModel();
    void renderPowerAura(int camX, int camY);
    // An effect animation's first chart, looping, drawn round the hero.
    void renderHeroOverlay(const CharacterModel* model, double ticks, int camX, int camY);

    // Scenario objects: the chests, doors, levers and gates a scenario
    // places (objects.cpp). Each has three states, kept in the NET flags
    // 100,000,000, 300,000,000 and 200,000,000 + its id until
    // ABSOLUTEOBJECTFLAG fixes them: whether it is shown, whether it can
    // be used (clicked), and whether its rectangle stands in the way
    // (0x45e080).
    struct SceneObject {
        std::size_t placement = 0;  // into control_.placements
        std::optional<std::array<int, 3>> fixed;  // set by ABSOLUTEOBJECTFLAG
        std::optional<Character> animation;       // for a chart rather than a still picture
        const SpriteBank* body = nullptr;
        const SpriteBank* shadow = nullptr;
        int prop = -1;  // open content: frame of Map/props.sheet
    };
    enum ObjectState { kShown = 0, kUsable = 1, kBlocks = 2 };
    void spawnObjects();
    void tickObjects();
    bool objectState(const SceneObject& o, ObjectState which) const;
    SceneObject* sceneObject(std::int32_t id);
    const SceneObject* sceneObject(std::int32_t id) const;
    const ScenarioControl::Placement& placementOf(const SceneObject& o) const { return control_.placements[o.placement]; }
    // Whether a step from one world position to another walks into an
    // object that stands in the way (0x414990); starting inside one does
    // not trap anyone.
    bool objectInTheWay(double fromX, double fromY, double toX, double toY) const;
    // Whether a box at a spot meets an object that stands in the way, as a
    // shot's does (0x45e1e0: status bit 0, which is that state).
    bool boxMeetsObject(double x, double y, const JudgeRect& box) const;
    // Where the object sorts among the map's pieces, as MapObject::depth.
    MapObject objectFootprint(const SceneObject& o) const;
    void drawSceneObject(const SceneObject& o, bool shadows, int camX, int camY, int shadowAlpha);
    bool workingObject(std::int32_t id) const;
    // The magic window (magic_window.cpp), the STATUS window's other tab:
    // the spells learned, six to a page, and the eight places of the
    // magic/attack bar that spells are dragged to.
    void toggleMagicWindow();
    void closeMagicWindow();
    bool magicWindowClick(int x, int y, bool right);
    void magicWindowRelease(int x, int y);
    void renderMagicWindow();
    bool spellOn(int spell) const { return spellOn_[static_cast<std::size_t>(spell)]; }
    void setSpellOn(int spell, bool on) { spellOn_[static_cast<std::size_t>(spell)] = on; }
    int spellPower(int spell, int level) const;
    int spellHit(int spell, int level) const;
    void renderEffect(const Effect& effect, int camX, int camY);
    void renderSpells(int camX, int camY);

    // Traps, effects, rain and story pictures that scenario scripts start
    // (traps.cpp, docs/traps.md).
    void fireTrap(const ScriptHost::TrapFire& fire);
    void stepTrap(Effect& e);
    void trapHitsPlayer(const Effect& e);
    void trapHitsPartner(const Effect& e);
    // A stun from a trap's blow (0x443cb0, 0x45f9f0): held in the hit pose
    // for `total` original ticks, pushed away from the blow unless steady.
    struct Stun {
        int total = 1, tick = 0;
        double angle = 0;  // radians, world y up, from the blow to the one hit
        bool pushed = false;
        double carry = 0;  // original ticks still to run
    };
    std::optional<Stun> trapStun(const Effect& e, double x, double y, int chanceResist, int lengthResist, bool gearSteady, int damage,
                                 int maxHp);
    bool tickStun(Character& who, Stun& stun);
    void trapSplash(const Effect& e, double x, double y, bool onHero);
    std::optional<Stun> heroStun_;
    void createScriptEffect(int number, double x, double y, int height, int direction, std::int32_t follows);
    int directionFrames(int option, int direction);
    std::optional<std::pair<double, double>> characterAt(std::int32_t id) const;  // players 0-3 too
    void resurrect(std::int32_t enemy, int x, int y, int direction);
    struct RainDrop {
        int age = 0;
        int x = 0, y = 0;  // screen pixels it starts from
        double angle = 0;  // radians, screen y up
        int speed = 0;     // hundredths of a pixel an original tick
        int length = 0;
        int brightness = 0;  // per mille
        Uint8 r = 0, g = 0, b = 0;
    };
    std::vector<RainDrop> rain_;
    double rainCarry_ = 0;
    void addRain(int r, int g, int b, int count);
    void tickRain();
    void renderRain();
    // STARTVISUAL's picture: pages of System/Common/Pattern/Visual<n>.njp
    // (Waiting.njp for 0), each fading in over 120 original ticks and
    // shown for 300 before a click or key turns to the next (0x417bd0).
    struct Visual {
        int picture = 0;
        int page = 0;
        double ticks = 0;  // original ticks the page has shown
    };
    std::optional<Visual> visual_;
    void startVisual(int picture);
    void tickVisual();
    bool visualEvent(const SDL_Event& event);
    void renderVisual();

    // Sound (sound.cpp): Voice00.Voc's effects in bank 0 and one piece of
    // music in bank 500, as the original keeps them.
    Mixer mixer_;
    void loadSounds();
    // A sound effect where something happens (0x417260): only heard
    // within 3000 units of the hero unless `everywhere`.
    void playSound(int voice, double x, double y, bool everywhere = false);
    // A sound heard wherever the hero is (the menus' clicks).
    void playSound(int voice);
    // The area's music, BGM<areaKind>.Voc, on a scenario change (0x4275e0).
    void startAreaMusic(int areaKind);
    // Plays the music: the title's on the title screens, otherwise the
    // area's, started again from the top after each scenario change
    // (0x4275a0).
    void tickMusic();
    void loadMusic(const std::string& path);
    std::string musicFile_;   // what bank 500 holds
    int areaMusic_ = -1;      // the area's BGM number, -1 for none
    int musicTicks_ = 0;      // ticks since the area's music started
    double everyTickCarry_ = 0;  // event 5 runs once an original tick

    // Land mines (mines.cpp): B or the MINE button lays one where the hero
    // stands; a controller, as the original's (0x42bd40), arms it, sets it
    // off and plays the blast.
    struct Mine {
        double x = 0, y = 0;
        int tick = 0;        // the original's ticks since it was laid, then since the blast
        int effect = 0;      // the mine on the ground; 0 once it has gone off
        bool blasted = false;
        int level = 1;       // the hero's when it was laid, for its power
        int ring = 0;        // how far out the last smoke ring was
        std::array<int, 4> pieces{}, fires{};
        int lift = 0;        // the pieces' upward speed
        bool done = false;
    };
    void layMine();
    void stepMine(Mine& mine);
    void mineBlast(Mine& mine);
    int mostMines() const;
    int minePower(int level) const;
    // Picking up a mine adds it to the count when there is room (0x44a0e2).
    bool pickUpMine();
    bool mineButtonClick(int x, int y);
    void renderMineButton();

    // The hero's companion (partner.cpp).
    struct Partner {
        explicit Partner(Character c) : character(std::move(c)) {}
        Character character;
        PartnerStats stats{};
        int hp = 1;
        enum class State { Idle, Walk, Run, Bite, Hit, Dead, Jump, Dash } state = State::Idle;
        std::optional<std::size_t> target{};  // the monster it fights, in actors_
        int walkDelay = 5;   // ticks before it starts walking after the hero
        int regenCarry = 0;  // thousandths of a point of health
        int deadTicks = 0;   // since it fell, for the fade
        bool struck = false;  // the current bite has landed

        // The hero's orders, Tab and a click (partner.cpp): where it jumps
        // or dashes to, and what it jumps on or fetches.
        enum class Order { None, Fetch, Jump, Dash, Explode } order = Order::None;
        double orderX = 0, orderY = 0;
        std::optional<std::size_t> orderActor{};   // a monster to land on, in actors_
        std::optional<std::size_t> orderObject{};  // a switch to land on, in control_.placements
        int orderCooldown = 0;  // ticks until it takes a new order on open ground (+0x210)
        int skillTick = 0;      // ticks into the jump or dash (+0x158)
        bool landed = false;    // the jump has come down
        bool explosion = false; // the jump is the hero's Explosion spell (state 10)
        double dashAngle = 0, dashSpeed = 0;
        int hitPause = 0;       // the dash stops a moment after a hit (+0x250)
        int restDirection = 0;  // its facing before a jump
        std::optional<Stun> stun;  // a trap's stun holds it in its hit pose
    };
    void spawnPartner();
    void changePartner(int kind);
    void refreshPartnerStats();
    void togglePartnerMode();
    bool partnerAlive() const;
    void tickPartner();
    void partnerBites(Actor& enemy);
    void strikePartner(Actor& enemy, int slot);
    bool partnerInReach(const Actor& enemy) const;
    void hurtPartner(int damage);
    void partnerCountKill();
    double partnerSpeed(int stat) const;
    PartnerStats partnerStats() const;
    std::string partnerStatusText(int kind, bool withItems = false) const;
    // Tab and a click: orders for the companion. True when it took the click.
    bool partnerOrder(float screenX, float screenY, bool right);
    bool partnerAt(float screenX, float screenY) const;
    void startPartnerJump();
    void tickPartnerJump();
    void tickPartnerDash();
    bool tickPartnerFetch(double toHero);
    void partnerLands(double left, double top, double right, double bottom, bool dash);
    void partnerExplodes();
    void orderPartnerExplosion(double x, double y);
    std::vector<std::size_t> monstersIn(double left, double top, double right, double bottom) const;
    bool partnerCanStand(double x, double y) const;
    void stunEnemy(Actor& enemy, int chance, int length, bool uncapped, int attackerElement = -1,
                   StunKind kind = StunKind::Plain);
    void endStun(Actor& enemy);
    void renderPartnerGauge();
    bool partnerGaugeClick(int x, int y);
    // The game panel along the bottom of the screen (panel.cpp).
    void renderPanel();
    bool panelClick(int x, int y);
    bool hasPanelArt();
    // The magic/attack bar (spells.cpp): where each icon is this frame.
    struct BarIcon {
        int x, y, size;
        int spell;    // -1 an empty slot, kAttackIcon the attack
        bool chosen;  // drawn large
    };
    static constexpr int kAttackIcon = 1000;
    std::vector<BarIcon> spellBarLayout() const;
    bool spellBarClick(int x, int y);
    void renderSpellBar();
    void renderMenu();
    void menuEvent(const SDL_Event& event);
    int menuItemAt(int x, int y) const;
    bool menuItemEnabled(MenuItem item) const;
    void chooseMenuItem(MenuItem item);
    void newGame();

    // The Create New Character screen (new_character.cpp), as ShadowFlare.exe
    // runs it (0x423ca0 and the mode window 0x424d80).
    struct NewCharacter {
        enum class Step { Choose, Name, Mode, Starting, Leaving };
        Step step = Step::Choose;
        int hover = 0;          // Choose: 1 the hero, 0 the heroine; Mode: 0 online, 1 single, 2 back
        HeroSex chosen = HeroSex::Male;
        int fade = 0;           // ticks into the fade of the figure not chosen (0 = none)
        int slideBack = 0;      // ticks into the chosen figure sliding back after Back
        int ticks = 0;          // ticks into Starting or Leaving
        std::string name;
        bool mouseMoved = true;  // the mouse, not the keys, sets the hover
    };
    void tickNewCharacter();
    void newCharacterEvent(const SDL_Event& event);
    void renderNewCharacter();
    void newCharacterClick(int x, int y);
    void newCharacterKey(SDL_Keycode key);
    void newCharacterHover(int x, int y);
    void newCharacterPattern(int pattern, int alpha = 255, int dx = 0);

    // Missions (missions.cpp): the mission list (Q), the newest mission
    // named along the bottom of the screen, and the area's name by the
    // compass at the top left when the player arrives.
    void loadMissions();
    int missionState(int mission) const;  // its QUEST flag: 0 not given, 1 taken, 2 cleared
    bool anyMissionTaken() const;
    void showMissionName(int mission);
    void showAreaName(std::string name);
    void toggleMissions();
    void closeMissions();
    bool missionsClick(int x, int y, bool right);
    bool missionHudClick(int x, int y);
    void tickMissions();
    void renderMissions();
    void renderMissionHud();
    void renderCompass();
    const CharacterModel* compassModel();

    // The GATE window (warp_gate.cpp), where ShadowFlare.exe draws it and
    // takes its clicks (0x40c950): the warp gates found, ten a page, in the
    // left half of the screen; a click on one goes there. A gate's
    // WARPGATE opens it and its DELETEWARPGATE closes it.
    void loadWarpGates();
    std::vector<int> warpGatesFound() const;  // gate numbers, in the window's order
    void closeWarpGate(int gate);  // closes the window if `gate` opened it
    void closeWarpWindow() { warpOpen_ = false; }
    bool warpGateClick(int x, int y, bool right);
    void renderWarpGate();

    // Darkness in dungeons (darkness.cpp), as ShadowFlare.exe draws it in
    // its frame code (0x41e4f0): the screen is lit by an ellipse round the
    // hero and by the lights some characters carry, dimmed by the area's
    // light level. Display Darkness off, or a fully lit area, skips it.
    bool darknessShown() const;
    void renderDarkness(int camX, int camY);
    const LightMap* darkPart(int part);  // Dark.njp's parts, or ours

    // The map (N), as the original's window (0x40d4d0): the area at a
    // tenth of its size in the left half of the screen, only what the hero
    // has seen, with them blinking in the middle (area_map.cpp).
    void toggleAreaMap();
    void closeAreaMap() { mapOpen_ = false; }
    bool areaMapClick(int x, int y, bool right);
    bool areaMapKey(SDL_Keycode key);
    void tickAreaMap();
    void renderAreaMap();
    // Where the hero is, in the map's pixels (a tenth of the screen's,
    // from the ground's top left corner).
    std::pair<int, int> mapPosition(double worldX, double worldY) const;
    std::string exploredKey() const;  // the area the map remembers: its scenario, else the map
    ExploredMap& explored();
    // The area drawn small: the original's Scenario.Njp when the map is the
    // original's own art, else ours, drawn from the map itself.
    void drawAreaMapPicture(int left, int top);
    void buildAreaMapPicture();
    void renderScenery(int camX, int camY);  // ground and objects, no one on them

    // The Settings Menu and Help window (options.cpp).
    enum class OptionRow {
        ScreenMode, SeeThroughPlayer, SeeThroughShadows, Darkness, SaveImage, ClickRange, ClickSize, ClickPriority,
        EffectVolume, MusicVolume, OriginalMode, MissionList, Map, Help, SaveAndTitle, SaveAndExit, Back
    };
    std::vector<OptionRow> optionRows() const;
    bool optionEnabled(OptionRow row) const;
    static const char* optionLabel(OptionRow row);
    static bool optionIsSlider(OptionRow row);
    std::vector<std::string> optionChoices(OptionRow row, int& chosen) const;
    SDL_Rect optionChoiceRect(int row, int choice);
    void setOption(OptionRow row, int index);
    void chooseOption(OptionRow row);
    void closeOptions();
    void optionsEvent(const SDL_Event& event);
    void renderOptions();
    void renderHelp();
    void saveSettings();
    void applyScreenMode();
    void takeScreenshot();
    // Closes the inventory panel, putting a held item back or dropping it.
    void closeInventory();
    // A key from the manual for something the engine does not have yet.
    void notYet(const char* what);
    // What a left click at a point on the screen is aimed at, by the click
    // priority. Only the kinds in `kinds` (bits of Settings::Target) count.
    struct ClickTarget {
        int kind = -1;  // Settings::Target, or -1 for the ground
        std::size_t index = 0;
    };
    ClickTarget clickTarget(float screenX, float screenY, unsigned kinds) const;
    // A point within the click range of something drawn at (sx, sy).
    bool inClickRange(double dx, double dy) const;

    SDL_Renderer* renderer_;
    const FileSystem* data_;
    Skin* skin_ = nullptr;
    SpriteCache sprites_;
    std::vector<std::string> mapNames_;
    std::size_t mapIndex_ = 0;
    std::optional<Map> map_;
    std::vector<std::size_t> drawOrder_;  // objects sorted back to front

    std::map<std::string, std::string> scenarioByMap_;  // lower-case map name -> Scenario/<dir>
    std::map<std::string, std::unique_ptr<CharacterModel>> models_;  // by path stem; null = failed
    std::vector<Actor> actors_;
    std::vector<SceneObject> sceneObjects_;
    std::map<std::string, std::unique_ptr<SpriteBank>> objectShadows_;  // by path stem; null = missing
    bool unlockSwitch_ = false;  // SETUNLOCKSW this frame
    std::int32_t heldObject_ = -1;  // --hold
    double unlockTicks_ = 0;
    std::optional<AiControl> ai_;
    Random random_;
    std::string scenarioDir_;  // Scenario/<dir> of the current map, or empty
    std::set<std::int32_t> skippedCommands_;  // script commands not carried out, logged once per scenario
    ScenarioControl control_;
    std::optional<ScenarioScript> script_;
    std::vector<TriggerArea> triggers_;
    ScriptFlags flags_;
    std::optional<std::pair<int, int>> pendingScenario_;  // scenario number, entry

    std::array<std::optional<CharacterModel>, 2> playerModels_;  // by HeroSex
    HeroSex heroSex_ = HeroSex::Male;
    std::string heroName_;
    std::optional<NewCharacter> newCharacter_;
    std::optional<Character> player_;
    std::optional<std::pair<double, double>> walkTarget_;  // world units
    std::optional<std::size_t> attackTarget_;              // index into actors_
    std::optional<std::size_t> talkTarget_;                // a person the player is walking up to
    std::optional<std::size_t> objectTarget_;              // a map object likewise, in control_.placements
    std::vector<Label> labels_;
    int playerHp_ = 0;
    int playerMp_ = 0;
    MagicTables magic_ = MagicTables::standard();
    SpellBook spells_;
    std::array<bool, kSpellCount> spellOn_{};  // toggle spells that are on
    PowerMode power_;
    double powerAuraTicks_ = 0;
    bool blowLanded_ = false;  // the hero's current swing has struck  // original ticks into the glow (+0x1558)
    bool magicOpen_ = false;
    int magicPage_ = 0;        // 0-3, six spells each (0xa93c)
    int dragSpell_ = -1;       // the spell being dragged to the bar (0xa710)
    int berserkerCarry_ = 0;
    int moonLevel_ = 0;  // the level Moon was cast at (+0x15e8)
    int moonCarry_ = 0;
    int moonGlow_ = 0;   // the effect id of the glow on the companion
    std::optional<int> casting_;               // the spell being cast
    std::pair<double, double> castTarget_;     // where, in world units
    bool castReleased_ = false;
    std::vector<SpellCast> casts_;
    std::vector<Effect> effects_;
    std::vector<Effect> pendingEffects_;  // spawned this tick
    std::map<int, std::unique_ptr<CharacterModel>> effectModels_;  // by OPTION id; null = missing
    int nextEffectId_ = 1;
    double originalTicks_ = 0;  // carried between engine ticks
    std::vector<Mine> mines_;
    int mineCount_ = mines::kStartCount;  // mines carried (player + 0x328)
    int mineWait_ = 0;                    // ticks before another can be laid
    Rules rules_;
    std::optional<PartnerTables> partnerTables_;  // none with open content
    PartnerRecord partnerRecord_;
    std::optional<Partner> partner_;
    int partnerHpCarried_ = 0;  // its health across a map change (+0x156c); 0 = full
    bool mouseHeld_ = false;
    bool showJudge_ = false;
    bool autoFight_ = false;

    ItemTable items_;
    Inventory inventory_;
    std::optional<Item> hand_;  // picked up in the inventory panel
    std::vector<GroundItem> groundItems_;
    std::map<std::string, std::unique_ptr<SpriteBank>> itemBanks_;  // by path stem; null = missing
    bool showInventory_ = false;
    bool drawnPanel_ = false;  // the item window has a picture this frame
    // The weight gauge's marks as last drawn (-1 until it is first shown),
    // whether they had caught up with the weight, and the tick they last
    // moved on (0x4074e0).
    int weightGaugeShown_ = -1;
    bool weightGaugeSettled_ = true;
    std::uint64_t weightGaugeTick_ = 0;
    // Identify was cast: the next item clicked in the bag is identified
    // (player + 0x1624).
    bool identifying_ = false;
    int playerLevel_ = 1;
    std::string saveFile_ = "save.txt";
    std::int64_t playerExperience_ = 0;
    LevelCurve levels_;
    GameTables tables_;  // Table.Tbd; empty with open content
    ModifierPrices modifierPrices_;  // Table.Tbd table 34
    open::ShopLists openShops_;  // shops.txt, INITTRADE's lists with open content
    DropTables dropTables_;  // Table.Tbd tables 30 and 31, or drops.txt
    Career career_;  // the hero's class, its history and the class on offer
    bool showStatus_ = false;
    std::optional<Dialogue> dialogue_;
    std::map<int, ShopStock> shops_;  // by INITTRADE's shop number; kStorage is the warehouse
    static constexpr int kStorage = -1;
    std::optional<int> openShop_;
    int entryPoint_ = 0;
    std::vector<int> answers_;
    bool startScripts_ = false;  // the scenario's start triggers still have to run
    std::string message_;
    int messageTicks_ = 0;

    Missions missions_;
    bool missionsOpen_ = false;
    int missionPage_ = 0;      // the list's two pages of 24 (0xa910)
    int missionShown_ = -1;    // the mission whose description is open (0xa97c)
    int missionBanner_ = -1;   // DRAWQUESTNAME's mission (0xa908)
    double missionBannerTicks_ = 0;  // original ticks left to show it (0xa90c)
    std::string areaName_;
    double compassAge_ = -1;   // original ticks since SETCOMPASSES (0xa800); -1 hidden

    // Transport (transport.cpp): the gate the spell opens between a field
    // or dungeon and its town (0x420020), the hero's only one. Ends are 0
    // the field's and 1 the town's.
    struct TransportGate {
        int field = 0;        // the scenario it was cast in (+8)
        double x = 0, y = 0;  // where it stands there (+0xc)
        int town = 0;         // the scenario of that area's town (+0x14)
        int age = 0;          // the original's ticks since it was cast (+0x20)
        // Its four pieces drop into place 7 ticks apart (+0x24, +0x34).
        std::array<int, 4> height{300, 300, 300, 300}, light{};
        std::array<int, 2> wait{}, frame{};  // its animation at each end (+0x54, +0x5c)
        std::array<bool, 2> standing{true, false};  // the hero is on that end (+0x64)
        bool assembled() const { return height[3] == 0; }
    };
    std::optional<TransportGate> transportGate_;
    double transportTicks_ = 0;
    // Where a scenario change puts the hero instead of an entry point.
    std::optional<std::pair<double, double>> pendingPosition_;
    int scenarioNumber() const;  // of the current area, -1 without one
    bool transportAllowed() const;
    void openTransportGate();
    void tickTransportGate();
    // The gate's end in this area, if it has one here.
    std::optional<std::pair<double, double>> transportGateHere() const;
    std::vector<Effect> transportGateParts() const;  // its pieces and animation, to draw
    void renderTransportGateName(int camX, int camY);

    WarpGates warpGates_;
    bool warpOpen_ = false;     // the GATE window (0xa05c)
    int warpOpener_ = 0;        // the gate that opened it (0x48d72c keeps -1 - gate)
    int warpPage_ = 0;          // ten gates a page (0xa934), the first again in each scenario

    std::map<std::string, ExploredMap> explored_;  // by exploredKey()
    bool mapOpen_ = false;      // the map window (0xa070)
    int mapScrollX_ = 0, mapScrollY_ = 0;  // the arrow keys' scroll (0xa998, 0xa99c)
    double mapTicks_ = 0;       // original ticks the map has been open, for its pulse and blink
    // Our drawing of the area at a tenth of its size, made when the map
    // first opens on it.
    TexturePtr mapPicture_;
    std::string mapPictureFor_;
    int mapPictureW_ = 0, mapPictureH_ = 0;
    std::optional<SpriteBank> scenarioPicture_;  // Scenario.Njp
    std::string scenarioPictureFor_;
    std::array<std::optional<LightMap>, 7> darkParts_;
    TexturePtr darkTexture_;

    double cameraX_ = 0, cameraY_ = 0;  // top-left of the view, in map pixels
    std::uint64_t ticks_ = 0;
    bool quit_ = false;
    bool menuOpen_ = false;
    Settings settings_;
    std::string settingsFile_;
    bool optionsOpen_ = false;
    bool optionsInGame_ = false;
    int optionsChoice_ = 0;
    bool helpOpen_ = false;
    int hurtPercent_ = -1;  // --hurt
    bool runMode_ = false;  // R or the RUN button: run instead of walk
    // What the panel shows between frames (0x4039f0): the HP and MP gauges
    // follow the real values a few pixels a frame (+0xa028, +0xa02c), the
    // level's digits pop when it changes (+0xa038), a clicked button
    // flashes, and glints run along the gauges.
    struct PanelState {
        struct Glint {
            int age = 0, speed = 0, dy = 0;
        };
        std::uint64_t frame = 0;  // original frames (30 a second) shown so far
        int hpShown = 0, mpShown = 0;
        int levelCounter = -1;
        std::array<int, 3> flash{};  // STATUS, ITEM, MENU
        std::array<std::array<Glint, 60>, 2> glints{};
        std::uint32_t random = 1;
    } panel_;
    bool screenshotPending_ = false;
    std::vector<MenuItem> menuItems_;
    int menuChoice_ = 0;
    std::size_t startMapIndex_ = 0;
    int startScenario_ = -1;  // a scenario number to start in, else the start map
};

}  // namespace sf

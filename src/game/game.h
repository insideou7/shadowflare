#pragma once

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
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
#include "logic/card_game.h"
#include "logic/combat.h"
#include "logic/explored_map.h"
#include "logic/lighting.h"
#include "logic/item_labels.h"
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
#include "logic/title_smoke.h"
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
struct SaveGame;

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

// Top of the companion's health gauge, as in the original.
inline constexpr int kPartnerGaugeY = 395;
// The belt pocket's cells on the screen, as the original's clicks find them
// (0x445bd0, 0x44a5f0): the top row's left edge, and the bottom row's,
// which sits a cell and a half further right.
// How much brighter what is under the pointer is drawn, per mille
// (0x40ee70): a character, a chest, lever or door, or an item.
inline constexpr int kHoverGlow = 300;
// The light outline around a chest, lever, door or item under the pointer
// (ours; Rules::outlineUnderPointer).
inline constexpr SDL_Color kHoverOutline{255, 224, 140, 210};
inline constexpr int kBeltLeft = 357, kBeltTop = 413, kBeltBottomLeft = 405, kBeltCell = 32;

// For now the game is a map viewer with a walking player: it loads a level
// from the original data and lets you walk around it.
class Game {
public:
    // Loads a skin: the original's maps and characters drawn with an open
    // content pack's art where it has some. Called the first time our art
    // is asked for; null when there is none.
    using SkinLoader = std::function<std::unique_ptr<Skin>()>;
    Game(SDL_Renderer* renderer, const FileSystem* data, const std::string& startMap, SkinLoader skinLoader = {});
    ~Game();
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

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
    // Puts the pointer at a screen spot, or with `onKind` (a
    // Settings::Target) on the nearest monster, person or item, instead of
    // where the mouse is (for screenshots).
    void setPointer(std::optional<SDL_FPoint> at, int onKind = -1);
    // Swaps in a kind of companion and sets it ACTIVE (for scripted tests).
    void bringPartner(int kind);

    // Puts an item in the bag, on the hero or on the ground next to them
    // (for scripted tests). For gold, `count` is the amount. False when the
    // item is unknown, does not fit or cannot be worn.
    enum class GiveTo { Bag, Wear, Ground };
    bool give(int itemClass, int code, int count, GiveTo to, int wear = 0);  // wear: how worn it starts

    // Shows the inventory panel (toggled with I).
    void setInventoryOpen(bool open) { showInventory_ = open; }
    // Shows the special items window (toggled with X).
    void setSpecialWindowOpen(bool open) { specialOpen_ = open; }

    // Draws the walkability grid over the map (toggled with J).
    void setShowJudge(bool show) { showJudge_ = show; }

    // Shows the names of the items on the ground as if Alt were held.
    void setShowItemNames(bool show) { forceItemNames_ = show; }
    std::uint64_t tickCount() const { return ticks_; }

    // Starts the hero at a level, fully healed (for testing): a warrior from
    // level 5, as the original makes every hero, or `cls` from then on.
    void setLevel(int level, HeroClass cls = HeroClass::Warrior) {
        playerLevel_ = std::clamp(level, 1, levels_.maxLevel());
        career_ = Career::at(playerLevel_, cls);
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
    // The Debug tab's experience rate in percent; 100 in original mode.
    int xpRate() const { return originalMode() ? Settings::kXpRateNormal : settings_.xpRate; }
    // The Debug tab's luck in percent (unique drops); 100 in
    // original mode.
    int luck() const { return originalMode() ? Settings::kLuckNormal : settings_.luck; }

    // Draws the original game with our art or its own, at once and
    // without reloading the map, like a remaster's classic-graphics
    // button (F12, or Art in the Settings Menu). Only with the original
    // files and a pack that has a skin.txt; otherwise there is nothing to
    // swap and this does nothing.
    bool canSwapArt() const { return skinLoader_ || ownedSkin_; }
    bool ourArt() const { return skin_ != nullptr; }
    void setOurArt(bool on);
    // F12, in play or on the title menu: the other art, saved in the
    // options. False when there is no other art.
    bool swapArt();

    // Chooses the spell on F1-F8 (slot 0-7).
    void selectSpellSlot(int slot);
    // Casts the chosen spell at a point given relative to the player on
    // screen, as the right button does (for scripted tests).
    void castBy(int slot, int screenDx, int screenDy);
    void comboBy(int screenDx, int screenDy);  // a Warrior's combo towards a point (for tests)
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
    // A shop's window by INITTRADE's number, or a warehouse (kStorage,
    // kGiantStorage), as TRADE and STORAGE open them.
    void openTrade(int shop);
    void openStorageWindow(bool giant) { openTrade(giant ? kGiantStorage : kStorage); }

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
    // Where pictures of the original's maps for the area map (N) are kept
    // once drawn, so each is drawn only once.
    void setMapPictureFolder(std::string folder) { mapPictureFolder_ = std::move(folder); }
    // Draws every map's picture for the area map into `folder` as
    // <map>.png (Map/Overview in an open pack), or with no folder where
    // the game keeps them (setMapPictureFolder); returns how many.
    int renderMapPictures(const std::string& folder);
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
    // Holds the left button down at the pointer (--hold-button, with --pointer).
    void pressLeftButton() {
        pressed_ = true;
        mouseHeld_ = true;
    }
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
    // STARTCARDGAME (card_game.cpp) and CHANGECOLOR (change_color.cpp), as
    // the scripts open them; also --card-game and --change-color.
    void toggleCardGame();
    void openChangeColor();

    // Saves the game to a text file (F10) or loads it back (F11). False, with
    // a message on screen, when that fails. Each hero has their own file, as
    // in the original (Save\0000.Ssv to 0005.Ssv): with a save folder, six
    // of them, save1.txt to save6.txt, each with a picture of where it was
    // saved (save1.bmp...); a new hero takes the first free one when first
    // saved. setSaveFile instead names one file for everything.
    void setSaveFile(std::string path) { saveFile_ = std::move(path); }
    void setSaveFolder(std::string folder);
    bool saveGame();
    // The current hero's save, or with none chosen yet the newest one.
    bool loadGame();

    // The Load Data screen that Continue opens (load_data.cpp), as
    // ShadowFlare.exe runs it (0x422e30): the six saves, a picture of the
    // chosen one, Delete, Back and OK, then the game mode.
    void openLoadData();
    bool loadDataOpen() const { return loadData_.has_value(); }

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
        double actionTicks = 0;  // the original's ticks since the action began
        int event = AiControl::kDecide;
        AiControl::Entry entry;
        bool struck = false;  // the current attack has landed
        double aim = 0;       // where its spell goes, radians in world space (monster + 0x260)
        double wanderX = 0, wanderY = 0;
        double wanderPause = 0;
        bool active = true;   // SETINACTIVE hides a character
        bool stopped = false;  // C_STOP holds it still while it talks
        // A person's C_ACTIONREQ action: the chart it plays, and the frames
        // it repeats when it loops (0x45d850).
        struct ScriptedAction {
            int chart = 0;
            bool loop = false;
            int from = -1, to = -1;
        };
        std::optional<ScriptedAction> scripted;
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
    void renderLabels();
    // Where a character or a map object is, in world units.
    std::optional<std::pair<double, double>> positionOf(std::int32_t id) const;
    const ScenarioControl::Placement* placementById(std::int32_t id) const;
    // A map object with a talk script (the warehouse) under the mouse,
    // as an index into control_.placements.
    std::optional<std::size_t> objectAt(float screenX, float screenY) const;
    // Any shown, usable map object under the mouse, talk script or not
    // (a CCHECK switch has none).
    std::optional<std::size_t> usableObjectAt(float screenX, float screenY) const;
    std::optional<std::size_t> objectAt(float screenX, float screenY, bool talks) const;
    bool shopClick(int x, int y, bool right);
    // The shop takes the item in hand and pays for it; false, with the
    // item still in hand, when it will not.
    bool sellHand();
    // Right click on a bag item while a shop is open (Rules::rightClickSells).
    void sellFromBag(int index);
    void renderShop();
    bool drawShopPicture();
    void closeShop();
    // The warehouse window open, either one, and the goods it shows: the
    // warehouse's, or the Giant Warehouse's page in view.
    bool storageOpen() const { return openShop_ == kStorage || openShop_ == kGiantStorage; }
    ShopStock& openStock();
    // The Giant Warehouse's tabs and close button (0x447ca0); true when the
    // click was on its header.
    bool giantStorageHeaderClick(int x, int y);
    void renderGiantStorageTabs();
    // Whether page `page` of the Giant Warehouse can be used: I and II
    // always, the others once a script sets EXSTRG n.
    bool giantPageOpen(int page) const;
    std::optional<std::size_t> personAt(float screenX, float screenY) const;
    Actor* actorById(std::int32_t id);
    const Actor* actorById(std::int32_t id) const;
    void talkTo(Actor& person);

    void loadMap(std::size_t index, const std::string& scenarioDir = {});
    void loadMapLayers(std::size_t index);
    void loadScenario(const std::string& dir);
    // Puts the scenario being left in visited_, unless the game is being
    // started or loaded (forgetVisitedScenarios).
    void keepScenario();
    void forgetVisitedScenarios();
    void indexScenarios();
    bool readScenario(const std::string& dir, ScenarioControl& control, std::optional<ScenarioScript>* script) const;
    void spawnActors();
    // Every layer of a model's first chart: what non-player characters wear.
    static std::vector<int> allLayers(const CharacterModel& m);
    void changeScenario(int number, int entry);
    void goToStart();
    void checkTriggers(bool fire = true);
    const CharacterModel* model(const std::string& stem);
    // The player's figure for `sex` in the art being drawn, or the hero's
    // when the pack has no heroine. Each is loaded the first time it is
    // asked for: our sheets are big.
    const CharacterModel* heroModel(HeroSex sex);
    void placePlayer();
    void updateCamera();
    // The world's zoom (Settings::zoom, the scroll wheel): 1 draws it as
    // the original does, 0.5 at half size, showing twice as much of the
    // map each way. The panel, the windows and the pointer keep their size.
    // Original mode keeps it at 1, as do the save's picture and the area
    // map's (fullSize_).
    double zoom() const;
    void zoomBy(int notches);
    // How much of the map the screen shows at that zoom, in map pixels.
    int viewWidth() const;
    int viewHeight() const;
    // The map pixel (as worldToScreen gives it) under a point on the
    // screen, and where a map pixel is on the screen.
    double pixelX(double screenX) const { return cameraX_ + screenX / zoom(); }
    double pixelY(double screenY) const { return cameraY_ + screenY / zoom(); }
    SDL_FPoint onScreen(double px, double py) const;
    // worldTexture_, made the size of the view; null if it cannot be.
    SDL_Texture* worldTexture();
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
    void strikePlayer(Actor& enemy, int slot = 0);
    // `bySpell` counts the kill as a magic one for the class offer.
    // byPartner: the companion's bite, which the class kill counts skip.
    void absorbFrom(const Actor& enemy, int damage);
    void hurtEnemy(Actor& enemy, int damage, bool bySpell = false, bool byPartner = false);
    // A blow or spell that got through, before the shields (0x443cb0).
    // `flinch`: whether it plays the hero's hit pose (traps do not, see
    // docs/traps.md).
    void hurtPlayer(int damage, bool magic = false, Actor* attacker = nullptr, bool flinch = true);
    std::optional<std::size_t> enemyAt(float screenX, float screenY) const;
    void renderHealthBars();
    void renderMap();
    void renderChips(int camX, int camY);
    void drawObject(const MapObject& o, bool shadows, int camX, int camY, int shadowAlpha);
    // Where drawObject puts the object's picture, in map pixels; empty when
    // it draws none (a shadow, or a piece with no picture or skinned away).
    SDL_Rect objectPicture(const MapObject& o);
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
    // Wearing more than their strength can carry slows the hero's walk,
    // swing and chant to the bottom step (0x445630, 0x450c60, 0x450d40).
    bool overloaded(const HeroStats& stats) const { return inventory_.wornWeight() > stats[HeroStats::kStrength]; }
    int playerAttack() const { return playerStats()[HeroStats::kAttack]; }
    int playerDefense() const { return playerStats()[HeroStats::kDefense]; }
    int playerHit() const { return playerStats()[HeroStats::kHit]; }
    int playerEvasion() const { return playerStats()[HeroStats::kEvasion]; }
    int playerMaxHp() const { return playerStats()[HeroStats::kHp]; }
    int playerMaxMp() const { return playerStats()[HeroStats::kMp]; }
    void loadLevelCurve();
    void gainExperience(int amount);
    void levelUp();  // one level up, with its sounds, spells and message
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
    // Right click equip and unequip (Rules::rightClickEquips).
    void equipFromBag(int index);
    void unequipToBag(EquipSlot slot);
    void dropHand();
    std::optional<std::size_t> groundItemAt(float screenX, float screenY) const;
    const SpriteBank* itemBank(const std::string& stem);
    void drawItemMarks(const Item& item, const SDL_Rect& area);
    void drawItemPicture(const ItemDef& def, const SDL_Rect& area);
    bool drawPanelPicture();
    void renderGroundItems(int camX, int camY);
    void renderInventory();
    bool drawSpecialPicture();
    void renderItemTotals();
    void renderSpecialButton(int mx, int my);
    void putInWarehouse();  // the held item, into the Warehouse Transfer Box
    // The I and II tabs under the weapon and shield slots (0x406aa0); true
    // when the click was on one.
    bool weaponSetTabClick(int x, int y, bool right);
    void renderWeaponSetTabs();
    // Caps Lock: the sets change once the hero is not in the middle of an
    // attack or a spell (0x4429b0, 0x48d7d0).
    void swapWeaponSetsSoon() {
        swapWeaponSetsPending_ = true;
        swapWeaponSetsIfFree();
    }
    void swapWeaponSetsIfFree();
    // The item window's mine picture (0x446320 case 11): a click with an
    // empty hand takes a mine into the hand, one with a mine puts it back.
    void mineSlotClick();
    // The gold split box: a right click on a stack of gold in the bag or a
    // warehouse asks how much of it to take into the hand (0x44a6f0,
    // 0x44a9f0, 0x44ab60; settled at 0x4429b0).
    struct GoldSplit {
        enum class From { Bag, Storage, GiantStorage } from = From::Bag;
        int x = 0, y = 0;  // the cell of the stack
        int boxX = 0, boxY = 0;
        std::string text;
        bool fresh = true;  // the amount shown is replaced by the first digit typed
    };
    void openGoldSplit(GoldSplit::From from, int cellX, int cellY, int mouseX, int mouseY);
    bool goldSplitEvent(const SDL_Event& event);
    void finishGoldSplit();
    void renderGoldSplit();
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
        bool homesOnPlayer = false;  // turns towards the hero and ends on reaching them
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
        bool pierces = false;  // a missile that flies on through what it hits (table 21)
        // A monster's spell (the index of its caster in actors_), which
        // hurts the hero and an ACTIVE companion instead of monsters;
        // -1 for the hero's own. `targetOnly`: the hero alone, and walls
        // do not stop it (Dread Deathscythe's blades).
        int caster = -1;
        bool targetOnly = false;
        bool struckHero = false, struckPartner = false;
        // A picture of the OPTION folder's Pattern bank instead of its
        // animation (the mine on the ground), -1 = the animation.
        int pattern = -1;
        int light = 1000;   // how bright a Pattern picture is drawn, 0-1000
        double height = 0;  // above the ground, as the original's objects (+0x124)
        int facing = -1;    // a fixed direction 0-8 to draw it in; -1: see `directional`
        std::int32_t follows = -1;  // a character it stays on (CREATEEFFECTCHAR), 0-3 the players
        // A trap's shot (CREATEOPTIONANGLE), which hurts the hero and the
        // companion instead of monsters (traps.cpp), or a monster's stone
        // or arrow (throwShot).
        struct Trap {
            TrapShot shot;
            int kind = 0;
            int power = 0;
            bool fixedDamage = false;
            ScriptHost::TrapFire::Stun stun;
            // A monster's shot: its thrower's index in actors_ (-1 for a
            // trap's), and the hit and element of the attack that threw
            // it, taken as it leaves the hand.
            int thrower = -1;
            int hit = 0, element = -1;
        };
        std::optional<Trap> trap;
        // One of the hero's bowgun shots, or an arrow of the hunter's
        // volley falling on its target (archery.cpp).
        struct Shot {
            int power = 0, hit = 0;  // the attack and hit rate it carries
            int hitOption = -1;      // the spark on what it strikes
            int box = 30;            // half the side of the square it strikes in
            bool pierces = false;    // flies on, striking each monster once
            // The volley's falling arrow: the actor it falls on and where
            // it last saw it, the ticks still to wait before it shows, and
            // the tick of its fall.
            int target = -1;
            double targetX = 0, targetY = 0;
            int wait = 0, fall = -1;
        };
        std::optional<Shot> shot;
        // Layers of its animation that are not drawn (an arrow's flame and
        // frost, which the original turns off unless the bowgun is a fire or
        // ice one; 0x42a300).
        unsigned hiddenLayers = 0;
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
        // A monster's cast (castMonsterSpell): its index in actors_, -1
        // for the hero's; the effect on whoever it hurts, and whether it
        // goes for the hero alone.
        int caster = -1;
        int hitOption = -1;
        bool targetOnly = false;
    };
    // A monster's spell leaving its hand (0x45b043).
    void castMonsterSpell(Actor& enemy, int slot);
    void monsterSpellHitsPlayer(const Effect& e);
    void monsterSpellHitsPartner(const Effect& e);
    // At a monster (an index in actors_), the spell follows it until it
    // leaves the hand (Rules::spellsAimAtEnemy).
    void startCast(double worldX, double worldY, std::optional<std::size_t> enemy = std::nullopt);
    void releaseSpell();
    void tickSpells();
    void regenerateHero();
    // Whether the spell beat the monster's magic evasion.
    bool spellHits(Actor& enemy, int spell, int level, int power, int hit, int hitOption);
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
    // The outline of a pattern drawn with its anchor at (x, y) on the view,
    // for what is under the pointer (Rules::outlineUnderPointer).
    void drawOutline(const SpriteBank& bank, int pattern, int x, int y, SDL_Color color);
    // Where drawSceneObject puts its picture, in map pixels, as objectPicture.
    SDL_Rect sceneObjectPicture(const SceneObject& o);
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

    // Bowguns (archery.cpp, docs/archery.md).
    bool bowInHand() const;
    // A pull of the bowgun's trigger at a monster (an actors_ index) or,
    // with none, along `angle` (radians, world y down).
    void shoot(std::optional<std::size_t> target, double angle);
    void loose();  // the shot leaves the bow, at the animation's shooting frame
    void stepShot(Effect& e);
    bool shotStrikes(Effect& e, Actor& enemy);
    // Moves a shot along its angle, spent where its box meets a wall or an
    // object in the way (0x45e1e0 by way of 0x4145b0 and 0x414990).
    void moveShot(Effect& e, const JudgeRect& box, bool walls);
    // The frame of a hero chart that shoots or strikes (cell status 0x40),
    // or `fallback` when the chart has none.
    int strikeFrame(int chart, int fallback) const;

    // Traps, effects, rain and story pictures that scenario scripts start
    // (traps.cpp, docs/traps.md).
    void fireTrap(const ScriptHost::TrapFire& fire);
    void stepTrap(Effect& e);
    void trapHitsPlayer(const Effect& e);
    void trapHitsPartner(const Effect& e);
    // A monster's attack that throws or shoots (words 44-48): the stone or
    // arrow leaves its hand (0x45a2f0), and lands on the hero or the
    // companion as its blow would.
    void throwShot(Actor& enemy, int slot);
    void shotHitsPlayer(const Effect& e);
    void shotHitsPartner(const Effect& e);
    void iceOnHero(int ticks);
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
    // The stun a blow or spell gives the hero: held in the hit pose, maybe
    // pushed back (0x443cb0).
    void stunHero(const Stun& stun);
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
    // The sounds a monster's animation called for this tick, and its death
    // cry (0x45a2a0, 0x45bec0).
    void monsterCries(Actor& enemy);
    void monsterDeathCry(const Actor& enemy);
    void heroSteps(bool walking, bool running);
    double stepTicks_ = -1;  // original ticks since the hero set off, -1 standing
    bool stepRunning_ = false;
    double swingTicks_ = 0;  // original ticks into the hero's swing
    bool shouted_ = false;
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
        double idleTicks = 0;   // original ticks standing by the hero (+0x22c)
        int restDirection = 0;  // its facing before a jump
        std::optional<Stun> stun;  // a trap's stun holds it in its hit pose
    };
    void spawnPartner();
    // The partner's layers in its model: all but the dash streak, unless
    // `before` shows it (partner.cpp).
    static std::vector<int> partnerLayers(const CharacterModel& m, const std::vector<int>& before = {});
    void changePartner(int kind);
    void tickPartnerIdle();
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
    // The steam blowing from the title picture's pipes (logic/title_smoke.h).
    void tickTitleSmoke();
    void renderTitleSmoke();
    // Smoke00-09 from the original's files [0] and from our pack [1].
    std::array<std::array<std::optional<CharacterModel>, TitleSmoke::kJets>, 2> titleSmokeModels_;
    std::array<bool, 2> titleSmokeLoaded_{};
    const std::array<std::optional<CharacterModel>, TitleSmoke::kJets>& titleSmokeJets();
    std::optional<TitleSmoke> titleSmoke_;
    Random titleRandom_;  // its own, so the menu leaves the game's rolls alone
    double titleSmokeCarry_ = 0;
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
    void newCharacterPattern(int pattern, int alpha = 255, int dx = 0, int dy = 0, int brightness = 255);

    // The Load Data screen (load_data.cpp).
    static constexpr int kSaveSlots = 6;
    struct LoadData {
        enum class Step { Choose, Delete, Mode, Starting, Leaving, Exiting };
        struct Slot {
            int number = 0;  // 0-5, the file save<number + 1>.txt
            std::string file;
            bool damaged = false;
            bool otherData = false;  // made with the other data, in an area this one does not have
            int level = 1, hp = 0, mp = 0;
            std::int64_t experience = 0;
            HeroSex sex = HeroSex::Male;
            HeroClass job = HeroClass::Mercenary;
            std::string name;
            std::shared_ptr<SDL_Texture> picture;  // save<n>.bmp, if there is one
        };
        Step step = Step::Choose;
        std::vector<Slot> slots;  // the saves there are, in file order, packed as the exe lists them
        int chosen = 0;           // into slots
        int appear = 0;           // ticks since the boxes began to show, one after another
        int ticks = 0;            // fading in, or out (Starting, Leaving, Exiting)
        int hover = 0;            // Delete: 1 yes, 2 no; Mode: 1 single, 2 back
        int clickTicks = 0;       // a second click this soon, in the same place, is a double click
        int clickX = -1, clickY = -1;
    };
    void readSaveSlots();
    void tickLoadData();
    void loadDataEvent(const SDL_Event& event);
    void loadDataKey(SDL_Keycode key);
    void loadDataClick(int x, int y);
    void loadDataHover(int x, int y);
    void renderLoadData();
    void renderSaveSlot(const LoadData::Slot* slot, int index, int x, int y, int alpha, int brightness, int plate);

    // The six save files (saving.cpp).
    std::string slotFile(int number) const;
    std::vector<int> savedSlots() const;  // the slot numbers in use
    std::optional<int> freeSlot() const;
    void adoptOldSave();                  // a save.txt from before slots takes the first free slot
    std::string saveFolder() const;
    bool loadGameFrom(const std::string& path);
    // Whether a save's area is in this game's data: the original and the
    // open content pack share the save folder and some map names.
    bool saveFitsData(const std::string& map, const std::string& scenario) const;
    // Fits a save to this game's data, moving one made with the other data
    // (the original's, or the open pack's) to the same place in this one's.
    // False when its area is not in this data.
    bool fitSave(SaveGame& save) const;
    void saveThumbnail(const std::string& path);

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

    // People's scripted actions (C_ACTIONREQ, talk.cpp).
    void requestAction(const ScriptHost::ActionRequest& r);
    void tickScriptedAction(Actor& a);

    // The card game (card_game.cpp): Black Jack over the whole screen,
    // which STARTCARDGAME opens (0x434297) and 0x40da90 plays and draws.
    // The world waits while it is open; its end runs the event 8 triggers.
    void tickCardGame();
    bool cardGameEvent(const SDL_Event& event);
    void renderCardGame();

    // The Change Color window (change_color.cpp; 0x40f9e0 draws it and
    // takes its clicks): CHANGECOLOR opens it, and the hero dyes the weapon,
    // shield and body armour they wear one of sixteen colours, turning a
    // picture of themselves round to see it. Cancel puts the colours back.
    void closeChangeColor(bool keep);
    bool changeColorEvent(const SDL_Event& event);
    void renderChangeColor();

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
    // Our picture of the map, drawn from it: w x h pixels, empty if it
    // cannot be drawn.
    std::vector<std::uint32_t> drawAreaMapPixels(int& w, int& h);
    // Where our picture of the current map is kept between runs ("" when
    // it is not: the open pack brings its own, in Map/Overview).
    std::string areaMapPictureFile();
    std::optional<std::uint64_t> skinFingerprint_;
    void renderScenery(int camX, int camY, bool ground = true);  // ground and objects, no one on them

    // The Settings Menu and Help window (options.cpp). The menu has two
    // tabs: the original's settings, and our debug options and cheats.
    enum class OptionsTab { Settings, Debug, Count };
    enum class OptionRow {
        ScreenMode, SeeThroughPlayer, SeeThroughShadows, Darkness, SaveImage, ClickRange, ClickSize, ClickPriority,
        EffectVolume, MusicVolume, OriginalMode, Art, MissionList, Map, Help, SaveAndTitle, SaveAndExit, Exit, Back,
        // Debug tab.
        XpRate, Luck, ShowFps
    };
    std::vector<OptionRow> optionRows() const;
    bool optionEnabled(OptionRow row) const;
    static const char* optionLabel(OptionRow row);
    static bool optionIsSlider(OptionRow row);
    // A stepper shows one value and steps it with left and right.
    static bool optionIsStepper(OptionRow row);
    std::string optionStepperText(OptionRow row) const;
    void stepOption(OptionRow row, int by, bool wrap = false);
    void switchOptionsTab(int by);
    SDL_Rect optionsTabRect(int tab);
    std::vector<std::string> optionChoices(OptionRow row, int& chosen) const;
    SDL_Rect optionChoiceRect(int row, int choice);
    void setOption(OptionRow row, int index);
    void chooseOption(OptionRow row);
    // The YES / NO question before SAVE AND RETURN TO TITLE and SAVE AND
    // EXIT (0x4103c0): answered with the mouse, or Enter and Esc.
    void answerSaveQuestion(bool yes);
    void closeOptions();
    void optionsEvent(const SDL_Event& event);
    void renderOptions();
    void renderHelp();
    void renderFps();  // the Debug tab's frame rate, top left
    void saveSettings();
    void applyScreenMode();
    void takeScreenshot();
    // Closes the inventory panel or the special items window, putting a
    // held item back or dropping it.
    void closeInventory();
    // Opens the item window in the special items window's place.
    void openItemWindow() {
        showInventory_ = true;
        specialOpen_ = false;
    }
    // A window covers the right half: the item window or the special one.
    bool rightWindowOpen() const { return showInventory_ || specialOpen_; }
    // The special items window (special_items.cpp, after 0x407170,
    // 0x40f900, 0x408db0, 0x448620 and 0x44ae20).
    void toggleSpecialWindow();
    void openSpecialWindow();
    bool specialWindowClick(int x, int y, bool right);
    // The button in the bottom of the item and special windows that
    // switches between them, lit under the pointer.
    bool specialButtonLit(int x, int y) const;
    void renderSpecialWindow(int mx, int my);

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
    SkinLoader skinLoader_;          // until the skin is first loaded
    std::unique_ptr<Skin> ownedSkin_;
    Skin* skin_ = nullptr;           // the skin being drawn: ownedSkin_ with our art, else null
    SpriteCache sprites_;
    std::vector<std::string> mapNames_;
    std::size_t mapIndex_ = 0;
    std::optional<Map> map_;
    std::vector<std::size_t> drawOrder_;  // objects sorted back to front

    std::map<std::string, std::string> scenarioByMap_;  // lower-case map name -> Scenario/<dir>
    // By path stem (null = failed): [0] the original's, [1] the skin's.
    std::array<std::map<std::string, std::unique_ptr<CharacterModel>>, 2> models_;
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
    // Every scenario entered since the game was started or loaded, as it
    // was left. The original loads each scenario once (0x426000 skips one
    // already loaded, 0x427b50) and keeps it until the game is started
    // again (0x41d3f0): its characters, dead monsters included, its items
    // on the ground and its TEMP and NET flags stay as they were, so
    // nothing comes back by leaving and returning.
    struct ScenarioVisit {
        ScenarioControl control;
        std::vector<Actor> actors;
        std::vector<std::optional<std::array<int, 3>>> objectsFixed;  // each SceneObject's
        std::vector<GroundItem> groundItems;
        ScriptFlags flags;  // its TEMP and NET ones
    };
    std::map<std::string, ScenarioVisit> visited_;  // by Scenario/<dir>
    bool forgetCurrent_ = false;

    std::array<std::array<std::optional<CharacterModel>, 2>, 2> playerModels_;  // [art][HeroSex], as models_
    std::array<std::array<bool, 2>, 2> playerModelsLoaded_{};
    HeroSex heroSex_ = HeroSex::Male;
    std::string heroName_;
    std::optional<NewCharacter> newCharacter_;
    std::optional<LoadData> loadData_;
    std::optional<Character> player_;
    std::optional<std::pair<double, double>> walkTarget_;  // world units
    std::optional<std::size_t> attackTarget_;              // index into actors_
    std::optional<std::size_t> talkTarget_;                // a person the player is walking up to
    std::optional<std::size_t> objectTarget_;              // a map object likewise, in control_.placements
    std::vector<Label> labels_;
    int playerHp_ = 0;
    // The hero's point on the attribute disc (player +0x74), which their
    // own element levels come from; the medicines move it (0x44fd10).
    int discX_ = 0, discY_ = 0;
    int playerMp_ = 0;
    MagicTables magic_ = MagicTables::standard();
    SpellBook spells_;
    std::array<bool, kSpellCount> spellOn_{};  // toggle spells that are on
    PowerMode power_;
    double powerAuraTicks_ = 0;
    bool blowLanded_ = false;  // the hero's current swing has struck
    // The bowgun attack under way: what it was aimed at, and whether it is
    // the hunter's volley in power mode (0x438a90).
    struct BowShot {
        std::optional<std::size_t> target;
        double angle = 0;  // radians, world y down
        bool volley = false;
        std::vector<std::size_t> volleyTargets;
    };
    std::optional<BowShot> bowShot_;  // original ticks into the glow (+0x1558)
    // The Warrior's combo or axe spin under way (combat.cpp, player states
    // 11-18, 0x4364e0-0x437910).
    struct Combo {
        WarriorMove move = WarriorMove::Sword;
        int blow = 1;            // the combo's blow, 1-3 (the spin has one)
        int count = 0;           // original ticks into the blow (+0x30c)
        int frame = 0;           // the frame it has reached (+0x308)
        int lastFrame = -1;      // the frame of the tick before (+0x310)
        bool struck = false;     // the blow has fallen (+0x1610)
        int lungeTick = 0;       // original ticks of lunging (+0x1614 / 10)
        double dx = 0, dy = 1;   // where the hero aims, a unit vector in world units (+0x1268)
        int facing = 0;          // the hero's facing, kept while chart 22 has none
        double carry = 0;        // original ticks still to run
    };
    std::optional<Combo> combo_;
    // A Warrior's attack towards (wx, wy) as their combo or spin; false when
    // the hero is no Warrior or has no sword, great sword or axe in hand.
    bool startCombo(double wx, double wy);
    void tickCombo();
    void comboTick(Combo& c);
    void comboBlow(const Combo& c);
    // Blows that do not stun the hero now: their combo's first two blows
    // and the spin (0x443cb0 zeroes the chance in states 11-14).
    bool heroSteadyInCombo() const;
    // The stun a blow of the hero's gives a monster (0x459690): table 25 by
    // how much of its health the blow took, plus the weapon's or the move's
    // own, plus table 24 for the hero's level in its opposite element.
    void heroBlowStun(Actor& enemy, int damage, int chance, int length, bool uncapped);
    // The Elixir (special item 98000000): a hero brought to 0 health with one in the bag
    // gets up again at full health and mental power (0x443cb0).
    bool reviveWithElixir();
    bool magicOpen_ = false;
    int magicPage_ = 0;        // 0-3, six spells each (0xa93c)
    int dragSpell_ = -1;       // the spell being dragged to the bar (0xa710)
    int moonLevel_ = 0;  // the level Moon was cast at (+0x15e8)
    // Hundredths of a point of health and mental power still to come back
    // (+0x1638, +0x163c), and the original's ticks counted towards it.
    int lifeCarry_ = 0, mentalCarry_ = 0;
    std::uint64_t regenTicks_ = 0;
    int moonGlow_ = 0;   // the effect id of the glow on the companion
    std::optional<int> casting_;               // the spell being cast
    std::pair<double, double> castTarget_;     // where, in world units
    bool castReleased_ = false;
    std::optional<std::size_t> castEnemy_;     // the monster cast at, if any
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
    bool mouseHeld_ = false;  // the left button walks or fights while down
    bool pressed_ = false;    // --hold-button: the left button stays down
    bool showJudge_ = false;
    bool autoFight_ = false;

    ItemTable items_;
    Inventory inventory_;
    std::optional<Item> hand_;  // picked up in the inventory panel
    std::vector<GroundItem> groundItems_;
    std::map<std::string, std::unique_ptr<SpriteBank>> itemBanks_;  // by path stem; null = missing
    bool showInventory_ = false;
    // The special items window (X), in the item window's place on the
    // right (DAT_0048ce68); the two never show together.
    bool specialOpen_ = false;
    bool drawnPanel_ = false;  // the item window has a picture this frame
    // The weight gauge's marks as last drawn (-1 until it is first shown),
    // whether they had caught up with the weight, and the tick they last
    // moved on (0x4074e0).
    int weightGaugeShown_ = -1;
    bool weightGaugeSettled_ = true;
    std::uint64_t weightGaugeTick_ = 0;
    int transferGlow_ = 0;  // the Warehouse Transfer Box's light, 0 to 10
    std::uint64_t transferGlowTick_ = 0;
    // Identify was cast: the next item clicked in the bag is identified
    // (player + 0x1624).
    bool identifying_ = false;
    int playerLevel_ = 1;
    std::string saveFile_ = "save.txt";  // the current hero's; empty until a new hero is first saved
    std::string saveFolder_;              // where the six save slots are, if there are slots
    bool worldOnly_ = false;              // renderMap leaves out the panel and windows
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
    static constexpr int kGiantStorage = -2;  // the Giant Warehouse, in giantStorage_
    std::array<ShopStock, kGiantStoragePages> giantStorage_;
    int giantPage_ = 0;
    std::optional<GoldSplit> goldSplit_;
    bool swapWeaponSetsPending_ = false;
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
    void renderTransportGateName();

    // The pointer, its Click Range square and the name of what it is on
    // (pointer.cpp).
    std::optional<SDL_FPoint> pointerPosition() const;  // logical, none while the mouse is outside
    bool pointerOnWorld(const SDL_FPoint& p) const;
    void followPointerTarget();
    void updateHover();
    const Actor* hoveredActor() const;
    // Whether the pointer is on this one (a Settings::Target and its index).
    bool hovered(int kind, std::size_t index) const { return hoverOn_ && hover_.kind == kind && hover_.index == index; }
    void renderClickSquare();
    void renderHoverName();
    void renderPointer();
    // Ours: the names of the items on the ground while Alt is held, or
    // always once L turns them on (Rules::groundItemNames).
    struct ItemLabel {
        std::size_t item;  // into groundItems_
        LabelBox box;      // logical screen pixels, edge included
        std::string name;
        SDL_Color color;
    };
    bool itemNamesShown() const;
    std::vector<ItemLabel> itemLabels() const;
    void renderItemNames();
    void nameOpenEnemy(Actor& actor);
    std::optional<SDL_FPoint> forcedPointer_;
    int pointerOnKind_ = -1;
    bool pointerHidden_ = false;  // the system's pointer, while ours is drawn
    bool forceItemNames_ = false;  // --item-names
    bool hoverOn_ = false;        // the pointer is on the uncovered world
    ClickTarget hover_;           // what a click there would go for
    std::optional<std::map<int, open::EnemyName>> enemyNames_;

    CardGame cardGame_{[this](int n) { return random_.next(n); }};
    double cardTickCarry_ = 0;  // the card game runs at the original's 30 ticks a second
    std::optional<SDL_Point> cardClick_;  // a left click for its next tick
    // The Change Color window (0xa06c): which item's tab is chosen (0
    // weapon, 1 shield, 2 armour; 0xa9e0), the way the picture faces
    // (0xa9f0) and the colours to put back on Cancel (0xa9e4-0xa9ec).
    struct ChangeColor {
        int tab = -1;
        int direction = 0;
        std::array<int, 3> saved{-1, -1, -1};
    };
    std::optional<ChangeColor> changeColor_;
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
    int darkTextureW_ = 0, darkTextureH_ = 0;
    // Zoomed out, the world is drawn at full size to this, then shrunk
    // onto the screen.
    TexturePtr worldTexture_;
    int worldTextureW_ = 0, worldTextureH_ = 0;
    bool fullSize_ = false;
    int shotWidth_ = 0, shotHeight_ = 0;  // the view's size while the area map's picture is drawn (else 0)

    double cameraX_ = 0, cameraY_ = 0;  // top-left of the view, in map pixels
    std::uint64_t ticks_ = 0;
    bool quit_ = false;
    bool menuOpen_ = false;
    Settings settings_;
    std::string settingsFile_;
    std::string mapPictureFolder_;
    bool optionsOpen_ = false;
    bool optionsInGame_ = false;
    int optionsChoice_ = 0;
    // The row whose YES / NO question shows in place of the menu.
    std::optional<OptionRow> saveQuestion_;
    OptionsTab optionsTab_ = OptionsTab::Settings;
    // Frames drawn in the second counted so far, and the last second's count.
    Uint64 fpsSince_ = 0;
    int fpsFrames_ = 0, fps_ = 0;
    // The volume slider row being dragged with the mouse, or -1.
    int optionsDrag_ = -1;
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

#pragma once

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "game/character.h"
#include "logic/ai_select.h"
#include "logic/inventory.h"
#include "logic/magic.h"
#include "logic/partner.h"
#include "logic/rules.h"
#include "logic/progression.h"
#include "logic/script_runner.h"
#include "logic/settings.h"
#include "logic/shop.h"
#include "formats/table_data.h"
#include "formats/open_content.h"
#include "formats/ai_control.h"
#include "formats/item_data.h"
#include "formats/scenario.h"
#include "formats/scenario_script.h"
#include "game/map.h"
#include "render/sprite_cache.h"

namespace sf {

class FileSystem;
class Skin;

// The original runs at 640x480; we render to that and let SDL scale it.
inline constexpr int kScreenWidth = 640;
inline constexpr int kScreenHeight = 480;

// Simulation runs at a fixed rate, separate from rendering, so game logic
// can later be matched tick-for-tick against the original executable.
inline constexpr int kTicksPerSecond = 20;

// The hero's step in world units per tick. Not yet taken from the original.
inline constexpr double kPlayerWalkSpeed = 70.0;
// Top of the companion's health gauge, as in the original.
inline constexpr int kPartnerGaugeY = 395;

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

    // Shows the status window (toggled with S).
    void setStatusOpen(bool open) { showStatus_ = open; }

    // The title menu: Continue, New Game, Options and Quit.
    enum class MenuItem { Continue, NewGame, Options, Quit };
    void openMenu();
    bool menuOpen() const { return menuOpen_; }

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
    void setSaveFlag(std::int32_t id, std::int32_t value) { flags_[{ScenarioScript::kSave, id}] = value; }

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
    void checkTriggers(bool fire = true);
    const CharacterModel* model(const std::string& stem);
    void placePlayer();
    void updateCamera();
    bool tryMove(Character& who, double dx, double dy);
    void tickPlayer();
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
    void hurtPlayer(int damage, bool magic = false, Actor* attacker = nullptr);
    std::optional<std::size_t> enemyAt(float screenX, float screenY) const;
    void renderHealthBars(int camX, int camY);
    void renderMap();
    void renderChips(int camX, int camY);
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
    void pickUpItems();
    void updateEquipment();
    // The hero's stats with what they wear added, as the original sums them
    // (0x44ea60).
    HeroStats heroStats() const { return sf::heroStats(career_.history); }
    // Everything counted: equipment, element levels and Berserker.
    HeroStats playerStats() const;
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
    void showMessage(std::string text);
    bool inventoryClick(int x, int y, bool right);
    void dropHand();
    std::optional<std::size_t> groundItemAt(float screenX, float screenY) const;
    const SpriteBank* itemBank(const std::string& stem);
    void drawItemPicture(const ItemDef& def, const SDL_Rect& area);
    bool drawPanelPicture();
    void renderGroundItems(int camX, int camY);
    void renderInventory();
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
        int option = 0, chart = 0;
        double x = 0, y = 0;
        int age = 0, life = 0;  // life -1: until it hits something or a wall
        double speed = 0, angle = 0;  // world units a tick; radians, in world space
        double turn = 0;              // homing: how far it may turn a tick, 0 = flies straight
        int animSpeed = 1000;         // animation frames per 1000 ticks
        bool directional = false;     // drawn facing its angle
        bool followsPlayer = false;
        // Hurting: spell < 0 for effects that only show.
        int spell = -1, level = 1, power = 0, hit = 0;
        double box = 0;            // half the side of the square it hurts in
        bool area = false;         // hurts everyone in its box (each once), else the first
        bool diesOnHit = false;
        bool diesOnWall = false;
        int hitOption = -1;        // played on whoever it hurts
        double travelled = 0;
        std::vector<std::size_t> struck;  // actors already hurt
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
    void spellHits(Actor& enemy, int spell, int level, int power, int hit, int hitOption);
    void showSpellHit(int spell, double x, double y);
    const CharacterModel* effectModel(int option);
    int effectLength(int option, int chart = 0);
    Effect& spawnEffect(int option, double x, double y, int chart = 0);
    Effect* findEffect(int id);
    void stepCast(SpellCast& cast);
    void stepEffect(Effect& effect);
    void clearSpells();
    void trainSpell(int spell);
    bool spellOn(int spell) const { return spellOn_[static_cast<std::size_t>(spell)]; }
    void setSpellOn(int spell, bool on) { spellOn_[static_cast<std::size_t>(spell)] = on; }
    int spellPower(int spell, int level) const;
    int spellHit(int spell, int level) const;
    void renderSpells(int camX, int camY);

    // The hero's companion (partner.cpp).
    struct Partner {
        explicit Partner(Character c) : character(std::move(c)) {}
        Character character;
        PartnerStats stats{};
        int hp = 1;
        enum class State { Idle, Walk, Run, Bite, Hit, Dead } state = State::Idle;
        std::optional<std::size_t> target{};  // the monster it fights, in actors_
        int walkDelay = 5;   // ticks before it starts walking after the hero
        int regenCarry = 0;  // thousandths of a point of health
        int deadTicks = 0;   // since it fell, for the fade
        bool struck = false;  // the current bite has landed
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
    std::string partnerStatusText(int kind) const;
    void renderPartnerGauge();
    bool partnerGaugeClick(int x, int y);
    void renderSpellBar();
    void renderMenu();
    void menuEvent(const SDL_Event& event);
    int menuItemAt(int x, int y) const;
    bool menuItemEnabled(MenuItem item) const;
    void chooseMenuItem(MenuItem item);
    void newGame();

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
    std::optional<AiControl> ai_;
    Random random_;
    std::string scenarioDir_;  // Scenario/<dir> of the current map, or empty
    ScenarioControl control_;
    std::optional<ScenarioScript> script_;
    std::vector<TriggerArea> triggers_;
    ScriptFlags flags_;
    std::optional<std::pair<int, int>> pendingScenario_;  // scenario number, entry

    std::optional<CharacterModel> playerModel_;
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
    int berserkerCarry_ = 0;
    std::optional<int> casting_;               // the spell being cast
    std::pair<double, double> castTarget_;     // where, in world units
    bool castReleased_ = false;
    std::vector<SpellCast> casts_;
    std::vector<Effect> effects_;
    std::vector<Effect> pendingEffects_;  // spawned this tick
    std::map<int, std::unique_ptr<CharacterModel>> effectModels_;  // by OPTION id; null = missing
    int nextEffectId_ = 1;
    double originalTicks_ = 0;  // carried between engine ticks
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
    int playerLevel_ = 1;
    std::string saveFile_ = "save.txt";
    std::int64_t playerExperience_ = 0;
    LevelCurve levels_;
    GameTables tables_;  // Table.Tbd; empty with open content
    open::ShopLists openShops_;  // shops.txt, INITTRADE's lists with open content
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
    bool runMode_ = false;  // R or the RUN button: run instead of walk
    bool screenshotPending_ = false;
    std::vector<MenuItem> menuItems_;
    int menuChoice_ = 0;
    std::size_t startMapIndex_ = 0;
};

}  // namespace sf

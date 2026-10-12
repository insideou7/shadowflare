#pragma once

#include <SDL.h>

#include <algorithm>
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
#include "logic/rules.h"
#include "logic/progression.h"
#include "logic/script_runner.h"
#include "logic/shop.h"
#include "formats/table_data.h"
#include "formats/ai_control.h"
#include "formats/item_data.h"
#include "formats/scenario.h"
#include "formats/scenario_script.h"
#include "game/map.h"
#include "render/sprite_cache.h"

namespace sf {

class FileSystem;

// The original runs at 640x480; we render to that and let SDL scale it.
inline constexpr int kScreenWidth = 640;
inline constexpr int kScreenHeight = 480;

// Simulation runs at a fixed rate, separate from rendering, so game logic
// can later be matched tick-for-tick against the original executable.
inline constexpr int kTicksPerSecond = 20;

// The player's health until characters have stats.

// For now the game is a map viewer with a walking player: it loads a level
// from the original data and lets you walk around it.
class Game {
public:
    Game(SDL_Renderer* renderer, const FileSystem* data, const std::string& startMap);

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

    // Puts an item in the bag, on the hero or on the ground next to them
    // (for scripted tests). For gold, `count` is the amount. False when the
    // item is unknown, does not fit or cannot be worn.
    enum class GiveTo { Bag, Wear, Ground };
    bool give(int itemClass, int code, int count, GiveTo to);

    // Shows the inventory panel (toggled with I).
    void setInventoryOpen(bool open) { showInventory_ = open; }

    // Draws the walkability grid over the map (toggled with J).
    void setShowJudge(bool show) { showJudge_ = show; }
    std::uint64_t tickCount() const { return ticks_; }

    // Saves the game to a text file (F10) or loads it back (F9). False, with
    // a message on screen, when that fails.
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

    // Shows the status window (toggled with S).
    void setStatusOpen(bool open) { showStatus_ = open; }

    // The main menu. At the start it offers Continue, New Game and Quit;
    // Esc during play opens it with Resume, Save and Load as well.
    enum class MenuItem { Resume, Continue, NewGame, Save, Load, OriginalMode, Quit };
    void openMenu(bool atStart);
    bool menuOpen() const { return menuOpen_; }

    // Walks the player up to a character and speaks to them (for scripted
    // tests); `answers` close the messages that follow, one each.
    void talkToId(std::int32_t id);
    void setAnswers(std::vector<int> answers) { answers_ = std::move(answers); }
    // Sets a SAVE story flag, for testing scripts part way through the story.
    void setSaveFlag(std::int32_t id, std::int32_t value) { flags_[{ScenarioScript::kSave, id}] = value; }

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
    // A monster's attack (slot 0-2) or spell (slot 0-2, `magic`).
    void strikePlayer(Actor& enemy, int slot = 0, bool magic = false);
    // `bySpell` counts the kill as a magic one for the class offer.
    void hurtEnemy(Actor& enemy, int damage, bool bySpell = false);
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
    struct Missile {
        int spell = 0, level = 1;
        int power = 0, hit = 0;
        double x = 0, y = 0, dx = 0, dy = 0;  // world units, per tick
        double travelled = 0;
    };
    // What is drawn for a spell, with no effect on the game.
    struct SpellEffect {
        enum class Kind { Burst, Heal, Spark } kind = Kind::Burst;
        int element = 0;
        double x = 0, y = 0, radius = 0;
        int ticks = 0, length = 1;
    };
    void startCast(double worldX, double worldY);
    void releaseSpell();
    void tickSpells();
    void spellHits(Actor& enemy, int spell, int level, int power, int hit);
    void trainSpell(int spell);
    bool spellOn(int spell) const { return spellOn_[static_cast<std::size_t>(spell)]; }
    void setSpellOn(int spell, bool on) { spellOn_[static_cast<std::size_t>(spell)] = on; }
    int spellPower(int spell, int level) const;
    int spellHit(int spell, int level) const;
    void renderSpells(int camX, int camY);
    void renderSpellBar();
    void renderMenu();
    void menuEvent(const SDL_Event& event);
    int menuItemAt(int x, int y) const;
    bool menuItemEnabled(MenuItem item) const;
    void chooseMenuItem(MenuItem item);
    void newGame();

    SDL_Renderer* renderer_;
    const FileSystem* data_;
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
    std::vector<Missile> missiles_;
    std::vector<SpellEffect> spellEffects_;
    Rules rules_;
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
    std::vector<MenuItem> menuItems_;
    int menuChoice_ = 0;
    std::size_t startMapIndex_ = 0;
};

}  // namespace sf

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "formats/ai_control.h"
#include "formats/scenario.h"
#include "formats/scenario_script.h"
#include "logic/ai_select.h"
#include "logic/inventory.h"
#include "logic/magic.h"
#include "logic/script_runner.h"
#include "logic/traps.h"
#include "world/character.h"
#include "world/map.h"
#include "world/world_events.h"

namespace sf {

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

// An item lying on the ground (game/items.cpp).
struct GroundItem {
    Item item;
    double x = 0, y = 0;  // world units
    std::optional<Character> sprite;  // when its picture is an animation chart
    bool waitForLeave = false;  // not picked up until the player steps away
};

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
    int followsActor = -1;  // an index into World::actors (a freeze's ice)
    bool followsPartner = false;
    bool homesOnPlayer = false;  // turns towards the hero and ends on reaching them
    int then = -1;          // played where it stands when its life runs out
    // Hurting: spell < 0 for effects that only show.
    int spell = -1, level = 1, power = 0, hit = 0;
    JudgeRect judge;           // where it hurts, around (x, y)
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
    // A monster's spell (the index of its caster in World::actors), which
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
        // A monster's shot: its thrower's index in World::actors (-1 for a
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
    // The "MISS!!" over whoever an attack missed (showMiss): it hops,
    // landing three times, then fades out (controller 20012, 0x42c750).
    struct Hop {
        int speed = 500;  // tenths of a world unit a tick, up
        int landings = 0;
        int fade = 1000;
        double floor = 0;  // the height it lands on
    };
    std::optional<Hop> hop;
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
    // A monster's cast (castMonsterSpell): its index in World::actors, -1
    // for the hero's; the effect on whoever it hurts, and whether it
    // goes for the hero alone.
    int caster = -1;
    int hitOption = -1;
    bool targetOnly = false;
};

// Scenario objects: the chests, doors, levers and gates a scenario
// places (objects.cpp). Each has three states, kept in the NET flags
// 100,000,000, 300,000,000 and 200,000,000 + its id until
// ABSOLUTEOBJECTFLAG fixes them: whether it is shown, whether it can
// be used (clicked), and whether its rectangle stands in the way
// (0x45e080).
struct SceneObject {
    std::size_t placement = 0;  // into World::control.placements
    std::optional<std::array<int, 3>> fixed;  // set by ABSOLUTEOBJECTFLAG
    std::optional<Character> animation;       // for a chart rather than a still picture
    const SpriteBank* body = nullptr;
    const SpriteBank* shadow = nullptr;
    int prop = -1;  // open content: frame of Map/props.sheet
};

// A land mine the hero laid (game/mines.cpp).
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

// One area of the game without a window (docs/online.md, the world split
// step 3): its map, the people and monsters in it, the items on its
// ground, its objects, story flags and scripts, and the spells, shots and
// mines flying about in it. The game owns one and draws what is in it;
// the online server will run them with no window at all. The rules that
// move it on move over from Game a piece at a time.
class World {
public:
    std::optional<Map> map;
    std::string scenarioDir;  // Scenario/<dir> of the current map, or empty
    ScenarioControl control;
    std::vector<Actor> actors;
    std::vector<SceneObject> sceneObjects;
    std::vector<GroundItem> groundItems;
    std::vector<TriggerArea> triggers;
    std::optional<AiControl> ai;
    std::optional<ScenarioScript> script;
    ScriptFlags flags;
    std::set<std::int32_t> skippedCommands;  // script commands not carried out, logged once per scenario
    std::map<std::string, ScenarioVisit> visited;  // by Scenario/<dir>

    // Spells, shots and mines (game/spells.cpp, archery.cpp, traps.cpp,
    // mines.cpp).
    std::vector<SpellCast> casts;
    std::vector<Effect> effects;
    std::vector<Effect> pendingEffects;  // spawned this tick
    int nextEffectId = 1;
    std::vector<Mine> mines;

    // Every roll the rules make, so a run plays the same each time.
    Random random;
    // What the rules tell the player this tick (sounds, messages).
    WorldEvents events;
};

}  // namespace sf

#include <SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/log.h"
#include "formats/binary_reader.h"
#include "formats/open_content.h"
#include "game/game.h"
#include "game/skin.h"
#include "platform/filesystem.h"

namespace {

struct Options {
    std::optional<std::filesystem::path> dataDir;  // default: game/, else the open content in content/
    std::string map;  // default: c02_00, or the content pack's start map
    std::string screenshot;  // saved as BMP when the run ends
    std::optional<std::pair<int, int>> walk;  // scripted walk, screen pixels from the player
    long maxFrames = -1;     // -1 runs until quit; used for smoke tests
    long ticks = -1;         // run exactly this many simulation ticks, ignoring the clock
    bool showJudge = false;  // overlay the walkability grid
    bool autoFight = false;  // player attacks nearby monsters by itself
    int partner = -1;        // a companion kind to bring, ACTIVE
    std::optional<std::pair<int, int>> at;  // start position in map pixels
    struct Gift {
        int itemClass = 0, code = 0, count = 1, wear = 0;
        sf::Game::GiveTo to = sf::Game::GiveTo::Bag;
    };
    std::vector<Gift> gifts;  // items to start with, for testing
    bool inventory = false;   // start with the inventory panel open
    bool status = false;      // start with the status window open
    bool noMenu = false;      // go straight into the game
    bool options = false;     // open the Settings Menu at the start (for screenshots)
    bool help = false;        // open the Help window at the start
    std::string optionsFile;  // default: options.txt in the user's settings folder
    std::string saveFile;     // default: save.txt in the user's settings folder
    bool load = false;        // start from the saved game
    bool saveAtEnd = false;   // save when the run ends (for tests)
    int level = 0;            // start at this level (for tests)
    std::optional<std::filesystem::path> skin;  // open content drawn over the original game
    bool originalArt = false;  // draw the original game with its own art
    int talk = -1;            // speak to this character (in-game id) at the start
    std::vector<int> answers; // replies to the first messages, -1 = none
    std::vector<std::pair<int, int>> saveFlags;  // SAVE[id] = value before the start
    std::vector<std::pair<int, int>> questFlags;  // QUEST[id] = state (1 taken, 2 cleared)
    std::optional<int> missions;  // open the mission list (-1, or a mission's description)
    bool original = false;    // play by the original game's rules, bugs included
    struct Cast {
        int slot = 0, dx = 0, dy = 0;
    };
    std::optional<Cast> cast;  // cast the spell on F1-F8 at a point from the player
    struct Order {
        std::string kind;  // jump, dash or fetch
        int dx = 0, dy = 0;
    };
    std::optional<Order> companion;  // a Tab order for the companion, in world units from the hero
    int spell = -1, spellLevel = 1;  // --spell n[:level] puts that spell on F1
    bool mine = false;        // lay a mine where the player starts
};

Options parseArgs(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--data" && i + 1 < argc) {
            options.dataDir = argv[++i];
        } else if (arg == "--map" && i + 1 < argc) {
            options.map = argv[++i];
        } else if (arg == "--screenshot" && i + 1 < argc) {
            options.screenshot = argv[++i];
        } else if (arg == "--walk" && i + 1 < argc) {
            int dx = 0, dy = 0;
            if (std::sscanf(argv[++i], "%d,%d", &dx, &dy) == 2) options.walk.emplace(dx, dy);
        } else if (arg == "--at" && i + 1 < argc) {
            int x = 0, y = 0;
            if (std::sscanf(argv[++i], "%d,%d", &x, &y) == 2) options.at.emplace(x, y);
        } else if ((arg == "--give" || arg == "--equip" || arg == "--drop") && i + 1 < argc) {
            // class:code[:count[:wear]], e.g. --give 0:0 (Short Sword), --give 4:0:500
            // (gold), --equip 0:0:1:150 (a Short Sword worn down by 150 points)
            Options::Gift gift;
            if (arg == "--equip") gift.to = sf::Game::GiveTo::Wear;
            if (arg == "--drop") gift.to = sf::Game::GiveTo::Ground;
            if (std::sscanf(argv[++i], "%d:%d:%d:%d", &gift.itemClass, &gift.code, &gift.count, &gift.wear) >= 2) {
                options.gifts.push_back(gift);
            } else {
                sf::log::warn("{} expects class:code[:count]", arg);
            }
        } else if (arg == "--inventory") {
            options.inventory = true;
        } else if (arg == "--status") {
            options.status = true;
        } else if (arg == "--save-file" && i + 1 < argc) {
            options.saveFile = argv[++i];
        } else if (arg == "--level" && i + 1 < argc) {
            options.level = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        } else if (arg == "--skin" && i + 1 < argc) {
            options.skin = argv[++i];
        } else if (arg == "--original-art") {
            options.originalArt = true;
        } else if (arg == "--talk" && i + 1 < argc) {
            options.talk = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        } else if (arg == "--answers" && i + 1 < argc) {
            for (char* p = argv[++i]; *p;) {
                options.answers.push_back(static_cast<int>(std::strtol(p, &p, 10)));
                if (*p == ',') ++p;
                else break;
            }
        } else if (arg == "--save-flag" && i + 1 < argc) {
            int id = 0, value = 0;
            if (std::sscanf(argv[++i], "%d=%d", &id, &value) == 2) options.saveFlags.emplace_back(id, value);
        } else if (arg == "--quest-flag" && i + 1 < argc) {
            int id = 0, value = 0;
            if (std::sscanf(argv[++i], "%d=%d", &id, &value) == 2) options.questFlags.emplace_back(id, value);
        } else if (arg == "--missions") {
            // Opens the mission list; --missions=<n> with mission n's description.
            options.missions = -1;
        } else if (arg.starts_with("--missions=")) {
            options.missions = static_cast<int>(std::strtol(argv[i] + 11, nullptr, 10));
        } else if (arg == "--original") {
            options.original = true;
        } else if (arg == "--cast" && i + 1 < argc) {
            Options::Cast cast;
            if (std::sscanf(argv[++i], "%d:%d,%d", &cast.slot, &cast.dx, &cast.dy) == 3) {
                options.cast = cast;
            } else {
                sf::log::warn("--cast expects slot:x,y");
            }
        } else if (arg == "--spell" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d:%d", &options.spell, &options.spellLevel) < 1) sf::log::warn("--spell expects n[:level]");
        } else if (arg == "--companion" && i + 1 < argc) {
            // jump:dx,dy, dash:dx,dy, explode:dx,dy or fetch (the nearest item).
            const std::string order = argv[++i];
            Options::Order o;
            o.kind = order.substr(0, order.find(':'));
            if (order.find(':') != std::string::npos) std::sscanf(order.c_str() + order.find(':') + 1, "%d,%d", &o.dx, &o.dy);
            options.companion = o;
        } else if (arg == "--mine") {
            options.mine = true;
        } else if (arg == "--options") {
            options.options = true;
        } else if (arg == "--options-file" && i + 1 < argc) {
            options.optionsFile = argv[++i];
        } else if (arg == "--help-window") {
            options.help = true;
        } else if (arg == "--no-menu") {
            options.noMenu = true;
        } else if (arg == "--load") {
            options.load = true;
        } else if (arg == "--save-at-end") {
            options.saveAtEnd = true;
        } else if (arg == "--partner" && i + 1 < argc) {
            options.partner = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        } else if (arg == "--auto-fight") {
            options.autoFight = true;
        } else if (arg == "--show-judge") {
            options.showJudge = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            options.maxFrames = std::strtol(argv[++i], nullptr, 10);
        } else if (arg == "--ticks" && i + 1 < argc) {
            options.ticks = std::strtol(argv[++i], nullptr, 10);
        } else {
            sf::log::warn("Unknown argument: {}", arg);
        }
    }
    return options;
}

// The data folder may be the install folder itself or a folder that
// contains it (e.g. game/Shadowflare/ShadowFlare). Look for Map/ a few levels
// down. An open content pack is recognised by its content.txt.
std::filesystem::path findInstallRoot(const std::filesystem::path& root) {
    sf::DirectoryFileSystem probe(root);
    if ((probe.exists("Map") && probe.exists("System")) || probe.exists("content.txt")) return root;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (!entry.is_directory(ec)) continue;
        auto found = findInstallRoot(entry.path());
        if (!found.empty()) return found;
    }
    return {};
}

void saveScreenshot(SDL_Renderer* renderer, const std::string& path) {
    // Reads the whole window, which is the 640x480 view scaled up.
    int w = 0, h = 0;
    if (SDL_GetRendererOutputSize(renderer, &w, &h) != 0) return;
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return;
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch) == 0 &&
        SDL_SaveBMP(surface, path.c_str()) == 0) {
        sf::log::info("Saved screenshot to {}", path);
    } else {
        sf::log::warn("Screenshot failed: {}", SDL_GetError());
    }
    SDL_FreeSurface(surface);
}

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    std::unique_ptr<sf::FileSystem> data;
    std::unique_ptr<sf::Game> game;
    Uint64 lastCounter = 0;
    double accumulator = 0.0;
    long frames = 0;
    long maxFrames = -1;
    long ticks = -1;  // fixed-step mode: one tick per frame, then quit
    bool running = true;
};

// One iteration of the main loop. Kept as a separate function because
// browsers cannot block in a loop; Emscripten calls this once per frame.
void frame(void* userData) {
    auto& app = *static_cast<App*>(userData);

    SDL_Event event;
    while (SDL_PollEvent(&event)) app.game->handleEvent(event);

    const Uint64 now = SDL_GetPerformanceCounter();
    const double elapsed = static_cast<double>(now - app.lastCounter) / SDL_GetPerformanceFrequency();
    app.lastCounter = now;

    constexpr double kTickSeconds = 1.0 / sf::kTicksPerSecond;
    if (app.ticks >= 0) {
        // Repeatable runs for tests: the simulation advances one tick per
        // frame whatever the clock says.
        if (static_cast<long>(app.game->tickCount()) < app.ticks) app.game->tick();
        app.game->render(0.0);
    } else {
        app.accumulator += SDL_min(elapsed, 0.25);
        while (app.accumulator >= kTickSeconds) {
            app.game->tick();
            app.accumulator -= kTickSeconds;
        }
        app.game->render(app.accumulator / kTickSeconds);
    }

    ++app.frames;
    const bool ticksDone = app.ticks >= 0 && static_cast<long>(app.game->tickCount()) >= app.ticks;
    if (app.game->wantsQuit() || ticksDone || (app.maxFrames >= 0 && app.frames >= app.maxFrames)) {
        app.running = false;
#ifdef __EMSCRIPTEN__
        emscripten_cancel_main_loop();
#endif
    }
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parseArgs(argc, argv);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        sf::log::error("SDL_Init failed: {}", SDL_GetError());
        return EXIT_FAILURE;
    }

    App app;
    app.maxFrames = options.maxFrames;
    app.ticks = options.ticks;
    app.window = SDL_CreateWindow("ShadowFlare", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  sf::kScreenWidth * 2, sf::kScreenHeight * 2, SDL_WINDOW_RESIZABLE);
    if (!app.window) {
        sf::log::error("SDL_CreateWindow failed: {}", SDL_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }
    app.renderer = SDL_CreateRenderer(app.window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!app.renderer) app.renderer = SDL_CreateRenderer(app.window, -1, SDL_RENDERER_SOFTWARE);
    if (!app.renderer) {
        sf::log::error("SDL_CreateRenderer failed: {}", SDL_GetError());
        SDL_DestroyWindow(app.window);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    SDL_RenderSetLogicalSize(app.renderer, sf::kScreenWidth, sf::kScreenHeight);

    // The player's own copy of the game if there is one, otherwise the
    // open replacement content that ships with the engine.
    std::filesystem::path installRoot;
    std::vector<std::filesystem::path> candidates;
    // Relative to the working folder first, then next to the executable
    // and the folders above it, so it also starts when launched from
    // build/Debug or by double-clicking.
    std::vector<std::filesystem::path> bases = {"."};
    if (char* exe = SDL_GetBasePath()) {
        std::filesystem::path dir = std::filesystem::path(exe).parent_path();
        SDL_free(exe);
        for (int up = 0; up < 4 && !dir.empty(); ++up, dir = dir.parent_path()) {
            bases.push_back(dir);
            if (dir == dir.parent_path()) break;
        }
    }
    if (options.dataDir) {
        candidates.push_back(*options.dataDir);
    } else {
        // The player's game comes before the open content wherever either
        // is found.
        for (const char* name : {"game", "content"}) {
            for (const auto& base : bases) candidates.push_back(base == "." ? name : base / name);
        }
    }
    for (const auto& dir : candidates) {
        if (std::filesystem::is_directory(dir)) installRoot = findInstallRoot(dir);
        if (!installRoot.empty()) break;
    }
    if (installRoot.empty()) {
        installRoot = candidates.front();
        sf::log::warn("No game data found under {} (use --data <path>)", installRoot.string());
    } else {
        sf::log::info("Using game data from {}", installRoot.string());
    }
    app.data = std::make_unique<sf::DirectoryFileSystem>(installRoot);
    // A new game of the original begins in scenario 0, Remote Town.
    std::string startMap = options.map.empty() ? "0" : options.map;
    if (auto info = app.data->read("content.txt")) {
        try {
            const auto pack = sf::open::parsePackInfo(*info);
            sf::projection = pack.projection;
            if (options.map.empty() && !pack.startMap.empty()) startMap = pack.startMap;
            sf::log::info("Open content pack \"{}\"", pack.name);
        } catch (const sf::FormatError& e) {
            sf::log::warn("content.txt: {}", e.what());
        }
    }
    // The original game is drawn with the open content's art (content/
    // or --skin) unless --original-art is given.
    std::unique_ptr<sf::Skin> skin;
    if (!app.data->exists("content.txt") && !options.originalArt) {
        std::vector<std::filesystem::path> packs;
        if (options.skin) {
            packs.push_back(*options.skin);
        } else {
            for (const auto& base : bases) packs.push_back(base == "." ? "content" : base / "content");
        }
        for (const auto& pack : packs) {
            if (!std::filesystem::exists(pack / "skin.txt")) continue;
            skin = sf::Skin::load(std::make_unique<sf::DirectoryFileSystem>(pack), app.renderer);
            if (skin) {
                sf::log::info("Drawing the game with the art in {}", pack.string());
                break;
            }
        }
        if (!skin && options.skin) sf::log::warn("No usable skin.txt in {}", options.skin->string());
    }
    app.game = std::make_unique<sf::Game>(app.renderer, app.data.get(), startMap, skin.get());
    app.game->setShowJudge(options.showJudge);
    app.game->setAutoFight(options.autoFight);
    app.game->setInventoryOpen(options.inventory);
    app.game->setStatusOpen(options.status);
    if (!options.saveFile.empty()) {
        app.game->setSaveFile(options.saveFile);
    } else if (char* prefs = SDL_GetPrefPath("ShadowFlare", "open")) {
        app.game->setSaveFile(std::string(prefs) + "save.txt");
        SDL_free(prefs);
    }
    // The main menu, unless this is a scripted run (tests, screenshots) or
    // the command line already says where to start.
    const bool scripted = options.noMenu || options.load || options.ticks >= 0 ||
                          options.at || options.walk || !options.map.empty() || !options.gifts.empty() ||
                          options.level > 0 || options.autoFight || options.partner >= 0 || options.talk >= 0 || options.cast ||
                          options.companion || options.mine;
    // The player's options (Esc in the game); scripted runs keep the
    // defaults so they come out the same on every machine.
    if (!options.optionsFile.empty()) {
        app.game->setSettingsFile(options.optionsFile);
    } else if (!scripted) {
        if (char* prefs = SDL_GetPrefPath("ShadowFlare", "open")) {
            app.game->setSettingsFile(std::string(prefs) + "options.txt");
            SDL_free(prefs);
        }
    }
    if (options.load) app.game->loadGame();
    if (options.level > 0) app.game->setLevel(options.level);
    if (options.original) app.game->setOriginalMode(true);
    if (!scripted) app.game->openMenu();
    if (options.options) app.game->openOptions(true);
    if (options.help) app.game->setHelpOpen(true);
    for (const auto& gift : options.gifts) app.game->give(gift.itemClass, gift.code, gift.count, gift.to, gift.wear);
    if (options.partner >= 0) app.game->bringPartner(options.partner);
    if (options.at) app.game->placePlayerAt(options.at->first, options.at->second);
    if (options.walk) app.game->walkBy(options.walk->first, options.walk->second);
    for (const auto& [id, value] : options.saveFlags) app.game->setSaveFlag(id, value);
    for (const auto& [id, value] : options.questFlags) app.game->setQuestFlag(id, value);
    if (options.missions) app.game->openMissions(*options.missions);
    app.game->setAnswers(options.answers);
    if (options.talk >= 0) app.game->talkToId(options.talk);
    if (options.spell >= 0) app.game->teachSpell(options.spell, options.spellLevel);
    if (options.cast) app.game->castBy(options.cast->slot - 1, options.cast->dx, options.cast->dy);
    if (options.companion) app.game->orderPartnerBy(options.companion->kind, options.companion->dx, options.companion->dy);
    if (options.mine) app.game->layMineHere();
    app.lastCounter = SDL_GetPerformanceCounter();

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(frame, &app, 0, 1);
#else
    while (app.running) frame(&app);
#endif

    if (options.saveAtEnd) app.game->saveGame();
    if (!options.screenshot.empty()) {
        app.game->render(0.0);
        saveScreenshot(app.renderer, options.screenshot);
    }
    sf::log::info("Exited after {} frames, {} ticks", app.frames, app.game->tickCount());
    app.game.reset();
    skin.reset();
    SDL_DestroyRenderer(app.renderer);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return EXIT_SUCCESS;
}

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
    std::optional<std::pair<int, int>> at;  // start position in map pixels
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
    if (options.dataDir) {
        candidates.push_back(*options.dataDir);
    } else {
        candidates = {"game", "content"};
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
    std::string startMap = options.map.empty() ? "c02_00" : options.map;
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
    app.game = std::make_unique<sf::Game>(app.renderer, app.data.get(), startMap);
    app.game->setShowJudge(options.showJudge);
    app.game->setAutoFight(options.autoFight);
    if (options.at) app.game->placePlayerAt(options.at->first, options.at->second);
    if (options.walk) app.game->walkBy(options.walk->first, options.walk->second);
    app.lastCounter = SDL_GetPerformanceCounter();

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(frame, &app, 0, 1);
#else
    while (app.running) frame(&app);
#endif

    if (!options.screenshot.empty()) {
        app.game->render(0.0);
        saveScreenshot(app.renderer, options.screenshot);
    }
    sf::log::info("Exited after {} frames, {} ticks", app.frames, app.game->tickCount());
    app.game.reset();
    SDL_DestroyRenderer(app.renderer);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return EXIT_SUCCESS;
}

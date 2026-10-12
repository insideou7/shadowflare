// The Settings Menu (Esc in the game, Options on the title menu) and the
// Help window (H), laid out after the original's: each row names a setting
// with its choices to the right, the chosen one bright. Rows the engine
// cannot do yet (no sound) are shown dimmed.
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

#include "core/log.h"
#include "game/game.h"
#include "render/tiny_font.h"

namespace sf {

namespace {

constexpr SDL_Rect kOptionsPanel{100, 60, 440, 350};
constexpr int kRowHeight = 18;
constexpr int kFirstRowY = kOptionsPanel.y + 40;
constexpr int kChoicesX = kOptionsPanel.x + 210;
constexpr int kChoiceGap = 12;
constexpr int kSliderWidth = 180;

const SDL_Color kTitle{255, 210, 80, 255};
const SDL_Color kLabel{225, 220, 205, 255};
const SDL_Color kOn{255, 255, 255, 255};
const SDL_Color kOff{105, 100, 95, 255};
const SDL_Color kDimmed{70, 68, 64, 255};

void frame(SDL_Renderer* renderer, const SDL_Rect& r, SDL_Color fill, SDL_Color edge) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &r);
    SDL_SetRenderDrawColor(renderer, edge.r, edge.g, edge.b, edge.a);
    SDL_RenderDrawRect(renderer, &r);
}

constexpr const char* kTargetLabels[Settings::kTargetCount] = {"ENEM", "ITEM", "OBJ.", "PEOP", "COMP"};

}  // namespace

std::vector<Game::OptionRow> Game::optionRows() const {
    using R = OptionRow;
    std::vector<R> rows{R::ScreenMode,   R::SeeThroughPlayer, R::SeeThroughShadows, R::Darkness,
                        R::SaveImage,    R::ClickRange,       R::ClickSize,         R::ClickPriority,
                        R::EffectVolume, R::MusicVolume,      R::OriginalMode};
    if (optionsInGame_) {
        rows.insert(rows.end(), {R::MissionList, R::Map, R::Help, R::SaveAndTitle, R::SaveAndExit});
    } else {
        rows.insert(rows.end(), {R::Help, R::Back});
    }
    return rows;
}

bool Game::optionEnabled(OptionRow row) const {
    switch (row) {
        // Not in the engine yet.
        case OptionRow::SaveImage:
        case OptionRow::EffectVolume:
        case OptionRow::MusicVolume: return false;
        case OptionRow::MissionList:
        case OptionRow::Map: return map_ && player_;
        case OptionRow::SaveAndTitle:
        case OptionRow::SaveAndExit: return map_ && player_;
        case OptionRow::ClickSize: return settings_.clickRange;
        default: return true;
    }
}

const char* Game::optionLabel(OptionRow row) {
    switch (row) {
        case OptionRow::ScreenMode: return "SCREEN MODE AT START";
        case OptionRow::SeeThroughPlayer: return "SEMI-TRANSPARENT PLAYER";
        case OptionRow::SeeThroughShadows: return "SEMI-TRANSPARENT SHADOW";
        case OptionRow::Darkness: return "DISPLAY DARKNESS";
        case OptionRow::SaveImage: return "SAVE IMAGE AT GAME END";
        case OptionRow::ClickRange: return "CLICK RANGE";
        case OptionRow::ClickSize: return "CLICK RANGE SIZE";
        case OptionRow::ClickPriority: return "CLICK PRIORITY";
        case OptionRow::EffectVolume: return "EFF. VOLUME";
        case OptionRow::MusicVolume: return "BGM VOLUME";
        case OptionRow::OriginalMode: return "ORIGINAL MODE";
        case OptionRow::MissionList: return "MISSION LIST";
        case OptionRow::Map: return "MAP";
        case OptionRow::Help: return "HELP";
        case OptionRow::SaveAndTitle: return "SAVE AND RETURN TO TITLE";
        case OptionRow::SaveAndExit: return "SAVE AND EXIT";
        case OptionRow::Back: return "BACK";
    }
    return "";
}

// The words to pick from on a row, and which one is chosen; none for the
// volume sliders and the rows that do something when chosen.
std::vector<std::string> Game::optionChoices(OptionRow row, int& chosen) const {
    auto onOff = [&](bool on) {
        chosen = on ? 0 : 1;
        return std::vector<std::string>{"ON", "OFF"};
    };
    switch (row) {
        case OptionRow::ScreenMode:
            chosen = settings_.fullScreen ? 1 : 0;
            return {"WINDOW", "FULL"};
        case OptionRow::SeeThroughPlayer: return onOff(settings_.seeThroughPlayer);
        case OptionRow::SeeThroughShadows: return onOff(settings_.seeThroughShadows);
        case OptionRow::Darkness: return onOff(settings_.darkness);
        case OptionRow::SaveImage: return onOff(settings_.saveImageAtEnd);
        case OptionRow::ClickRange: return onOff(settings_.clickRange);
        case OptionRow::OriginalMode: return onOff(originalMode());
        case OptionRow::ClickSize:
            chosen = settings_.clickSize;
            return {"MINI", "SMAL", "NORM", "LARG", "MAX."};
        case OptionRow::ClickPriority: {
            // In priority order; the first is the one that wins.
            std::vector<std::string> names;
            for (const int t : settings_.clickPriority) names.emplace_back(kTargetLabels[t]);
            chosen = 0;
            return names;
        }
        default: chosen = -1; return {};
    }
}

bool Game::optionIsSlider(OptionRow row) { return row == OptionRow::EffectVolume || row == OptionRow::MusicVolume; }

void Game::openOptions(bool inGame) {
    optionsOpen_ = true;
    optionsInGame_ = inGame;
    optionsChoice_ = 0;
    mouseHeld_ = false;
    walkTarget_.reset();
    attackTarget_.reset();
}

void Game::closeOptions() { optionsOpen_ = false; }

void Game::setSettingsFile(std::string path) {
    settingsFile_ = std::move(path);
    std::ifstream in(settingsFile_, std::ios::binary);
    if (!in) return;
    std::stringstream text;
    text << in.rdbuf();
    settings_ = Settings::parse(text.str());
    setOriginalMode(settings_.originalMode);
    applyScreenMode();
    log::info("Options from {}", settingsFile_);
}

void Game::saveSettings() {
    if (settingsFile_.empty()) return;
    std::ofstream out(settingsFile_, std::ios::binary | std::ios::trunc);
    out << settings_.write();
    if (!out) log::warn("Could not write {}", settingsFile_);
}

void Game::applyScreenMode() {
    if (SDL_Window* window = SDL_RenderGetWindow(renderer_)) {
        SDL_SetWindowFullscreen(window, settings_.fullScreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    }
}

// Picks choice `index` on a row (or the slider value).
void Game::setOption(OptionRow row, int index) {
    if (!optionEnabled(row)) return;
    switch (row) {
        case OptionRow::ScreenMode:
            settings_.fullScreen = index == 1;
            applyScreenMode();
            break;
        case OptionRow::SeeThroughPlayer: settings_.seeThroughPlayer = index == 0; break;
        case OptionRow::SeeThroughShadows: settings_.seeThroughShadows = index == 0; break;
        case OptionRow::Darkness: settings_.darkness = index == 0; break;
        case OptionRow::SaveImage: settings_.saveImageAtEnd = index == 0; break;
        case OptionRow::ClickRange: settings_.clickRange = index == 0; break;
        case OptionRow::ClickSize: settings_.clickSize = std::clamp(index, 0, Settings::kClickSizes - 1); break;
        case OptionRow::ClickPriority:
            if (index >= 0 && index < Settings::kTargetCount) {
                settings_.putFirst(settings_.clickPriority[static_cast<std::size_t>(index)]);
            }
            break;
        case OptionRow::EffectVolume: settings_.effectVolume = std::clamp(index, 0, Settings::kMaxVolume); break;
        case OptionRow::MusicVolume: settings_.musicVolume = std::clamp(index, 0, Settings::kMaxVolume); break;
        case OptionRow::OriginalMode:
            settings_.originalMode = index == 0;
            setOriginalMode(settings_.originalMode);
            log::info("Original mode {}", originalMode() ? "on" : "off");
            break;
        default: return;
    }
    saveSettings();
}

void Game::chooseOption(OptionRow row) {
    if (!optionEnabled(row)) return;
    switch (row) {
        case OptionRow::Help:
            closeOptions();
            helpOpen_ = true;
            break;
        case OptionRow::MissionList:
            closeOptions();
            if (!missionsOpen_) toggleMissions();
            break;
        case OptionRow::Map:
            closeOptions();
            if (!mapOpen_) toggleAreaMap();
            break;
        case OptionRow::SaveAndTitle:
            if (saveGame()) {
                closeOptions();
                openMenu();
            }
            break;
        case OptionRow::SaveAndExit:
            if (saveGame()) quit_ = true;
            break;
        case OptionRow::Back: closeOptions(); break;
        case OptionRow::ClickPriority:
            // Enter brings the next one to the front.
            setOption(row, 1);
            break;
        default: {
            // Enter steps through a row's choices.
            int chosen = 0;
            const auto choices = optionChoices(row, chosen);
            if (!choices.empty()) setOption(row, (chosen + 1) % static_cast<int>(choices.size()));
            break;
        }
    }
}

SDL_Rect Game::optionChoiceRect(int row, int choice) {
    int chosen = 0;
    const auto choices = optionChoices(optionRows()[static_cast<std::size_t>(row)], chosen);
    int x = kChoicesX;
    for (int i = 0; i < choice; ++i) x += uiTextWidth(choices[static_cast<std::size_t>(i)]) + kChoiceGap;
    const int w = choice < static_cast<int>(choices.size()) ? uiTextWidth(choices[static_cast<std::size_t>(choice)]) : 0;
    return {x - 3, kFirstRowY + row * kRowHeight - 3, w + 6, kRowHeight};
}

void Game::optionsEvent(const SDL_Event& event) {
    const auto rows = optionRows();
    const int count = static_cast<int>(rows.size());
    auto step = [&](int by) {
        for (int i = 0; i < count; ++i) {
            optionsChoice_ = (optionsChoice_ + by + count) % count;
            if (optionEnabled(rows[static_cast<std::size_t>(optionsChoice_)])) break;
        }
    };
    auto rowAt = [&](int y) {
        const int row = (y - kFirstRowY + 3) / kRowHeight;
        return y >= kFirstRowY - 3 && row < count ? row : -1;
    };
    // Mouse x on a slider, as a volume.
    auto sliderValue = [&](int x) { return std::clamp((x - kChoicesX) * Settings::kMaxVolume / kSliderWidth, 0, Settings::kMaxVolume); };

    switch (event.type) {
        case SDL_QUIT: quit_ = true; break;
        case SDL_KEYDOWN: {
            const OptionRow row = rows[static_cast<std::size_t>(optionsChoice_)];
            int chosen = 0;
            const auto choices = optionChoices(row, chosen);
            switch (event.key.keysym.sym) {
                case SDLK_UP: step(-1); break;
                case SDLK_DOWN: step(1); break;
                case SDLK_LEFT:
                case SDLK_RIGHT: {
                    const int by = event.key.keysym.sym == SDLK_LEFT ? -1 : 1;
                    if (row == OptionRow::ClickPriority) {
                        // Left and right turn the order round.
                        setOption(row, by > 0 ? 1 : Settings::kTargetCount - 1);
                    } else if (optionIsSlider(row)) {
                        setOption(row, (row == OptionRow::EffectVolume ? settings_.effectVolume : settings_.musicVolume) + by);
                    } else if (!choices.empty()) {
                        setOption(row, std::clamp(chosen + by, 0, static_cast<int>(choices.size()) - 1));
                    }
                    break;
                }
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE: chooseOption(row); break;
                case SDLK_ESCAPE: closeOptions(); break;
                default: break;
            }
            break;
        }
        case SDL_MOUSEMOTION:
            if (const int i = rowAt(event.motion.y); i >= 0 && optionEnabled(rows[static_cast<std::size_t>(i)])) optionsChoice_ = i;
            break;
        case SDL_MOUSEBUTTONDOWN: {
            if (event.button.button != SDL_BUTTON_LEFT) break;
            const int i = rowAt(event.button.y);
            if (i < 0 || event.button.x < kOptionsPanel.x || event.button.x >= kOptionsPanel.x + kOptionsPanel.w) break;
            const OptionRow row = rows[static_cast<std::size_t>(i)];
            int chosen = 0;
            const auto choices = optionChoices(row, chosen);
            if (optionIsSlider(row)) {
                setOption(row, sliderValue(event.button.x));
            } else if (choices.empty()) {
                chooseOption(row);
            } else {
                for (int c = 0; c < static_cast<int>(choices.size()); ++c) {
                    const SDL_Rect r = optionChoiceRect(i, c);
                    if (event.button.x >= r.x && event.button.x < r.x + r.w) setOption(row, c);
                }
            }
            break;
        }
        default: break;
    }
}

void Game::renderOptions() {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 150);
    SDL_Rect all{0, 0, kScreenWidth, kScreenHeight};
    SDL_RenderFillRect(renderer_, &all);
    frame(renderer_, kOptionsPanel, {14, 12, 18, 235}, {150, 130, 90, 255});

    const char* title = optionsInGame_ ? "SETTINGS MENU" : "OPTIONS";
    uiText(kOptionsPanel.x + (kOptionsPanel.w - uiTextWidth(title)) / 2, kOptionsPanel.y + 12, title, kTitle);

    const auto rows = optionRows();
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const OptionRow row = rows[static_cast<std::size_t>(i)];
        const bool enabled = optionEnabled(row);
        const int y = kFirstRowY + i * kRowHeight;
        int chosen = 0;
        const auto choices = optionChoices(row, chosen);
        const bool action = choices.empty() && !optionIsSlider(row);
        if (i == optionsChoice_) {
            SDL_Rect bar{kOptionsPanel.x + 6, y - 3, kOptionsPanel.w - 12, kRowHeight};
            SDL_SetRenderDrawColor(renderer_, 90, 70, 30, 120);
            SDL_RenderFillRect(renderer_, &bar);
        }
        // The rows that do something are centred, as on the original.
        const char* label = optionLabel(row);
        const int lx = action ? kOptionsPanel.x + (kOptionsPanel.w - uiTextWidth(label)) / 2 : kOptionsPanel.x + 16;
        uiText(lx, y, label, enabled ? kLabel : kDimmed);

        if (optionIsSlider(row)) {
            const int value = row == OptionRow::EffectVolume ? settings_.effectVolume : settings_.musicVolume;
            SDL_SetRenderDrawColor(renderer_, enabled ? 160 : 70, enabled ? 140 : 66, enabled ? 100 : 60, 255);
            SDL_RenderDrawLine(renderer_, kChoicesX, y + 6, kChoicesX + kSliderWidth, y + 6);
            SDL_Rect knob{kChoicesX + value * kSliderWidth / Settings::kMaxVolume - 3, y + 1, 7, 11};
            SDL_RenderFillRect(renderer_, &knob);
        }
        for (int c = 0; c < static_cast<int>(choices.size()); ++c) {
            const SDL_Rect r = optionChoiceRect(i, c);
            const SDL_Color color = !enabled ? kDimmed : c == chosen ? kOn : kOff;
            uiText(r.x + 3, y, choices[static_cast<std::size_t>(c)], color);
        }
    }
    const char* note = optionsInGame_ ? "ESC TO GO BACK TO THE GAME   DIM ROWS ARE NOT IN YET" : "ESC TO GO BACK   DIM ROWS ARE NOT IN YET";
    drawText(renderer_, kOptionsPanel.x + (kOptionsPanel.w - textWidth(note, 1)) / 2, kOptionsPanel.y + kOptionsPanel.h - 14, note,
             kOff, 1);
}

// The Help window: the keys, as the manual's Shortcut Keys page lists
// them. Keys for things the engine does not have yet are dimmed.
void Game::renderHelp() {
    if (!helpOpen_) return;
    struct Key {
        const char* key;
        const char* what;
        bool works;
    };
    static constexpr Key kKeys[] = {
        {"F1-F8", "SPELL ON RIGHT BUTTON", true},
        {"F9", "ATTACK ON RIGHT BUTTON", true},
        {"1-8", "USE BELT POCKET ITEM", true},
        {"B", "LAY A MINE", true},
        {"P", "INCREASED-POWER MODE", true},
        {"CTRL", "RUN WHILE HELD", true},
        {"SHIFT", "STOP; ATTACK IN PLACE", true},
        {"CAPS LOCK", "SWAP EQUIPMENT", false},
        {"TAB+CLICK", "COMPANION", false},
        {"SPACE", "COMPANION ACTIVE/INACTIVE", true},
        {"S", "STATUS WINDOW", true},
        {"I", "ITEMS WINDOW", true},
        {"X", "SPECIAL ITEMS WINDOW", false},
        {"M", "MAGIC WINDOW", true},
        {"Q", "MISSION LIST", true},
        {"N", "MAP; ARROWS SCROLL IT, ENTER CENTERS IT", true},
        {"R", "SWITCH WALK/RUN", true},
        {"H", "HELP", true},
        {"PRINT SCREEN", "SCREEN SHOT", true},
        {"ESC", "SETTINGS MENU, CLOSE WINDOWS", true},
        {"F10 / F11", "QUICK SAVE / LOAD (OURS)", true},
        {"ARROWS", "WALK (OURS)", true},
    };
    const SDL_Rect panel{60, 20, 520, 400};
    frame(renderer_, panel, {14, 12, 18, 235}, {150, 130, 90, 255});
    const char* title = "KEYBOARD HELP";
    uiText(panel.x + (panel.w - uiTextWidth(title)) / 2, panel.y + 10, title, kTitle);
    int y = panel.y + 34;
    for (const auto& k : kKeys) {
        uiText(panel.x + 20, y, k.key, k.works ? kTitle : kDimmed);
        uiText(panel.x + 140, y, k.what, k.works ? kLabel : kDimmed);
        y += 14;
    }
    const char* mouse = "LEFT CLICK: WALK, ATTACK, TALK, PICK UP   RIGHT CLICK: CAST OR ATTACK";
    drawText(renderer_, panel.x + (panel.w - textWidth(mouse, 1)) / 2, panel.y + panel.h - 22, mouse, kLabel, 1);
    const char* close = "H OR ESC TO CLOSE";
    drawText(renderer_, panel.x + (panel.w - textWidth(close, 1)) / 2, panel.y + panel.h - 12, close, kOff, 1);
}

// Print Screen: the screen as a bitmap in a ScreenShot folder next to the
// saved game, as the original keeps them in its ScreenShot folder.
void Game::takeScreenshot() {
    screenshotPending_ = false;
    std::error_code ec;
    const auto dir = std::filesystem::path(saveFile_).parent_path() / "ScreenShot";
    std::filesystem::create_directories(dir, ec);
    std::filesystem::path path;
    for (int n = 0; n < 10000; ++n) {
        char name[32];
        std::snprintf(name, sizeof name, "SF%04d.bmp", n);
        path = dir / name;
        if (!std::filesystem::exists(path, ec)) break;
    }
    int w = 0, h = 0;
    if (SDL_GetRendererOutputSize(renderer_, &w, &h) != 0) return;
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return;
    if (SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch) == 0 &&
        SDL_SaveBMP(surface, path.string().c_str()) == 0) {
        showMessage("Screen shot saved");
        log::info("Saved screen shot to {}", path.string());
    } else {
        log::warn("Screen shot failed: {}", SDL_GetError());
    }
    SDL_FreeSurface(surface);
}

}  // namespace sf

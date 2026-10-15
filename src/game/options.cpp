// The Settings Menu (Esc in the game, Options on the title menu) and the
// Help window (H), laid out after the original's: each row names a setting
// with its choices to the right, the chosen one bright. Rows the engine
// cannot do yet (no sound) are shown dimmed. A second tab, ours, holds
// debug options and cheats; original mode turns the cheats off.
#include <algorithm>
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

constexpr SDL_Rect kOptionsPanel{100, 46, 440, 388};
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

constexpr const char* kTabNames[] = {"SETTINGS", "DEBUG"};
constexpr int kTabGap = 24;

constexpr const char* kTargetLabels[Settings::kTargetCount] = {"ENEM", "ITEM", "OBJ.", "PEOP", "COMP"};

}  // namespace

std::vector<Game::OptionRow> Game::optionRows() const {
    using R = OptionRow;
    // A new debug option or cheat is a row here, with its label, its
    // choices or stepper text, what setOption or stepOption does with it,
    // and a field in Settings. A cheat must do nothing in original mode.
    if (optionsTab_ == OptionsTab::Debug) return {R::XpRate, R::Luck, R::GoldRate, R::UnlockGates, R::UnlockStorage, R::UnlockMagic, R::ShowFps};
    std::vector<R> rows{R::ScreenMode,   R::SeeThroughPlayer, R::SeeThroughShadows, R::Darkness,
                        R::SaveImage,    R::ClickRange,       R::ClickSize,         R::ClickPriority,
                        R::EffectVolume, R::MusicVolume,      R::OriginalMode};
    // Only with the original files and our art to draw them with.
    if (canSwapArt()) rows.push_back(R::Art);
    if (optionsInGame_) {
        rows.insert(rows.end(), {R::MissionList, R::Map, R::Help, R::SaveAndTitle, R::SaveAndExit});
        // Ours: quits without saving. The original's menu has no such row,
        // so original mode leaves it out.
        if (!originalMode()) rows.push_back(R::Exit);
    } else {
        rows.insert(rows.end(), {R::Help, R::Back});
    }
    return rows;
}

bool Game::optionEnabled(OptionRow row) const {
    switch (row) {
        // Not in the engine yet.
        case OptionRow::SaveImage: return false;
        case OptionRow::MissionList:
        case OptionRow::Map: return map_ && player_;
        case OptionRow::SaveAndTitle:
        case OptionRow::SaveAndExit: return map_ && player_;
        case OptionRow::ClickSize: return settings_.clickRange;
        // Cheats are ours, so original mode turns them off.
        case OptionRow::XpRate:
        case OptionRow::Luck:
        case OptionRow::GoldRate: return !originalMode();
        // Only in a game, and dimmed once every gate is found.
        case OptionRow::UnlockGates: {
            if (originalMode() || !map_ || !player_) return false;
            const auto named = std::count_if(warpGates_.list.begin(), warpGates_.list.end(),
                                             [](const WarpGates::Gate& g) { return !g.name.empty(); });
            return static_cast<std::size_t>(named) > warpGatesFound().size();
        }
        // Only in a game, and dimmed once every page is open.
        case OptionRow::UnlockStorage: {
            if (originalMode() || !map_ || !player_) return false;
            for (int page = 0; page < kGiantStoragePages; ++page)
                if (!giantPageOpen(page)) return true;
            return false;
        }
        // Only in a game, and dimmed once the hero knows them all.
        case OptionRow::UnlockMagic:
            return !originalMode() && map_ && player_ && !spells_.unlearned(magic_, career_.history, career_.current).empty();
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
        case OptionRow::Art: return "ART";
        case OptionRow::MissionList: return "MISSION LIST";
        case OptionRow::Map: return "MAP";
        case OptionRow::Help: return "HELP";
        case OptionRow::SaveAndTitle: return "SAVE AND RETURN TO TITLE";
        case OptionRow::SaveAndExit: return "SAVE AND EXIT";
        case OptionRow::Exit: return "EXIT";
        case OptionRow::Back: return "BACK";
        case OptionRow::XpRate: return "EXPERIENCE RATE";
        case OptionRow::Luck: return "LUCK";
        case OptionRow::GoldRate: return "GOLD RATE";
        case OptionRow::UnlockGates: return "UNLOCK ALL GATES";
        case OptionRow::UnlockStorage: return "UNLOCK ALL STORAGE";
        case OptionRow::UnlockMagic: return "UNLOCK ALL MAGIC";
        case OptionRow::ShowFps: return "SHOW FPS";
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
        case OptionRow::ShowFps: return onOff(settings_.showFps);
        case OptionRow::Art:
            chosen = ourArt() ? 1 : 0;
            return {"ORIGINAL", "OURS"};
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

bool Game::optionIsStepper(OptionRow row) {
    return row == OptionRow::XpRate || row == OptionRow::Luck || row == OptionRow::GoldRate;
}

std::string Game::optionStepperText(OptionRow row) const {
    switch (row) {
        case OptionRow::XpRate: {
            const int rate = xpRate();
            std::string text = std::format("{}X", rate / 100);
            if (originalMode()) text += "  (ORIGINAL MODE)";
            return text;
        }
        case OptionRow::Luck: {
            std::string text = std::format("{}X UNIQUE ITEMS", luck() / 100);
            if (originalMode()) text += "  (ORIGINAL MODE)";
            return text;
        }
        case OptionRow::GoldRate: {
            std::string text = std::format("{}X", goldRate() / 100);
            if (originalMode()) text += "  (ORIGINAL MODE)";
            return text;
        }
        default: return {};
    }
}

// Moves a stepper row `by` steps; with `wrap`, past the end goes back to
// the start (Enter), else it stops there (the arrows).
void Game::stepOption(OptionRow row, int by, bool wrap) {
    if (!optionEnabled(row)) return;
    switch (row) {
        case OptionRow::XpRate: {
            const auto& rates = Settings::kXpRates;
            const int count = static_cast<int>(rates.size());
            const int at = static_cast<int>(std::ranges::find(rates, Settings::clampXpRate(settings_.xpRate)) - rates.begin());
            const int next = wrap ? (at + by + count) % count : std::clamp(at + by, 0, count - 1);
            const int rate = rates[static_cast<std::size_t>(next)];
            if (rate == settings_.xpRate) return;
            settings_.xpRate = rate;
            log::info("Experience rate {}%", rate);
            break;
        }
        case OptionRow::Luck: {
            const auto& rates = Settings::kLuckRates;
            const int count = static_cast<int>(rates.size());
            const int at = static_cast<int>(std::ranges::find(rates, Settings::clampLuck(settings_.luck)) - rates.begin());
            const int next = wrap ? (at + by + count) % count : std::clamp(at + by, 0, count - 1);
            const int luck = rates[static_cast<std::size_t>(next)];
            if (luck == settings_.luck) return;
            settings_.luck = luck;
            log::info("Luck {}%", luck);
            break;
        }
        case OptionRow::GoldRate: {
            const auto& rates = Settings::kGoldRates;
            const int count = static_cast<int>(rates.size());
            const int at = static_cast<int>(std::ranges::find(rates, Settings::clampGoldRate(settings_.goldRate)) - rates.begin());
            const int next = wrap ? (at + by + count) % count : std::clamp(at + by, 0, count - 1);
            const int rate = rates[static_cast<std::size_t>(next)];
            if (rate == settings_.goldRate) return;
            settings_.goldRate = rate;
            log::info("Gold rate {}%", rate);
            break;
        }
        default: return;
    }
    playSound(0x37);
    saveSettings();
}

void Game::switchOptionsTab(int by) {
    const int count = static_cast<int>(OptionsTab::Count);
    optionsTab_ = static_cast<OptionsTab>((static_cast<int>(optionsTab_) + by + count) % count);
    optionsChoice_ = 0;
    optionsDrag_ = -1;
    playSound(0x37);
}

// The tabs sit side by side, centred where the menu's title was.
SDL_Rect Game::optionsTabRect(int tab) {
    const char* first = optionsInGame_ ? "SETTINGS MENU" : "OPTIONS";
    auto name = [&](int t) { return t == 0 ? std::string(first) : std::string(kTabNames[t]); };
    int total = 0;
    for (int t = 0; t < static_cast<int>(OptionsTab::Count); ++t) total += uiTextWidth(name(t)) + (t ? kTabGap : 0);
    int x = kOptionsPanel.x + (kOptionsPanel.w - total) / 2;
    for (int t = 0; t < tab; ++t) x += uiTextWidth(name(t)) + kTabGap;
    return {x - 6, kOptionsPanel.y + 8, uiTextWidth(name(tab)) + 12, kRowHeight};
}

void Game::openOptions(bool inGame) {
    optionsOpen_ = true;
    optionsInGame_ = inGame;
    optionsChoice_ = 0;
    optionsTab_ = OptionsTab::Settings;
    optionsDrag_ = -1;
    mouseHeld_ = false;
    walkTarget_.reset();
    attackTarget_.reset();
}

void Game::closeOptions() {
    optionsOpen_ = false;
    saveQuestion_.reset();
}

void Game::setSettingsFile(std::string path) {
    settingsFile_ = std::move(path);
    std::ifstream in(settingsFile_, std::ios::binary);
    if (!in) return;
    std::stringstream text;
    text << in.rdbuf();
    settings_ = Settings::parse(text.str());
    setOriginalMode(settings_.originalMode);
    setOurArt(settings_.ourArt);
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
    // Every choice clicks, even one already set; the sliders are silent (0x4103c0).
    if (!optionIsSlider(row)) playSound(0x37);
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
        case OptionRow::EffectVolume:
        case OptionRow::MusicVolume: {
            // Music follows its slider as it plays (tickMusic); effects take
            // theirs when they start. Dragging only saves when it moves a step.
            int& volume = row == OptionRow::EffectVolume ? settings_.effectVolume : settings_.musicVolume;
            const int to = std::clamp(index, 0, Settings::kMaxVolume);
            if (to == volume) return;
            volume = to;
            break;
        }
        case OptionRow::OriginalMode:
            settings_.originalMode = index == 0;
            setOriginalMode(settings_.originalMode);
            log::info("Original mode {}", originalMode() ? "on" : "off");
            break;
        case OptionRow::ShowFps: settings_.showFps = index == 0; break;
        case OptionRow::Art:
            settings_.ourArt = index == 1;
            setOurArt(settings_.ourArt);
            break;
        default: return;
    }
    saveSettings();
}

void Game::chooseOption(OptionRow row) {
    if (!optionEnabled(row)) return;
    // Going ahead plays 0x38 (0x4103c0). Saving asks YES or NO first, and
    // plays it again on YES.
    switch (row) {
        case OptionRow::Help:
        case OptionRow::MissionList:
        case OptionRow::Map:
        case OptionRow::SaveAndTitle:
        case OptionRow::SaveAndExit:
        case OptionRow::Exit:
        case OptionRow::UnlockGates:
        case OptionRow::UnlockStorage:
        case OptionRow::UnlockMagic: playSound(0x38); break;
        case OptionRow::Back: playSound(0x39); break;
        default: break;
    }
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
        case OptionRow::SaveAndExit: saveQuestion_ = row; break;
        case OptionRow::Exit: quit_ = true; break;
        case OptionRow::Back: closeOptions(); break;
        case OptionRow::UnlockGates: unlockAllWarpGates(); break;
        case OptionRow::UnlockStorage: unlockAllGiantPages(); break;
        case OptionRow::UnlockMagic: unlockAllSpells(); break;
        case OptionRow::ClickPriority:
            // Enter brings the next one to the front.
            setOption(row, 1);
            break;
        default: {
            if (optionIsStepper(row)) {
                stepOption(row, 1, true);
                break;
            }
            // Enter steps through a row's choices.
            int chosen = 0;
            const auto choices = optionChoices(row, chosen);
            if (!choices.empty()) setOption(row, (chosen + 1) % static_cast<int>(choices.size()));
            break;
        }
    }
}

namespace {

// The question and its answers, where the original writes them over its
// menu (0x4103c0): the question centred on x = 320, YES and NO below.
constexpr int kQuestionY = 170;
constexpr SDL_Rect kYes{336, 202, 24, 12};
constexpr SDL_Rect kNo{384, 202, 36, 12};
const SDL_Color kAnswer{224, 224, 224, 255};
const SDL_Color kAnswerLit{64, 64, 224, 255};

bool on(const SDL_Rect& r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

}  // namespace

void Game::answerSaveQuestion(bool yes) {
    if (!saveQuestion_) return;
    const OptionRow row = *saveQuestion_;
    saveQuestion_.reset();
    if (!yes) {
        playSound(0x37);
        return;
    }
    playSound(0x38);
    if (!saveGame()) return;
    if (row == OptionRow::SaveAndExit) {
        quit_ = true;
    } else {
        closeOptions();
        openMenu();
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
    if (saveQuestion_) {
        if (event.type == SDL_QUIT) quit_ = true;
        if (event.type == SDL_KEYDOWN) {
            const auto key = event.key.keysym.sym;
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_y) answerSaveQuestion(true);
            if (key == SDLK_ESCAPE || key == SDLK_n) answerSaveQuestion(false);
        }
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            if (on(kYes, event.button.x, event.button.y)) answerSaveQuestion(true);
            else if (on(kNo, event.button.x, event.button.y)) answerSaveQuestion(false);
        }
        return;
    }
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
    // Mouse x on a slider, as a volume: the nearest step to it.
    auto sliderValue = [&](int x) {
        return std::clamp(((x - kChoicesX) * Settings::kMaxVolume + kSliderWidth / 2) / kSliderWidth, 0, Settings::kMaxVolume);
    };
    auto onSlider = [&](int x) { return x >= kChoicesX - 6 && x <= kChoicesX + kSliderWidth + 6; };

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
                    } else if (optionIsStepper(row)) {
                        stepOption(row, by);
                    } else if (!choices.empty()) {
                        setOption(row, std::clamp(chosen + by, 0, static_cast<int>(choices.size()) - 1));
                    }
                    break;
                }
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE: chooseOption(row); break;
                case SDLK_TAB: switchOptionsTab((event.key.keysym.mod & KMOD_SHIFT) ? -1 : 1); break;
                case SDLK_PAGEUP: switchOptionsTab(-1); break;
                case SDLK_PAGEDOWN: switchOptionsTab(1); break;
                case SDLK_ESCAPE: closeOptions(); break;
                default: break;
            }
            break;
        }
        case SDL_MOUSEMOTION:
            if (optionsDrag_ >= 0 && optionsDrag_ < count && (event.motion.state & SDL_BUTTON_LMASK)) {
                // Dragging a slider keeps hold of it wherever the mouse goes.
                optionsChoice_ = optionsDrag_;
                setOption(rows[static_cast<std::size_t>(optionsDrag_)], sliderValue(event.motion.x));
                break;
            }
            optionsDrag_ = -1;
            if (const int i = rowAt(event.motion.y); i >= 0 && optionEnabled(rows[static_cast<std::size_t>(i)])) optionsChoice_ = i;
            break;
        case SDL_MOUSEBUTTONDOWN: {
            if (event.button.button != SDL_BUTTON_LEFT) break;
            // The panel's MENU button closes the menu again (0x4458c0).
            if (optionsInGame_ && event.button.x >= 589 && event.button.x <= 639 && event.button.y >= 402 &&
                event.button.y <= 413) {
                playSound(0x39);
                closeOptions();
                break;
            }
            for (int t = 0; t < static_cast<int>(OptionsTab::Count); ++t) {
                const SDL_Rect r = optionsTabRect(t);
                const SDL_Point p{event.button.x, event.button.y};
                if (SDL_PointInRect(&p, &r)) {
                    if (t != static_cast<int>(optionsTab_)) switchOptionsTab(t - static_cast<int>(optionsTab_));
                    return;
                }
            }
            const int i = rowAt(event.button.y);
            if (i < 0 || event.button.x < kOptionsPanel.x || event.button.x >= kOptionsPanel.x + kOptionsPanel.w) break;
            const OptionRow row = rows[static_cast<std::size_t>(i)];
            int chosen = 0;
            const auto choices = optionChoices(row, chosen);
            if (optionIsSlider(row)) {
                if (optionEnabled(row) && onSlider(event.button.x)) {
                    optionsChoice_ = i;
                    optionsDrag_ = i;
                    setOption(row, sliderValue(event.button.x));
                }
            } else if (optionIsStepper(row)) {
                // The arrows either side of the value step it.
                const int w = uiTextWidth(optionStepperText(row));
                if (event.button.x >= kChoicesX - 4 && event.button.x < kChoicesX + 10) stepOption(row, -1);
                else if (event.button.x >= kChoicesX + 18 + w && event.button.x < kChoicesX + 32 + w) stepOption(row, 1);
                else if (event.button.x >= kChoicesX + 10) stepOption(row, 1, true);
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
        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) optionsDrag_ = -1;
            break;
        default: break;
    }
}

void Game::renderOptions() {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 150);
    SDL_Rect all{0, 0, kScreenWidth, kScreenHeight};
    SDL_RenderFillRect(renderer_, &all);
    frame(renderer_, kOptionsPanel, {14, 12, 18, 235}, {150, 130, 90, 255});

    for (int t = 0; t < static_cast<int>(OptionsTab::Count); ++t) {
        const SDL_Rect r = optionsTabRect(t);
        const bool active = t == static_cast<int>(optionsTab_);
        if (active) frame(renderer_, r, {60, 45, 20, 200}, {150, 130, 90, 255});
        const char* name = t == 0 ? (optionsInGame_ ? "SETTINGS MENU" : "OPTIONS") : kTabNames[t];
        uiText(r.x + 6, kOptionsPanel.y + 12, name, active ? kTitle : kOff);
    }

    if (saveQuestion_) {
        // The question in place of the rows.
        int mx = 0, my = 0;
        if (const auto at = pointerPosition()) {
            mx = static_cast<int>(at->x);
            my = static_cast<int>(at->y);
        }
        const char* question = *saveQuestion_ == OptionRow::SaveAndExit ? "Is it O.K. to exit the game?" : "Return to the Title Screen?";
        uiText(320 - uiTextWidth(question) / 2, kQuestionY, question, kLabel);
        uiText(kYes.x, kYes.y, "YES", on(kYes, mx, my) ? kAnswerLit : kAnswer);
        uiText(kNo.x, kNo.y, "NO", on(kNo, mx, my) ? kAnswerLit : kAnswer);
        return;
    }
    const auto rows = optionRows();
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const OptionRow row = rows[static_cast<std::size_t>(i)];
        const bool enabled = optionEnabled(row);
        const int y = kFirstRowY + i * kRowHeight;
        int chosen = 0;
        const auto choices = optionChoices(row, chosen);
        const bool action = choices.empty() && !optionIsSlider(row) && !optionIsStepper(row);
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
        if (optionIsStepper(row)) {
            const std::string value = optionStepperText(row);
            const int w = uiTextWidth(value);
            uiText(kChoicesX, y, "<", enabled ? kLabel : kDimmed);
            uiText(kChoicesX + 14, y, value, enabled ? kOn : kDimmed);
            uiText(kChoicesX + 22 + w, y, ">", enabled ? kLabel : kDimmed);
        }
        for (int c = 0; c < static_cast<int>(choices.size()); ++c) {
            const SDL_Rect r = optionChoiceRect(i, c);
            const SDL_Color color = !enabled ? kDimmed : c == chosen ? kOn : kOff;
            uiText(r.x + 3, y, choices[static_cast<std::size_t>(c)], color);
        }
    }
    const char* note = optionsTab_ == OptionsTab::Debug ? "TAB FOR SETTINGS   ORIGINAL MODE TURNS THE CHEATS OFF"
                       : optionsInGame_                 ? "ESC TO GO BACK TO THE GAME   TAB FOR DEBUG   DIM ROWS ARE NOT IN YET"
                                                        : "ESC TO GO BACK   TAB FOR DEBUG   DIM ROWS ARE NOT IN YET";
    drawText(renderer_, kOptionsPanel.x + (kOptionsPanel.w - textWidth(note, 1)) / 2, kOptionsPanel.y + kOptionsPanel.h - 14, note,
             kOff, 1);
}

// The Debug tab's SHOW FPS: frames drawn in the last second, in the top
// left corner over everything but the pointer.
void Game::renderFps() {
    const Uint64 now = SDL_GetTicks64();
    if (!fpsSince_) fpsSince_ = now;
    ++fpsFrames_;
    if (now - fpsSince_ >= 1000) {
        fps_ = static_cast<int>(fpsFrames_ * 1000 / (now - fpsSince_));
        fpsSince_ = now;
        fpsFrames_ = 0;
    }
    if (!settings_.showFps) return;
    // Until the first second is counted there is nothing to show.
    const std::string text = fps_ ? std::format("FPS {}", fps_) : std::string("FPS --");
    const SDL_Rect back{2, 2, textWidth(text, 1) + 6, 12};
    frame(renderer_, back, {0, 0, 0, 170}, {0, 0, 0, 170});
    drawText(renderer_, 5, 4, text, kOn, 1);
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
        {"CAPS LOCK", "SWAP EQUIPMENT", true},
        {"TAB+CLICK", "COMPANION", false},
        {"SPACE", "COMPANION ACTIVE/INACTIVE", true},
        {"S", "STATUS WINDOW", true},
        {"I", "ITEMS WINDOW", true},
        {"X", "SPECIAL ITEMS WINDOW", true},
        {"M", "MAGIC WINDOW", true},
        {"Q", "MISSION LIST", true},
        {"N", "MAP; ARROWS SCROLL IT, ENTER CENTERS IT", true},
        {"R", "SWITCH WALK/RUN", true},
        {"H", "HELP", true},
        {"PRINT SCREEN", "SCREEN SHOT", true},
        {"ESC", "SETTINGS MENU, CLOSE WINDOWS", true},
        {"F10 / F11", "QUICK SAVE / LOAD (OURS)", true},
        {"ARROWS", "WALK (OURS)", true},
        {"ALT / L", "ITEM NAMES: WHILE HELD / ON-OFF (OURS)", true},
    };
    const SDL_Rect panel{60, 20, 520, 400};
    frame(renderer_, panel, {14, 12, 18, 235}, {150, 130, 90, 255});
    const char* title = "KEYBOARD HELP";
    uiText(panel.x + (panel.w - uiTextWidth(title)) / 2, panel.y + 10, title, kTitle);
    int y = panel.y + 34;
    for (const auto& k : kKeys) {
        // Original mode has no item names (Rules::groundItemNames).
        const bool works = k.works && (k.key != std::string_view("ALT / L") || rules_.groundItemNames);
        uiText(panel.x + 20, y, k.key, works ? kTitle : kDimmed);
        uiText(panel.x + 140, y, k.what, works ? kLabel : kDimmed);
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
    const auto dir = std::filesystem::path(saveFolder()) / "ScreenShot";
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

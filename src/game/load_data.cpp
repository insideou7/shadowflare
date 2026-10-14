// The Load Data screen that Continue opens, as ShadowFlare.exe runs it
// (0x422e30, after the title's Load Game at 0x421a00): the six saved games
// in boxes numbered 1 to 6, a picture of where the chosen one was saved,
// and Delete, Back and OK along the bottom. OK, Enter or a double click on
// a box asks for the game mode, as after Create New Character, then loads
// the game. Delete asks first (0x4228b0). Everything is drawn from
// System/Select/Pattern/Select.njp, the Create New Character screen's
// patterns (new_character.cpp), which hold this screen's too; the open
// content pack has the same pattern numbers (tools/art/select_screen.py).
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/log.h"
#include "formats/binary_reader.h"
#include "game/game.h"
#include "logic/save_game.h"
#include "render/tiny_font.h"

namespace sf {
namespace {

// Select.njp's patterns, each dim/lit pair as the exe uses them.
enum Pattern {
    kExit = 0, kExitLit = 1, kBack = 2, kBackLit = 3, kOk = 4, kOkLit = 5, kDelete = 6, kDeleteLit = 13,
    kYes = 14, kYesLit = 15, kNo = 16, kNoLit = 17,
    kModeBack = 20, kModeBackLit = 21, kOnline = 22, kSingle = 24, kSingleLit = 25,
    kTitle = 37, kWindow = 38, kPictureFrame = 39, kBox = 40, kBackground = 41,
    kPlates = 42,  // 42-65: the number plates 1 to 6, four of each from bright (0) to dim (3)
};

// Where a click counts, from the exe's mouse tests (inclusive).
struct Area {
    int l, t, r, b;
    bool contains(int x, int y) const { return x >= l && x <= r && y >= t && y <= b; }
};
constexpr Area kExitArea{576, 8, 632, 19};
constexpr Area kBackArea{393, 451, 465, 462};
constexpr Area kOkArea{567, 451, 603, 462};
constexpr Area kDeleteArea{62, 451, 168, 462};
constexpr Area kYesArea{249, 276, 296, 287};
constexpr Area kNoArea{349, 276, 386, 287};
// The game mode window's, as on the Create New Character screen (0x424d80).
constexpr Area kSingleArea{222, 242, 417, 253};
constexpr Area kModeBackArea{284, 279, 356, 290};

// The boxes: two columns, three rows (0x422e30). A box takes clicks
// inside its 287 x 76 picture; the number plate sits 28 pixels down.
constexpr int kBoxWidth = 287;
constexpr int kBoxHeight = 76;
int boxX(int i) { return (i & 1) * 304 + 32; }
int boxY(int i) { return (i / 2) * 88 + 188; }
bool inBox(int i, int x, int y) {
    return x > boxX(i) && x < boxX(i) + kBoxWidth && y > boxY(i) && y < boxY(i) + kBoxHeight;
}

// The chosen save's picture shows through the frame's window.
constexpr int kPictureX = 224;
constexpr int kPictureY = 60;
constexpr int kPictureWidth = 391;
constexpr int kPictureHeight = 114;

constexpr int kFadeInTicks = 20;   // the screen fading in (+8 = 0x14)
constexpr int kFadeOutTicks = 10;  // and out, to the title or out of the game (+0xc)
constexpr int kStartTicks = 14;    // into the game, as from Create New Character
constexpr int kAppearTicks = 28;   // the boxes show one after another, 2 ticks apart, each over 8
constexpr int kDoubleClick = 10;   // ticks a second click has to land in the same place
// While the pointer is over a box its number plate throbs, a step every
// four ticks.
constexpr int kThrob[8] = {3, 2, 1, 0, 0, 1, 2, 3};

const SDL_Color kShadow{0, 0, 0, 255};

}  // namespace

void Game::openLoadData() {
    loadData_.emplace();
    loadData_->ticks = kFadeInTicks;
    readSaveSlots();
    mouseHeld_ = false;
}

void Game::readSaveSlots() {
    auto& d = *loadData_;
    adoptOldSave();
    d.slots.clear();
    for (const int n : savedSlots()) {
        LoadData::Slot slot;
        slot.number = n;
        slot.file = slotFile(n);
        try {
            std::ifstream in(slot.file, std::ios::binary);
            std::ostringstream text;
            text << in.rdbuf();
            const SaveGame save = SaveGame::parse(text.str());
            slot.level = std::max(1, save.level);
            slot.hp = save.hp;
            slot.mp = save.mp.value_or(0);
            slot.experience = save.experience;
            slot.sex = save.sex;
            slot.job = save.career ? save.career->current : Career::at(slot.level).current;
            slot.name = save.name;
            slot.otherData = !saveFitsData(save.map, save.scenario);
        } catch (const FormatError& e) {
            log::warn("{}: {}", slot.file, e.what());
            slot.damaged = true;
        }
        const auto picture = std::filesystem::path(slot.file).replace_extension(".bmp").string();
        if (SDL_Surface* s = SDL_LoadBMP(picture.c_str())) {
            if (SDL_Texture* t = SDL_CreateTextureFromSurface(renderer_, s)) {
                slot.picture.reset(t, SDL_DestroyTexture);
            }
            SDL_FreeSurface(s);
        }
        d.slots.push_back(std::move(slot));
    }
    d.chosen = std::clamp(d.chosen, 0, std::max(0, static_cast<int>(d.slots.size()) - 1));
}

void Game::tickLoadData() {
    auto& d = *loadData_;
    using S = LoadData::Step;
    if (d.clickTicks > 0) --d.clickTicks;
    if (d.appear < kAppearTicks) ++d.appear;
    switch (d.step) {
        case S::Starting:
            if (++d.ticks >= kStartTicks) {
                const std::string file = d.slots[static_cast<std::size_t>(d.chosen)].file;
                loadData_.reset();
                if (!loadGameFrom(file)) openMenu();
            }
            break;
        case S::Leaving:
            if (++d.ticks >= kFadeOutTicks) {
                loadData_.reset();
                openMenu();
            }
            break;
        case S::Exiting:
            if (++d.ticks >= kFadeOutTicks) quit_ = true;
            break;
        default:
            if (d.ticks > 0) --d.ticks;  // fading in
            break;
    }
}

void Game::loadDataKey(SDL_Keycode key) {
    auto& d = *loadData_;
    using S = LoadData::Step;
    const bool back = key == SDLK_ESCAPE;
    const bool enter = key == SDLK_RETURN || key == SDLK_KP_ENTER;
    const int count = static_cast<int>(d.slots.size());
    switch (d.step) {
        case S::Choose: {
            // Up and down step a row, left and right a box, among the saves
            // there are; a change plays 0x37.
            const int was = d.chosen;
            if (count > 0) {
                if (key == SDLK_UP) d.chosen -= 2;
                if (key == SDLK_DOWN) d.chosen += 2;
                if (key == SDLK_LEFT) d.chosen -= 1;
                if (key == SDLK_RIGHT) d.chosen += 1;
                d.chosen = std::clamp(d.chosen, 0, count - 1);
            }
            if (d.chosen != was) playSound(0x37);
            if (back) {
                playSound(0x37);
                d.step = S::Leaving;
                d.ticks = 0;
            } else if (enter && count > 0 && d.slots[static_cast<std::size_t>(d.chosen)].otherData) {
                // Its box says Other Data; it can only be deleted here.
            } else if (enter && count > 0) {
                playSound(0x37);
                d.step = S::Mode;
                d.hover = 1;
            } else if (key == SDLK_DELETE && count > 0) {
                playSound(0x37);
                d.step = S::Delete;
                d.hover = 2;  // No, until the player picks Yes
            }
            break;
        }
        case S::Delete: {
            // Up or left for Yes, down or right for No, moving with 0x3a.
            const int was = d.hover;
            if (key == SDLK_UP || key == SDLK_LEFT) d.hover = 1;
            if (key == SDLK_DOWN || key == SDLK_RIGHT) d.hover = 2;
            if (d.hover != was) playSound(0x3a);
            if (enter && d.hover == 1) {
                const auto& slot = d.slots[static_cast<std::size_t>(d.chosen)];
                std::error_code ec;
                std::filesystem::remove(slot.file, ec);
                std::filesystem::remove(std::filesystem::path(slot.file).replace_extension(".bmp"), ec);
                log::info("Deleted the save {}", slot.file);
                if (slot.file == saveFile_) saveFile_.clear();
                d.chosen = 0;
                readSaveSlots();
            }
            if (enter || back) {
                playSound(0x37);
                d.step = S::Choose;
            }
            break;
        }
        case S::Mode: {
            const int was = d.hover;
            if (key == SDLK_UP) d.hover = 1;
            if (key == SDLK_DOWN) d.hover = 2;
            if (d.hover != was) playSound(0x3a);
            if (back || enter) playSound(0x38);
            if (back || (enter && d.hover == 2)) {
                d.step = S::Choose;
            } else if (enter) {
                d.step = S::Starting;
                d.ticks = 0;
                log::info("Loading {}", d.slots[static_cast<std::size_t>(d.chosen)].file);
            }
            break;
        }
        case S::Starting:
        case S::Leaving:
        case S::Exiting: break;
    }
}

void Game::loadDataHover(int x, int y) {
    auto& d = *loadData_;
    using S = LoadData::Step;
    if (d.step == S::Delete) {
        if (kYesArea.contains(x, y)) d.hover = 1;
        if (kNoArea.contains(x, y)) d.hover = 2;
    } else if (d.step == S::Mode) {
        // Online play is not part of this game: the row stays dim.
        if (kSingleArea.contains(x, y)) d.hover = 1;
        if (kModeBackArea.contains(x, y)) d.hover = 2;
    }
}

void Game::loadDataClick(int x, int y) {
    auto& d = *loadData_;
    using S = LoadData::Step;
    const bool any = !d.slots.empty();
    switch (d.step) {
        case S::Choose:
            if (kExitArea.contains(x, y)) {
                playSound(0x37);
                d.step = S::Exiting;
                d.ticks = 0;
            } else if (kBackArea.contains(x, y)) {
                loadDataKey(SDLK_ESCAPE);
            } else if (any && kOkArea.contains(x, y)) {
                loadDataKey(SDLK_RETURN);
            } else if (any && kDeleteArea.contains(x, y)) {
                loadDataKey(SDLK_DELETE);
            } else if (d.appear >= kAppearTicks) {
                for (int i = 0; i < static_cast<int>(d.slots.size()); ++i) {
                    if (!inBox(i, x, y)) continue;
                    d.chosen = i;
                    playSound(0x37);
                    // A second click soon after, in the same place, opens it.
                    if (d.clickTicks > 0 && d.clickX == x && d.clickY == y) {
                        d.clickTicks = 0;
                        d.clickX = d.clickY = -1;
                        loadDataKey(SDLK_RETURN);
                    } else {
                        d.clickTicks = kDoubleClick;
                        d.clickX = x;
                        d.clickY = y;
                    }
                }
            }
            break;
        case S::Delete:
            if (kYesArea.contains(x, y)) {
                d.hover = 1;
                loadDataKey(SDLK_RETURN);
            } else if (kNoArea.contains(x, y)) {
                d.hover = 2;
                loadDataKey(SDLK_RETURN);
            }
            break;
        case S::Mode:
            if (kSingleArea.contains(x, y)) {
                d.hover = 1;
                loadDataKey(SDLK_RETURN);
            } else if (kModeBackArea.contains(x, y)) {
                loadDataKey(SDLK_ESCAPE);
            }
            break;
        default: break;
    }
}

void Game::loadDataEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_QUIT: quit_ = true; break;
        case SDL_KEYDOWN: loadDataKey(event.key.keysym.sym); break;
        case SDL_MOUSEMOTION: loadDataHover(event.motion.x, event.motion.y); break;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT) loadDataClick(event.button.x, event.button.y);
            break;
        default: break;
    }
}

// One box: its frame, its number plate and what the save holds, as
// 0x421e10 draws them. The chosen box is at full strength and the others at
// half; labels are gold and values white, each over a black shadow.
void Game::renderSaveSlot(const LoadData::Slot* slot, int index, int x, int y, int alpha, int brightness, int plate) {
    const bool art = itemBank("System/Select/Pattern/Select") != nullptr;
    if (!slot) {
        brightness = 128;
        plate = 3;
    }
    if (art) {
        newCharacterPattern(kBox, alpha, x, y, brightness);
        newCharacterPattern(kPlates + index * 4 + plate, alpha, x, y + 28, brightness);
    } else {
        const Uint8 a = static_cast<Uint8>(alpha);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        const SDL_Rect box{x, y, kBoxWidth, kBoxHeight};
        SDL_SetRenderDrawColor(renderer_, 10, 8, 6, a);
        SDL_RenderFillRect(renderer_, &box);
        const Uint8 b = static_cast<Uint8>(150 * brightness / 255);
        SDL_SetRenderDrawColor(renderer_, b, b * 4 / 5, b / 2, a);
        SDL_RenderDrawRect(renderer_, &box);
        const std::string number = std::to_string(index + 1);
        drawText(renderer_, x + 10, y + 30, number, {b, static_cast<Uint8>(b * 4 / 5), static_cast<Uint8>(b / 2), a}, 2);
    }
    auto scaled = [&](int r, int g, int b) {
        return SDL_Color{static_cast<Uint8>(r * brightness / 255), static_cast<Uint8>(g * brightness / 255),
                         static_cast<Uint8>(b * brightness / 255), static_cast<Uint8>(alpha)};
    };
    const SDL_Color gold = scaled(224, 192, 128), white = scaled(224, 224, 224);
    const SDL_Color shadow{0, 0, 0, static_cast<Uint8>(alpha)};
    auto text = [&](int dx, int dy, const std::string& s, SDL_Color color) {
        uiText(x + dx + 1, y + dy + 1, s, shadow);
        uiText(x + dx, y + dy, s, color);
    };
    if (!slot) {
        text(39, 12, "No Data", white);
        return;
    }
    if (slot->damaged) {
        text(39, 12, "Damaged", white);
        return;
    }
    // Each value is padded on the left to start just past its label, as
    // the exe writes them over the label.
    auto pad = [](std::string_view label, const std::string& value) {
        return std::string(label.size() + 1, ' ') + value;
    };
    if (slot->otherData) {
        // Ours: saved with the other game data, so it cannot be played here.
        text(39, 12, "Other Data", white);
        text(39, 32, "Name.", gold);
        text(39, 32, pad("Name.", slot->name), white);
        return;
    }
    text(39, 12, "Level.", gold);
    text(39, 12, pad("Level.", std::to_string(slot->level)), white);
    text(104, 12, "Job.", gold);
    text(104, 12, pad("Job.", std::string(className(slot->job, slot->sex))), white);
    text(200, 12, "Sex.", gold);
    text(200, 12, pad("Sex.", slot->sex == HeroSex::Female ? "Female" : "Male"), white);
    text(39, 32, "Name.", gold);
    text(39, 32, pad("Name.", slot->name), white);
    text(39, 52, "HP.", gold);
    text(39, 52, pad("HP.", std::to_string(slot->hp)), white);
    text(100, 52, "MP.", gold);
    text(100, 52, pad("MP.", std::to_string(slot->mp)), white);
    text(160, 52, "EXP.", gold);
    text(160, 52, pad("EXP.", std::to_string(slot->experience)), white);
}

void Game::renderLoadData() {
    const auto& d = *loadData_;
    using S = LoadData::Step;
    int mx = 0, my = 0;
    {
        int wx, wy;
        SDL_GetMouseState(&wx, &wy);
        float lx, ly;
        SDL_RenderWindowToLogical(renderer_, wx, wy, &lx, &ly);
        mx = static_cast<int>(lx);
        my = static_cast<int>(ly);
    }
    const bool art = itemBank("System/Select/Pattern/Select") != nullptr;
    const LoadData::Slot* chosen = d.slots.empty() ? nullptr : &d.slots[static_cast<std::size_t>(d.chosen)];
    newCharacterPattern(kBackground);
    newCharacterPattern(kTitle);
    if (chosen && chosen->picture) {
        const SDL_Rect dst{kPictureX, kPictureY, kPictureWidth, kPictureHeight};
        SDL_RenderCopy(renderer_, chosen->picture.get(), nullptr, &dst);
    }
    if (art) {
        newCharacterPattern(kPictureFrame);
    } else {
        SDL_SetRenderDrawColor(renderer_, 150, 130, 90, 255);
        const SDL_Rect frame{kPictureX - 2, kPictureY - 2, kPictureWidth + 4, kPictureHeight + 4};
        SDL_RenderDrawRect(renderer_, &frame);
    }

    // The boxes, showing one after another as the screen opens; the one
    // under the pointer throbs.
    const bool choosing = d.step == S::Choose && d.ticks == 0;
    for (int i = 0; i < kSaveSlots; ++i) {
        if (d.appear < i * 2) break;
        const int alpha = std::min(255, (d.appear - i * 2) * 255 / 8);
        const LoadData::Slot* slot = i < static_cast<int>(d.slots.size()) ? &d.slots[static_cast<std::size_t>(i)] : nullptr;
        const bool isChosen = slot && i == d.chosen;
        int plate = isChosen ? 0 : 3;
        if (choosing && d.appear >= kAppearTicks && inBox(i, mx, my)) plate = kThrob[(ticks_ >> 2) & 7];
        renderSaveSlot(slot, i, boxX(i), boxY(i), alpha, isChosen ? 255 : 128, plate);
    }

    newCharacterPattern(choosing && kExitArea.contains(mx, my) ? kExitLit : kExit);
    newCharacterPattern(choosing && kBackArea.contains(mx, my) ? kBackLit : kBack);
    if (chosen) {
        newCharacterPattern(choosing && kOkArea.contains(mx, my) ? kOkLit : kOk);
        newCharacterPattern(choosing && kDeleteArea.contains(mx, my) ? kDeleteLit : kDelete);
    }

    if (d.step == S::Delete) {
        // Asks first, naming the save, in the game mode's window.
        newCharacterPattern(kWindow);
        const std::string lines[] = {
            "No." + std::to_string(d.chosen + 1) + ", " + (chosen ? chosen->name : std::string()),
            "Do you really want to delete",
            "this saved game?",
        };
        for (int i = 0; i < 3; ++i) {
            const int x = 320 - uiTextWidth(lines[i]) / 2, y = 190 + i * 20;
            uiText(x + 1, y + 1, lines[i], kShadow);
            uiText(x, y, lines[i], {255, 255, 255, 255});
        }
        newCharacterPattern(d.hover == 1 ? kYesLit : kYes);
        newCharacterPattern(d.hover == 2 ? kNoLit : kNo);
    } else if (d.step == S::Mode || d.step == S::Starting) {
        newCharacterPattern(kWindow);
        newCharacterPattern(kOnline, 110);
        newCharacterPattern(d.hover == 1 ? kSingleLit : kSingle);
        newCharacterPattern(d.hover == 2 ? kModeBackLit : kModeBack);
    }

    // Fading in, or out.
    int dark = 0;
    if (d.step == S::Starting) dark = 255 * d.ticks / kStartTicks;
    else if (d.step == S::Leaving || d.step == S::Exiting) dark = 255 * d.ticks / kFadeOutTicks;
    else dark = 255 * d.ticks / kFadeInTicks;
    if (dark > 0) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, static_cast<Uint8>(std::clamp(dark, 0, 255)));
        SDL_Rect all{0, 0, kScreenWidth, kScreenHeight};
        SDL_RenderFillRect(renderer_, &all);
    }
}

}  // namespace sf

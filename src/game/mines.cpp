// Land mines: laying them (B or the MINE button), arming, setting off and
// the blast, as ShadowFlare.exe's mine controller does them (0x42bd40;
// logic/mines.h has the numbers, docs/mines.md the story).
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include "core/log.h"
#include "game/game.h"
#include "render/tiny_font.h"

namespace sf {

namespace {

// The MINE button on the original's panel, as its click test finds it
// (0x445fc5): x 496-511, y 424-439.
constexpr SDL_Rect kMineButton{496, 424, 16, 16};

}  // namespace

int Game::mostMines() const { return mines::most(inventory_.modifier(ItemDef::kModMineCount)); }

// Table 23 at the hero's level when the mine was laid, plus the Mine
// Effect worn when it goes off.
int Game::minePower(int level) const { return mines::power(magic_, level, inventory_.modifier(ItemDef::kModMineEffect)); }

// B and the MINE button (0x442a73, 0x445fc5): with a mine in hand and the
// last one's wait over, a mine goes down where the hero stands.
void Game::layMine() {
    if (!player_ || !map_ || menuOpen_) return;
    if (mineCount_ <= 0) {
        showMessage("No mines");
        return;
    }
    if (mineWait_ > 0) return;
    mineWait_ = mines::kLayWait;
    --mineCount_;
    Mine mine;
    mine.x = player_->x;
    mine.y = player_->y;
    mine.level = playerLevel_;
    // The mine's own object: a picture from the OPTION folder's Pattern
    // bank, not an animation, so it is made here rather than by spawnEffect.
    Effect e;
    e.id = nextEffectId_++;
    e.option = mines::kMineOption;
    e.pattern = 0;
    e.x = mine.x;
    e.y = mine.y;
    e.life = mines::kLife;
    mine.effect = e.id;
    pendingEffects_.push_back(std::move(e));
    mines_.push_back(mine);
    log::info("Laid a mine, {} left", mineCount_);
}

// One of the original's ticks of a mine's controller.
void Game::stepMine(Mine& mine) {
    ++mine.tick;
    if (!mine.blasted) {
        Effect* e = findEffect(mine.effect);
        if (e) {
            // Armed: its picture changes at tick 40 and monsters set it
            // off from its 45th tick on.
            if (mine.tick == mines::kArmedPictureAt) e->pattern = 1;
            if (e->age >= mines::kArmedAt &&
                !monstersIn(mine.x - mines::kTriggerHalf, mine.y - mines::kTriggerHalf, mine.x + mines::kTriggerHalf,
                            mine.y + mines::kTriggerHalf)
                     .empty()) {
                e->dead = true;
                e = nullptr;
            }
        }
        // Stepped on, or its 300 ticks are up.
        if (!e) mineBlast(mine);
        return;
    }
    // Smoke rings, every other tick from 12 to 40, each 50 further out.
    if (mine.tick >= mines::kRingsFrom && mine.tick <= mines::kRingsTo && (mine.tick - mines::kRingsFrom) % 2 == 0) {
        mine.ring += mines::kRingStep;
        for (int option : mines::kSmokeOptions) {
            const double a = random_.next(360) * std::numbers::pi / 180;
            spawnEffect(option, mine.x + std::cos(a) * mine.ring, mine.y + std::sin(a) * mine.ring);
        }
    }
    // Four pieces of the mine thrown up, each with a fire riding on it.
    if (mine.tick == mines::kPiecesAt) {
        mine.lift = mines::kPieceLift;
        for (std::size_t i = 0; i < mine.pieces.size(); ++i) {
            const double a = random_.next(360) * std::numbers::pi / 180;
            const double x = mine.x + std::cos(a) * 50, y = mine.y + std::sin(a) * 50;
            for (int option : {mines::kPieceOptions[i], mines::kPieceFireOption}) {
                Effect& piece = spawnEffect(option, x, y);
                piece.life = mines::kPieceLife;
                piece.speed = mines::kPieceSpeed;
                piece.angle = a;
                (option == mines::kPieceFireOption ? mine.fires : mine.pieces)[i] = piece.id;
            }
        }
    } else if (mine.tick > mines::kPiecesAt) {
        bool landed = false;
        for (std::size_t i = 0; i < mine.pieces.size(); ++i) {
            Effect* piece = findEffect(mine.pieces[i]);
            if (!piece) continue;
            if (Effect* fire = findEffect(mine.fires[i])) fire->height = piece->height;
            piece->height += mine.lift / 10.0;
            if (piece->height < 0) landed = true;
        }
        // They fall together and bounce together, a little.
        mine.lift = landed ? mines::kPieceBounce : mine.lift - mines::kPieceFall;
    }
    if (mine.tick >= mines::kDone) mine.done = true;
}

// The mine goes off: the blast, which hurts every monster within 600 once.
void Game::mineBlast(Mine& mine) {
    mine.blasted = true;
    mine.effect = 0;
    mine.tick = 0;  // the next tick is the blast's first
    spawnEffect(mines::kBlastOption, mine.x, mine.y);
    const auto struck = monstersIn(mine.x - mines::kBlastHalf, mine.y - mines::kBlastHalf, mine.x + mines::kBlastHalf,
                                   mine.y + mines::kBlastHalf);
    const int power = minePower(mine.level);
    std::vector<std::size_t> done;
    for (std::size_t i : struck) {
        if (std::ranges::find(done, i) != done.end()) continue;
        done.push_back(i);
        Actor& enemy = actors_[i];
        const auto& s = enemy.placement.stats;
        spawnEffect(mines::kHitOptions[static_cast<std::size_t>(random_.next(4))], enemy.character.x,
                    enemy.character.y);
        if (random_.next(100) >= hitChance(mines::kBlastHit, s.evasion)) continue;
        const int damage = mines::damage(magic_, power, s.element, s.defense, random_.next(3));
        log::info("A mine hits for {}", damage);
        hurtEnemy(enemy, damage);
    }
}

// Picking up a mine: one more in hand if there is room, else it stays on
// the ground.
bool Game::pickUpMine() {
    if (mineCount_ >= mostMines()) return false;
    ++mineCount_;
    return true;
}

bool Game::mineButtonClick(int x, int y) {
    if (menuOpen_ || !player_ || x < kMineButton.x || x >= kMineButton.x + kMineButton.w || y < kMineButton.y ||
        y >= kMineButton.y + kMineButton.h) {
        return false;
    }
    layMine();
    return true;
}

// The MINE button where the original's panel has it, with the mines in
// hand and the most beside it.
void Game::renderMineButton() {
    if (hasPanelArt()) return;  // the panel has the button, lit by renderPanel
    const SDL_Rect& r = kMineButton;
    SDL_SetRenderDrawColor(renderer_, 20, 18, 26, 200);
    SDL_RenderFillRect(renderer_, &r);
    const bool ready = mineCount_ > 0 && mineWait_ == 0;
    SDL_SetRenderDrawColor(renderer_, ready ? 200 : 110, ready ? 170 : 100, ready ? 110 : 80, 255);
    SDL_RenderDrawRect(renderer_, &r);
    // A small mine: a dark dome on a rim.
    SDL_SetRenderDrawColor(renderer_, 150, 140, 120, 255);
    SDL_RenderDrawLine(renderer_, r.x + 3, r.y + 11, r.x + 12, r.y + 11);
    SDL_SetRenderDrawColor(renderer_, 90, 88, 84, 255);
    const SDL_Rect dome{r.x + 5, r.y + 7, 6, 4};
    SDL_RenderFillRect(renderer_, &dome);
    SDL_SetRenderDrawColor(renderer_, 230, 90, 40, 255);
    SDL_RenderDrawPoint(renderer_, r.x + 8, r.y + 6);
    drawText(renderer_, r.x + r.w + 3, r.y, "MINE", {200, 190, 160, 255}, 1);
    drawText(renderer_, r.x + r.w + 3, r.y + 8, std::format("{}/{}", mineCount_, mostMines()), {255, 255, 255, 255}, 1);
}

}  // namespace sf

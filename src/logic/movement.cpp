#include "logic/movement.h"

namespace sf {

bool tryMove(const GroundLayer& g, double& x, double& y, double dx, double dy) {
    const bool xFree = g.walkable(x + dx, y), yFree = g.walkable(x, y + dy);
    // The diagonal needs both sides free, or it could slip between two
    // blocked cells that only touch at a corner.
    if (xFree && yFree && g.walkable(x + dx, y + dy)) {
        x += dx;
        y += dy;
        return true;
    }
    if (xFree && dx != 0) {
        x += dx;
        return true;
    }
    if (yFree && dy != 0) {
        y += dy;
        return true;
    }
    return false;
}

}  // namespace sf

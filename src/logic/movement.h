#pragma once

#include "formats/map_layers.h"

namespace sf {

// Moves a position by a world-space step unless that would leave walkable
// ground. A blocked diagonal step slides along whichever axis is still
// free, so walking into a wall at an angle follows the wall. Returns
// whether the position changed.
bool tryMove(const GroundLayer& ground, double& x, double& y, double dx, double dy);

}  // namespace sf

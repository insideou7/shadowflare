#pragma once

namespace sf::crash {

// Writes crash.txt next to the program when the game crashes or a check
// fails (a Debug build's "vector subscript out of range", abort(), a bad
// memory access): what went wrong, the call stack and the last lines of
// the log. Call once, early in main().
void install();

}  // namespace sf::crash

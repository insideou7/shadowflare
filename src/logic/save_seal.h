#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace sf {

// Offline saves are written sealed: the text of SaveGame::write encrypted
// and signed (XChaCha20-Poly1305, Monocypher) with a key built into the
// game, so a save can't be read or changed in a text editor, and one that
// has been changed is refused. This stops easy editing, not a determined
// player: the key is in the game, and in the source unless a build sets its
// own (SFE_SAVE_KEY). Builds with SFE_SAVE_TOOLS can still read and write
// plain text saves for development (--plain-saves).
//
// A sealed save: "SFSEAL1\n", a 24-byte nonce, a 16-byte tag, then the
// encrypted text.
inline constexpr std::string_view kSealMagic = "SFSEAL1\n";

std::string sealSave(std::string_view text);
bool isSealedSave(std::string_view bytes);
// The text inside a sealed save, or null when the bytes are not a sealed
// save or have been changed.
std::optional<std::string> unsealSave(std::string_view bytes);

}  // namespace sf

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "formats/binary_reader.h"

namespace sf::rclib {

// Most ShadowFlare data is wrapped in an "RCLIB-L" block:
//   char magic[8] = "RCLIB-L\x1a"; u32 unpackedSize; u32 packedSize; u8 data[packedSize]
// The payload is LZSS with a 4 KiB ring buffer.
inline constexpr std::size_t kHeaderSize = 16;

bool isBlock(std::span<const std::uint8_t> data);

// Decodes a raw LZSS stream until `unpackedSize` bytes are produced.
std::vector<std::uint8_t> lzDecode(std::span<const std::uint8_t> packed, std::size_t unpackedSize);

// Reads one RCLIB block at the reader's position and returns its contents.
std::vector<std::uint8_t> readBlock(BinaryReader& reader);

}  // namespace sf::rclib

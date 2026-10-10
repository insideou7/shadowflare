#include "formats/rclib.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace sf::rclib {

namespace {

constexpr char kMagic[8] = {'R', 'C', 'L', 'I', 'B', '-', 'L', '\x1a'};
constexpr std::size_t kRingSize = 4096;
constexpr std::size_t kRingStart = kRingSize - 18;
constexpr std::size_t kMinMatch = 3;

}  // namespace

bool isBlock(std::span<const std::uint8_t> data) {
    return data.size() >= kHeaderSize && std::memcmp(data.data(), kMagic, sizeof(kMagic)) == 0;
}

std::vector<std::uint8_t> lzDecode(std::span<const std::uint8_t> packed, std::size_t unpackedSize) {
    std::array<std::uint8_t, kRingSize> ring{};
    std::size_t r = kRingStart;
    std::vector<std::uint8_t> out;
    out.reserve(unpackedSize);

    auto emit = [&](std::uint8_t c) {
        out.push_back(c);
        ring[r] = c;
        r = (r + 1) & (kRingSize - 1);
    };

    std::size_t i = 0;
    while (out.size() < unpackedSize && i < packed.size()) {
        // Eight items follow each flag byte, most significant bit first.
        // A set bit is a back reference; a clear bit is a literal byte.
        const std::uint8_t flags = packed[i++];
        for (int bit = 7; bit >= 0 && out.size() < unpackedSize; --bit) {
            if (flags & (1u << bit)) {
                if (i + 1 >= packed.size()) throw FormatError("truncated LZ reference");
                const std::uint8_t lo = packed[i++];
                const std::uint8_t hi = packed[i++];
                const std::size_t pos = lo | (static_cast<std::size_t>(hi & 0xf0) << 4);
                const std::size_t len = (hi & 0x0f) + kMinMatch;
                for (std::size_t k = 0; k < len && out.size() < unpackedSize; ++k) {
                    emit(ring[(pos + k) & (kRingSize - 1)]);
                }
            } else {
                if (i >= packed.size()) break;
                emit(packed[i++]);
            }
        }
    }
    if (out.size() != unpackedSize) throw FormatError("LZ stream ended early");
    return out;
}

std::vector<std::uint8_t> readBlock(BinaryReader& reader) {
    if (reader.chars(sizeof(kMagic)) != std::string_view(kMagic, sizeof(kMagic))) {
        throw FormatError("missing RCLIB-L header");
    }
    const std::uint32_t unpacked = reader.u32();
    const std::uint32_t packed = reader.u32();
    return lzDecode(reader.bytes(packed), unpacked);
}

}  // namespace sf::rclib

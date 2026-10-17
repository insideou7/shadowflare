#include "logic/save_seal.h"

#include <array>
#include <cstdint>
#include <random>

#include "monocypher.h"

#ifndef SF_SAVE_KEY
#define SF_SAVE_KEY "ShadowFlare open source saves, not for editing"
#endif

namespace sf {
namespace {

constexpr std::size_t kNonce = 24, kTag = 16, kHeader = kSealMagic.size() + kNonce + kTag;

const std::uint8_t* bytesOf(std::string_view s) { return reinterpret_cast<const std::uint8_t*>(s.data()); }

// The key: a hash of the build's secret, so any length of secret works.
std::array<std::uint8_t, 32> key() {
    std::array<std::uint8_t, 32> k{};
    constexpr std::string_view secret = SF_SAVE_KEY;
    crypto_blake2b(k.data(), k.size(), bytesOf(secret), secret.size());
    return k;
}

}  // namespace

std::string sealSave(std::string_view text) {
    std::string out(kHeader + text.size(), '\0');
    out.replace(0, kSealMagic.size(), kSealMagic);
    auto* nonce = reinterpret_cast<std::uint8_t*>(out.data() + kSealMagic.size());
    auto* tag = nonce + kNonce;
    std::random_device random;
    for (std::size_t i = 0; i < kNonce; ++i) nonce[i] = static_cast<std::uint8_t>(random());
    auto k = key();
    crypto_aead_lock(tag + kTag, tag, k.data(), nonce, nullptr, 0, bytesOf(text), text.size());
    crypto_wipe(k.data(), k.size());
    return out;
}

bool isSealedSave(std::string_view bytes) { return bytes.starts_with(kSealMagic); }

std::optional<std::string> unsealSave(std::string_view bytes) {
    if (!isSealedSave(bytes) || bytes.size() < kHeader) return std::nullopt;
    const std::uint8_t* nonce = bytesOf(bytes) + kSealMagic.size();
    const std::uint8_t* tag = nonce + kNonce;
    std::string text(bytes.size() - kHeader, '\0');
    auto k = key();
    const int bad = crypto_aead_unlock(reinterpret_cast<std::uint8_t*>(text.data()), tag, k.data(), nonce, nullptr, 0, tag + kTag,
                                       text.size());
    crypto_wipe(k.data(), k.size());
    if (bad) return std::nullopt;
    return text;
}

}  // namespace sf

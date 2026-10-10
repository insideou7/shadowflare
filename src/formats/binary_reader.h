#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sf {

// Thrown when a data file does not match the layout we expect.
class FormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Bounds-checked little-endian reader over a byte buffer.
class BinaryReader {
public:
    explicit BinaryReader(std::span<const std::uint8_t> data) : data_(data) {}

    std::size_t offset() const { return offset_; }
    std::size_t remaining() const { return data_.size() - offset_; }
    bool atEnd() const { return offset_ >= data_.size(); }

    void seek(std::size_t offset) {
        if (offset > data_.size()) throw FormatError("seek past end of file");
        offset_ = offset;
    }
    void skip(std::size_t count) { seek(offset_ + need(count)); }

    std::uint8_t u8() {
        need(1);
        return data_[offset_++];
    }

    std::int16_t i16() { return static_cast<std::int16_t>(read<std::uint16_t>()); }
    std::uint32_t u32() { return read<std::uint32_t>(); }
    std::int32_t i32() { return static_cast<std::int32_t>(read<std::uint32_t>()); }

    std::span<const std::uint8_t> bytes(std::size_t count) {
        need(count);
        auto out = data_.subspan(offset_, count);
        offset_ += count;
        return out;
    }

    std::string_view chars(std::size_t count) {
        auto b = bytes(count);
        return {reinterpret_cast<const char*>(b.data()), b.size()};
    }

private:
    std::size_t need(std::size_t count) const {
        if (count > remaining()) throw FormatError("unexpected end of file");
        return count;
    }

    template <typename T>
    T read() {
        need(sizeof(T));
        T value = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            value |= static_cast<T>(static_cast<T>(data_[offset_ + i]) << (8 * i));
        }
        offset_ += sizeof(T);
        return value;
    }

    std::span<const std::uint8_t> data_;
    std::size_t offset_ = 0;
};

}  // namespace sf

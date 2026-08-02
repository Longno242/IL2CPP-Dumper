#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace static_dump {

class BinaryStream {
public:
    std::vector<uint8_t> data;
    size_t Position = 0;
    double Version = 0;
    bool Is32Bit = false;

    explicit BinaryStream(std::vector<uint8_t> buf) : data(std::move(buf)) {}

    size_t Length() const { return data.size(); }
    size_t PointerSize() const { return Is32Bit ? 4u : 8u; }

    void Seek(size_t pos) {
        if (pos > data.size()) throw std::runtime_error("seek past end");
        Position = pos;
    }

    template <typename T>
    T Read() {
        if (Position + sizeof(T) > data.size()) throw std::runtime_error("read past end");
        T v{};
        std::memcpy(&v, data.data() + Position, sizeof(T));
        Position += sizeof(T);
        return v;
    }

    uint8_t ReadU8() { return Read<uint8_t>(); }
    uint16_t ReadU16() { return Read<uint16_t>(); }
    uint32_t ReadU32() { return Read<uint32_t>(); }
    uint64_t ReadU64() { return Read<uint64_t>(); }
    int32_t ReadI32() { return Read<int32_t>(); }
    int64_t ReadI64() { return Read<int64_t>(); }

    uint64_t ReadUIntPtr() {
        return Is32Bit ? static_cast<uint64_t>(ReadU32()) : ReadU64();
    }

    int64_t ReadIntPtr() {
        return Is32Bit ? static_cast<int64_t>(ReadI32()) : ReadI64();
    }

    std::vector<uint8_t> ReadBytes(size_t n) {
        if (Position + n > data.size()) throw std::runtime_error("read bytes past end");
        std::vector<uint8_t> out(data.begin() + static_cast<std::ptrdiff_t>(Position),
                                 data.begin() + static_cast<std::ptrdiff_t>(Position + n));
        Position += n;
        return out;
    }

    std::string ReadStringToNull(size_t offset) const {
        if (offset >= data.size()) return {};
        size_t end = offset;
        while (end < data.size() && data[end] != 0) ++end;
        return std::string(reinterpret_cast<const char*>(data.data() + offset), end - offset);
    }

    // IL2CPP compressed unsigned int (metadata v27+ type encoding helpers)
    uint32_t ReadCompressedUInt32() {
        uint8_t b = ReadU8();
        if ((b & 0x80) == 0) return b;
        if ((b & 0xC0) == 0x80) return ((b & ~0x80u) << 8) | ReadU8();
        return (ReadU8() << 24) | (ReadU8() << 16) | (ReadU8() << 8) | ReadU8();
    }

    int32_t ReadCompressedInt32() {
        uint32_t encoded = ReadCompressedUInt32();
        uint32_t unsignedValue = encoded >> 1;
        if (encoded & 1) return -(static_cast<int32_t>(unsignedValue) + 1);
        return static_cast<int32_t>(unsignedValue);
    }

    bool InRange(size_t off, size_t n) const {
        return off <= data.size() && n <= data.size() - off;
    }
};

inline bool InVersion(double v, double minV, double maxV) {
    return v + 1e-9 >= minV && v <= maxV + 1e-9;
}

} // namespace static_dump

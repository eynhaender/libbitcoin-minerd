#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace minerd {

using Bytes = std::vector<std::uint8_t>;
using Hash256 = std::array<std::uint8_t, 32>;

std::string to_hex(std::span<const std::uint8_t> data);
std::optional<Bytes> from_hex(std::string_view hex);
/// Parses exactly 32 bytes of hex (no byte order change).
std::optional<Hash256> hash_from_hex(std::string_view hex);
/// Bitcoin "display order" (reversed) <-> internal order.
Hash256 reversed(const Hash256& h);

inline std::uint32_t read_le32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
           (std::uint32_t(p[3]) << 24);
}
inline std::uint32_t read_be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) |
           std::uint32_t(p[3]);
}
inline void write_le32(std::uint8_t* p, std::uint32_t v) {
    p[0] = std::uint8_t(v); p[1] = std::uint8_t(v >> 8); p[2] = std::uint8_t(v >> 16); p[3] = std::uint8_t(v >> 24);
}
inline void write_be32(std::uint8_t* p, std::uint32_t v) {
    p[0] = std::uint8_t(v >> 24); p[1] = std::uint8_t(v >> 16); p[2] = std::uint8_t(v >> 8); p[3] = std::uint8_t(v);
}
inline std::uint32_t bswap32(std::uint32_t v) {
    return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24);
}

} // namespace minerd

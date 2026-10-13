#include "minerd/util/bytes.hpp"

#include <algorithm>

namespace minerd {
namespace {
int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
} // namespace

std::string to_hex(std::span<const std::uint8_t> data) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (auto b : data) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0x0F]);
    }
    return out;
}

std::optional<Bytes> from_hex(std::string_view hex) {
    if (hex.size() % 2 != 0) return std::nullopt;
    Bytes out(hex.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int hi = nibble(hex[2 * i]);
        const int lo = nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out[i] = std::uint8_t((hi << 4) | lo);
    }
    return out;
}

std::optional<Hash256> hash_from_hex(std::string_view hex) {
    auto b = from_hex(hex);
    if (!b || b->size() != 32) return std::nullopt;
    Hash256 h{};
    std::copy(b->begin(), b->end(), h.begin());
    return h;
}

Hash256 reversed(const Hash256& h) {
    Hash256 r = h;
    std::reverse(r.begin(), r.end());
    return r;
}

} // namespace minerd

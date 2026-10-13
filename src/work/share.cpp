#include "minerd/work/share.hpp"

#include <cmath>
#include <limits>

namespace minerd::work {
namespace {
constexpr double kDiff1 = 0xFFFF * 0x1p208;  // 0xFFFF * 2^208

double as_double_le(const Hash256& h) {
    double v = 0;
    for (int i = 31; i >= 0; --i) v = v * 256.0 + h[std::size_t(i)];
    return v;
}
} // namespace

double hash_difficulty(const Hash256& h) {
    const double v = as_double_le(h);
    return v == 0 ? std::numeric_limits<double>::infinity() : kDiff1 / v;
}

double nbits_difficulty(std::uint32_t nbits) {
    const int exponent = int(nbits >> 24);
    const double mantissa = double(nbits & 0x007FFFFF);
    if (mantissa == 0) return 0;
    const double target = mantissa * std::pow(256.0, exponent - 3);
    return kDiff1 / target;
}

bool hash_meets_nbits(const Hash256& h, std::uint32_t nbits) {
    // Build the target as a little-endian 256-bit number.
    std::array<std::uint8_t, 32> target{};
    const int exponent = int(nbits >> 24);
    const std::uint32_t mantissa = nbits & 0x007FFFFF;
    if (nbits & 0x00800000) return false;  // negative target: invalid
    for (int i = 0; i < 3; ++i) {
        const int pos = exponent - 3 + i;
        if (pos >= 0 && pos < 32) target[std::size_t(pos)] = std::uint8_t(mantissa >> (8 * i));
        else if (pos >= 32 && ((mantissa >> (8 * i)) & 0xFF)) return true;  // overflow: everything passes
    }
    for (int i = 31; i >= 0; --i) {
        if (h[std::size_t(i)] < target[std::size_t(i)]) return true;
        if (h[std::size_t(i)] > target[std::size_t(i)]) return false;
    }
    return true;  // equal
}

ShareCheck check_share(const WorkItem& work, std::uint32_t nonce, std::uint32_t version) {
    auto hdr = work.header;
    header::set_version(hdr, version);
    header::set_nonce(hdr, nonce);
    ShareCheck c;
    c.hash = crypto::sha256d(hdr);
    c.difficulty = hash_difficulty(c.hash);
    const double pool_diff = work.job ? work.job->pool_difficulty : 1.0;
    c.meets_pool = c.difficulty >= pool_diff;
    c.meets_network = hash_meets_nbits(c.hash, header::nbits(hdr));
    return c;
}

} // namespace minerd::work

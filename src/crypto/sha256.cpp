#include "minerd/crypto/sha256.hpp"

#include <cstring>

namespace minerd::crypto {
namespace {
constexpr std::uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

/// Hashes `len` bytes starting from a given state, applying padding for a
/// total message length of `total_len` bytes.
Hash256 finish(Sha256State st, const std::uint8_t* data, std::size_t len, std::uint64_t total_len) {
    while (len >= 64) {
        sha256_compress(st, data);
        data += 64;
        len -= 64;
    }
    std::uint8_t block[128] = {};
    std::memcpy(block, data, len);
    block[len] = 0x80;
    const std::size_t blocks = (len + 9 > 64) ? 2 : 1;
    const std::uint64_t bits = total_len * 8;
    for (int i = 0; i < 8; ++i) block[blocks * 64 - 1 - i] = std::uint8_t(bits >> (8 * i));
    for (std::size_t b = 0; b < blocks; ++b) sha256_compress(st, block + 64 * b);

    Hash256 out{};
    for (int i = 0; i < 8; ++i) write_be32(out.data() + 4 * i, st.h[i]);
    return out;
}
} // namespace

Sha256State sha256_initial_state() {
    return {{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}};
}

void sha256_compress(Sha256State& state, const std::uint8_t* p) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) w[i] = read_be32(p + 4 * i);
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = state.h[0], b = state.h[1], c = state.h[2], d = state.h[3];
    std::uint32_t e = state.h[4], f = state.h[5], g = state.h[6], h = state.h[7];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + S1 + ch + K[i] + w[i];
        const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    state.h[0] += a; state.h[1] += b; state.h[2] += c; state.h[3] += d;
    state.h[4] += e; state.h[5] += f; state.h[6] += g; state.h[7] += h;
}

Hash256 sha256(std::span<const std::uint8_t> data) {
    return finish(sha256_initial_state(), data.data(), data.size(), data.size());
}

Hash256 sha256d(std::span<const std::uint8_t> data) {
    const Hash256 first = sha256(data);
    return sha256(first);
}

Sha256State header_midstate(const std::uint8_t* header80) {
    Sha256State st = sha256_initial_state();
    sha256_compress(st, header80);
    return st;
}

Hash256 sha256d_header_from_midstate(const Sha256State& midstate, const std::uint8_t* header80) {
    const Hash256 first = finish(midstate, header80 + 64, 16, 80);
    return sha256(first);
}

} // namespace minerd::crypto

#pragma once
// SHA-256 with an exposed compression function.
//
// The host side of an ASIC miner needs raw midstates (SHA-256 state after the
// first 64 header bytes) for chips that take midstates instead of full headers,
// which is why this is implemented here rather than hidden behind a library.
// With MINERD_WITH_LIBBITCOIN, sha256d() is additionally cross-checked against
// libbitcoin-system in the tests (see src/crypto/libbitcoin_backend.cpp).

#include "minerd/util/bytes.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace minerd::crypto {

struct Sha256State {
    std::array<std::uint32_t, 8> h;
};

Sha256State sha256_initial_state();
/// Compresses exactly one 64-byte block into `state`.
void sha256_compress(Sha256State& state, const std::uint8_t* block64);

Hash256 sha256(std::span<const std::uint8_t> data);
/// Double SHA-256 as used for Bitcoin headers, txids and merkle nodes.
Hash256 sha256d(std::span<const std::uint8_t> data);

/// State after compressing the first 64 bytes of an 80-byte header.
Sha256State header_midstate(const std::uint8_t* header80);

/// Fast path for header hashing given a precomputed midstate (used by the
/// simulator and by host-side share validation loops).
Hash256 sha256d_header_from_midstate(const Sha256State& midstate, const std::uint8_t* header80);

#ifdef MINERD_HAVE_LIBBITCOIN
/// libbitcoin-system's double-SHA256, for cross-checking.
Hash256 sha256d_libbitcoin(std::span<const std::uint8_t> data);
#endif

} // namespace minerd::crypto

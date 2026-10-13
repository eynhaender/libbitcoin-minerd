// Only compiled with -DMINERD_WITH_LIBBITCOIN=ON.
//
// libbitcoin-system is used where it adds value on the miner side: a second,
// independently maintained double-SHA256 to cross-check the hand-written
// midstate code, and (later) script/address handling when the daemon builds
// its own coinbase for solo mining against libbitcoin-server.
//
// NOTE: written against the libbitcoin-system v4 API (namespace
// libbitcoin::system, bitcoin_hash()). Verify the symbol names against the
// exact version you build (v4/v5) before relying on it.

#include "minerd/crypto/sha256.hpp"

#include <bitcoin/system.hpp>

#include <algorithm>

namespace minerd::crypto {

Hash256 sha256d_libbitcoin(std::span<const std::uint8_t> data) {
    const libbitcoin::system::data_chunk chunk(data.begin(), data.end());
    const auto digest = libbitcoin::system::bitcoin_hash(chunk);
    Hash256 out{};
    std::copy(digest.begin(), digest.end(), out.begin());
    return out;
}

} // namespace minerd::crypto

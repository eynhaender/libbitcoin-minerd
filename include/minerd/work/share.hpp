#pragma once
// Host-side verification of nonces returned by chips. Every nonce is
// re-hashed on the control board: this catches hardware errors (which feed the
// autotuner) and guarantees we never submit garbage to a pool.

#include "minerd/work/job.hpp"

namespace minerd::work {

/// Difficulty-1 target: 0x00000000FFFF0000...0000
double hash_difficulty(const Hash256& hash_internal_order);
double nbits_difficulty(std::uint32_t nbits);
/// Exact 256-bit comparison against the compact target.
bool hash_meets_nbits(const Hash256& hash_internal_order, std::uint32_t nbits);

struct ShareCheck {
    Hash256 hash{};
    double difficulty = 0;
    bool meets_pool = false;
    bool meets_network = false;   ///< a block!
};

ShareCheck check_share(const WorkItem& work, std::uint32_t nonce, std::uint32_t version);

/// Expected hashes per returned nonce at a given chip-side difficulty.
inline double hashes_per_nonce(double difficulty) { return difficulty * 4294967296.0; }

} // namespace minerd::work

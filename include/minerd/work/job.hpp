#pragma once
// Chip-agnostic work representation.
//
//   Job       : what the pool/node sent (Stratum notify + session data)
//   WorkItem  : one concrete 80-byte header (unique extranonce2) handed to a chain
//   NonceResult : what a chain reports back, already decoded from chip format
//
// Nothing in here knows about a particular ASIC. Chip drivers translate
// WorkItem -> wire format and wire format -> NonceResult.

#include "minerd/crypto/sha256.hpp"
#include "minerd/util/bytes.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace minerd::work {

struct Job {
    std::string job_id;
    Hash256 prev_hash{};                 ///< internal (header) byte order
    Bytes coinb1;
    Bytes coinb2;
    std::vector<Hash256> merkle_branch;  ///< internal byte order
    std::uint32_t version = 0;
    std::uint32_t nbits = 0;
    std::uint32_t ntime = 0;
    bool clean_jobs = false;

    // Session data captured at notify time so a Job is self-contained.
    Bytes extranonce1;
    std::size_t extranonce2_size = 4;
    std::uint32_t version_mask = 0;      ///< BIP310 mask granted by the pool, 0 = no rolling
    double pool_difficulty = 1.0;

    std::uint64_t epoch = 0;             ///< bumped on every clean_jobs; older epochs are stale
};

namespace header {
// 80-byte header layout: version | prev_hash | merkle_root | ntime | nbits | nonce
constexpr std::size_t kVersion = 0, kPrevHash = 4, kMerkleRoot = 36, kNtime = 68, kNbits = 72, kNonce = 76;
using Header = std::array<std::uint8_t, 80>;

inline std::uint32_t version(const Header& h) { return read_le32(h.data() + kVersion); }
inline std::uint32_t ntime(const Header& h) { return read_le32(h.data() + kNtime); }
inline std::uint32_t nbits(const Header& h) { return read_le32(h.data() + kNbits); }
inline std::uint32_t nonce(const Header& h) { return read_le32(h.data() + kNonce); }
inline void set_version(Header& h, std::uint32_t v) { write_le32(h.data() + kVersion, v); }
inline void set_ntime(Header& h, std::uint32_t v) { write_le32(h.data() + kNtime, v); }
inline void set_nonce(Header& h, std::uint32_t v) { write_le32(h.data() + kNonce, v); }
} // namespace header

struct WorkItem {
    std::uint64_t id = 0;                    ///< host-unique id; chip drivers map their small job ids to this
    std::shared_ptr<const Job> job;
    Bytes extranonce2;
    header::Header header{};                 ///< nonce = 0, version = job base version
    std::uint32_t version_mask = 0;
    /// Midstates for rolled versions 0..n-1 (only filled for chips that need them).
    std::vector<crypto::Sha256State> midstates;
    std::vector<std::uint32_t> midstate_versions;

    std::uint32_t base_version() const { return header::version(header); }
};

struct NonceResult {
    std::uint64_t work_id = 0;
    std::uint32_t nonce = 0;
    std::uint32_t version = 0;   ///< full (rolled) header version
    unsigned chain = 0;
    unsigned chip = 0;           ///< chip index on the chain (0-based), if the chip reports it
};

} // namespace minerd::work

#pragma once

#include "minerd/work/job.hpp"

#include <atomic>
#include <memory>

namespace minerd::work {

/// Merkle root (internal order) for coinbase = coinb1|en1|en2|coinb2.
Hash256 merkle_root(const Job& job, std::span<const std::uint8_t> extranonce2);

/// Complete header for a given extranonce2/ntime/version/nonce (used by pools
/// and tests to re-validate a submitted share independently of WorkItems).
header::Header build_header(const Job& job, std::span<const std::uint8_t> extranonce2, std::uint32_t ntime,
                            std::uint32_t version, std::uint32_t nonce);

/// Places the bits of `n` into the set bits of `mask` (software PDEP).
/// rolled_version(base, mask, n) yields the n-th version reachable via BIP310.
std::uint32_t rolled_version(std::uint32_t base, std::uint32_t mask, std::uint32_t n);

/// Produces unique WorkItems for one Job by rolling extranonce2.
/// Thread-safe: several chain dispatchers may pull from one generator.
class WorkGenerator {
public:
    explicit WorkGenerator(std::shared_ptr<const Job> job);

    /// `midstates` > 0 precomputes that many midstates over rolled versions
    /// (for chips that hash from midstates instead of full headers).
    WorkItem next(std::uint64_t work_id, unsigned midstates = 0);

    const std::shared_ptr<const Job>& job() const { return job_; }

private:
    std::shared_ptr<const Job> job_;
    std::atomic<std::uint64_t> en2_counter_{0};
};

} // namespace minerd::work

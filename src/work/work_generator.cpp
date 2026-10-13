#include "minerd/work/work_generator.hpp"

#include <algorithm>
#include <cstring>

namespace minerd::work {

Hash256 merkle_root(const Job& job, std::span<const std::uint8_t> en2) {
    Bytes coinbase;
    coinbase.reserve(job.coinb1.size() + job.extranonce1.size() + en2.size() + job.coinb2.size());
    coinbase.insert(coinbase.end(), job.coinb1.begin(), job.coinb1.end());
    coinbase.insert(coinbase.end(), job.extranonce1.begin(), job.extranonce1.end());
    coinbase.insert(coinbase.end(), en2.begin(), en2.end());
    coinbase.insert(coinbase.end(), job.coinb2.begin(), job.coinb2.end());

    Hash256 root = crypto::sha256d(coinbase);
    std::uint8_t buf[64];
    for (const auto& branch : job.merkle_branch) {
        std::memcpy(buf, root.data(), 32);
        std::memcpy(buf + 32, branch.data(), 32);
        root = crypto::sha256d(std::span<const std::uint8_t>(buf, 64));
    }
    return root;
}

header::Header build_header(const Job& j, std::span<const std::uint8_t> en2, std::uint32_t ntime,
                            std::uint32_t version, std::uint32_t nonce) {
    header::Header h{};
    const Hash256 root = merkle_root(j, en2);
    header::set_version(h, version);
    std::memcpy(h.data() + header::kPrevHash, j.prev_hash.data(), 32);
    std::memcpy(h.data() + header::kMerkleRoot, root.data(), 32);
    header::set_ntime(h, ntime);
    write_le32(h.data() + header::kNbits, j.nbits);
    header::set_nonce(h, nonce);
    return h;
}

std::uint32_t rolled_version(std::uint32_t base, std::uint32_t mask, std::uint32_t n) {
    std::uint32_t bits = 0;
    for (std::uint32_t m = mask; m != 0 && n != 0; m &= m - 1) {
        const std::uint32_t lowest = m & (~m + 1);
        if (n & 1u) bits |= lowest;
        n >>= 1;
    }
    return (base & ~mask) | bits;
}

WorkGenerator::WorkGenerator(std::shared_ptr<const Job> job) : job_(std::move(job)) {}

WorkItem WorkGenerator::next(std::uint64_t work_id, unsigned midstates) {
    const Job& j = *job_;
    WorkItem w;
    w.id = work_id;
    w.job = job_;
    w.version_mask = j.version_mask;

    // extranonce2: little-endian counter, as most pools/cgminer expect.
    const std::uint64_t ctr = en2_counter_.fetch_add(1, std::memory_order_relaxed);
    w.extranonce2.assign(j.extranonce2_size, 0);
    for (std::size_t i = 0; i < j.extranonce2_size && i < 8; ++i) w.extranonce2[i] = std::uint8_t(ctr >> (8 * i));

    const Hash256 root = merkle_root(j, w.extranonce2);
    header::set_version(w.header, j.version);
    std::memcpy(w.header.data() + header::kPrevHash, j.prev_hash.data(), 32);
    std::memcpy(w.header.data() + header::kMerkleRoot, root.data(), 32);
    header::set_ntime(w.header, j.ntime);
    write_le32(w.header.data() + header::kNbits, j.nbits);
    header::set_nonce(w.header, 0);

    if (midstates > 0) {
        const unsigned n = (j.version_mask == 0) ? 1 : midstates;
        auto tmp = w.header;
        for (unsigned i = 0; i < n; ++i) {
            const std::uint32_t v = rolled_version(j.version, j.version_mask, i);
            header::set_version(tmp, v);
            w.midstates.push_back(crypto::header_midstate(tmp.data()));
            w.midstate_versions.push_back(v);
        }
    }
    return w;
}

} // namespace minerd::work

#pragma once
// Stratum V1 client with BIP310 version rolling (mining.configure).
// Works against regular pools and against libbitcoin-server's stratum
// endpoint (point [pool] url at the `bs` stratum bind address).

#include "minerd/pool/pool_client.hpp"
#include "minerd/util/json.hpp"

#include <optional>

namespace minerd::pool {

struct StratumSession {
    Bytes extranonce1;
    std::size_t extranonce2_size = 4;
    std::uint32_t version_mask = 0;
    double difficulty = 1.0;
};

/// Parses mining.notify params into a Job (exposed for unit tests).
std::optional<work::Job> parse_notify(const json::Value& params, const StratumSession& session, std::string* error = nullptr);

/// Formats mining.submit params: [user, job_id, en2, ntime, nonce, version_bits].
json::Value submit_params(const std::string& user, const Share& share);

struct StratumUrl {
    std::string host;
    unsigned port = 0;
};
std::optional<StratumUrl> parse_stratum_url(const std::string& url);

std::unique_ptr<IPoolClient> make_stratum_v1_client(const board::PoolProfile& profile);

} // namespace minerd::pool

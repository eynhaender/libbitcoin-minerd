#pragma once
// Upstream abstraction: anything that hands out Jobs and accepts shares.
//   * StratumV1Client  – pools and libbitcoin-server's stratum endpoint
//   * LocalTestPool    – offline job source for development / CI
//   * (planned) StratumV2Client, GetBlockTemplate solo client

#include "minerd/board/profile.hpp"
#include "minerd/work/job.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace minerd::pool {

struct Share {
    std::uint64_t seq = 0;
    std::string job_id;
    Bytes extranonce2;
    std::uint32_t ntime = 0;
    std::uint32_t nonce = 0;
    std::uint32_t version = 0;        ///< full rolled version
    std::uint32_t version_mask = 0;
    double difficulty = 0;            ///< actual share difficulty (for stats)
};

struct PoolEvents {
    std::function<void(std::shared_ptr<const work::Job>)> on_job;
    std::function<void(std::uint64_t seq, bool accepted, const std::string& reason)> on_share_result;
    std::function<void(bool connected)> on_connection;
};

class IPoolClient {
public:
    virtual ~IPoolClient() = default;
    virtual Status start(PoolEvents events) = 0;
    virtual void stop() = 0;
    virtual void submit(const Share& share) = 0;
    virtual bool connected() const = 0;
    virtual std::string describe() const = 0;
};

/// "stratum+tcp://host:port" -> StratumV1Client, "local-test" -> LocalTestPool
std::unique_ptr<IPoolClient> make_pool_client(const board::PoolProfile& profile, Status& status);

struct LocalTestPoolOptions {
    double difficulty = 0.0001;
    std::chrono::seconds job_interval{30};
    std::uint32_t version_mask = 0x1FFFE000;
};
std::unique_ptr<IPoolClient> make_local_test_pool(LocalTestPoolOptions opts);

} // namespace minerd::pool

#pragma once
// cgminer-compatible read-only API (TCP, default 127.0.0.1:4028) so existing
// farm-monitoring tools work unchanged. Supported commands: version, summary,
// devs, pools, stats. Accepts both `summary` and `{"command":"summary"}`.
//
// The legacy cgminer API has no authentication: bind to localhost unless the
// network is trusted. Write commands are intentionally not implemented.

#include "minerd/control/miner.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace minerd::api {

std::string handle_request(const std::string& request, const control::MinerStats& stats);

class CgminerApiServer {
public:
    using StatsProvider = std::function<control::MinerStats()>;
    CgminerApiServer(std::string bind, unsigned port, StatsProvider provider);
    ~CgminerApiServer();

    Status start();
    void stop();

private:
    void run();
    std::string bind_;
    unsigned port_;
    StatsProvider provider_;
    int listen_fd_ = -1;
    std::atomic<bool> stop_{true};
    std::thread thread_;
};

} // namespace minerd::api

#include "minerd/api/cgminer_api.hpp"

#include "minerd/util/json.hpp"
#include "minerd/util/log.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace minerd::api {
namespace {
using json::Array;
using json::Object;
using json::Value;

Value status_block(const std::string& code, const std::string& msg) {
    return Value(Array{Value(Object{{"STATUS", Value(code)},
                                    {"When", Value(static_cast<long long>(std::time(nullptr)))},
                                    {"Code", Value(code == "S" ? 11 : 14)},
                                    {"Msg", Value(msg)},
                                    {"Description", Value("minerd 0.1")}})});
}

Value opt_num(const std::optional<double>& v) { return v ? Value(*v) : Value(nullptr); }
} // namespace

std::string handle_request(const std::string& raw, const control::MinerStats& s) {
    std::string cmd = raw;
    while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r' || cmd.back() == '\0' || cmd.back() == ' ')) cmd.pop_back();
    if (!cmd.empty() && cmd.front() == '{') {
        if (auto j = json::parse(cmd)) cmd = (*j)["command"].as_string();
    }
    if (auto bar = cmd.find('|'); bar != std::string::npos) cmd = cmd.substr(0, bar);

    Object out;
    if (cmd == "version") {
        out["STATUS"] = status_block("S", "minerd versions");
        out["VERSION"] = Value(Array{Value(Object{{"CGMiner", Value("minerd-0.1")}, {"API", Value("3.7")}})});
    } else if (cmd == "summary") {
        out["STATUS"] = status_block("S", "Summary");
        out["SUMMARY"] = Value(Array{Value(Object{
            {"Elapsed", Value(static_cast<long long>(s.uptime_s))},
            {"MHS av", Value(s.hashrate_ghs_15m * 1000.0)},
            {"MHS 1m", Value(s.hashrate_ghs_1m * 1000.0)},
            {"GHS 1m", Value(s.hashrate_ghs_1m)},
            {"GHS expected", Value(s.expected_ghs)},
            {"Accepted", Value(s.accepted)},
            {"Rejected", Value(s.rejected)},
            {"Stale", Value(s.stale)},
            {"Hardware Errors", Value(s.hw_errors)},
            {"Best Share", Value(s.best_share)},
            {"Found Blocks", Value(s.blocks)},
            {"Temperature Max", opt_num(s.max_temp_c)},
            {"Fan Duty", Value(s.fan_duty)},
            {"Throttling", Value(s.throttling)},
            {"Safety Trip", Value(s.tripped ? s.trip_reason : std::string())},
        })});
    } else if (cmd == "devs" || cmd == "edevs") {
        out["STATUS"] = status_block("S", "Devices");
        Array devs;
        for (const auto& c : s.chains) {
            devs.push_back(Value(Object{{"ASC", Value(c.index)},
                                        {"Name", Value(c.chip)},
                                        {"Enabled", Value(c.running ? "Y" : "N")},
                                        {"Status", Value(c.running ? "Alive" : "Dead")},
                                        {"Chips", Value(c.chips)},
                                        {"Frequency", Value(c.avg_freq_mhz)},
                                        {"Temperature", opt_num(c.max_temp_c)},
                                        {"Voltage", opt_num(c.voltage_v)},
                                        {"MHS 1m", Value(c.hashrate_ghs_1m * 1000.0)},
                                        {"Nonces", Value(c.nonces)},
                                        {"Hardware Errors", Value(c.hw_errors)}}));
        }
        out["DEVS"] = Value(std::move(devs));
    } else if (cmd == "pools") {
        out["STATUS"] = status_block("S", "Pools");
        out["POOLS"] = Value(Array{Value(Object{{"POOL", Value(0)},
                                                {"URL", Value(s.pool)},
                                                {"Status", Value(s.pool_connected ? "Alive" : "Dead")},
                                                {"Accepted", Value(s.accepted)},
                                                {"Rejected", Value(s.rejected)},
                                                {"Stale", Value(s.stale)}})});
    } else if (cmd == "stats") {
        out["STATUS"] = status_block("S", "Stats");
        Array fans;
        for (const auto& f : s.fan_rpm) fans.push_back(f ? Value(*f) : Value(nullptr));
        out["STATS"] = Value(Array{Value(Object{{"Fan RPM", Value(std::move(fans))}, {"Fan Duty", Value(s.fan_duty)}})});
    } else {
        out["STATUS"] = status_block("E", "Invalid command");
    }
    out["id"] = Value(1);
    return Value(std::move(out)).dump();
}

CgminerApiServer::CgminerApiServer(std::string bind, unsigned port, StatsProvider p)
    : bind_(std::move(bind)), port_(port), provider_(std::move(p)) {}

CgminerApiServer::~CgminerApiServer() { stop(); }

Status CgminerApiServer::start() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) return Status::error(std::string("api socket: ") + std::strerror(errno));
    int one = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(std::uint16_t(port_));
    if (::inet_pton(AF_INET, bind_.c_str(), &addr.sin_addr) != 1) return Status::error("api: bad bind address " + bind_);
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(listen_fd_, 8) != 0) {
        const std::string e = std::strerror(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        return Status::error("api bind " + bind_ + ":" + std::to_string(port_) + ": " + e);
    }
    stop_.store(false);
    thread_ = std::thread([this] { run(); });
    log::info("api", "listening on ", bind_, ":", port_);
    return Status::ok();
}

void CgminerApiServer::stop() {
    if (stop_.exchange(true)) return;
    if (thread_.joinable()) thread_.join();
    if (listen_fd_ >= 0) ::close(listen_fd_);
    listen_fd_ = -1;
}

void CgminerApiServer::run() {
    while (!stop_.load()) {
        pollfd p{listen_fd_, POLLIN, 0};
        if (::poll(&p, 1, 250) <= 0) continue;
        const int fd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0) continue;
        std::string req;
        char buf[1024];
        pollfd cp{fd, POLLIN, 0};
        while (req.size() < 4096 && ::poll(&cp, 1, 500) > 0) {
            const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
            if (n <= 0) break;
            req.append(buf, std::size_t(n));
            if (req.find('\n') != std::string::npos || req.find('}') != std::string::npos ||
                req.find('\0') != std::string::npos)
                break;
            if (req.find('{') == std::string::npos && n < ssize_t(sizeof buf)) break;  // plain command
        }
        const std::string resp = handle_request(req, provider_());
        (void)::send(fd, resp.data(), resp.size(), MSG_NOSIGNAL);
        (void)::send(fd, "", 1, MSG_NOSIGNAL);  // cgminer terminates with NUL
        ::close(fd);
    }
}

} // namespace minerd::api

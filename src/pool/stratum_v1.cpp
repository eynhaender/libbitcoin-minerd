#include "minerd/pool/stratum_v1.hpp"

#include "minerd/pool/tcp_line_socket.hpp"
#include "minerd/util/log.hpp"

#include <atomic>
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>

namespace minerd::pool {
namespace {
constexpr std::string_view kLog = "stratum";
using namespace std::chrono_literals;

std::optional<std::uint32_t> parse_u32_hex(const json::Value& v) {
    if (!v.is_string() || v.as_string().size() != 8) return std::nullopt;
    auto b = from_hex(v.as_string());
    if (!b) return std::nullopt;
    return read_be32(b->data());
}

std::string u32_hex(std::uint32_t v) {
    char buf[9];
    std::snprintf(buf, sizeof buf, "%08x", v);
    return buf;
}
} // namespace

std::optional<work::Job> parse_notify(const json::Value& p, const StratumSession& s, std::string* error) {
    auto fail = [&](const char* m) -> std::optional<work::Job> {
        if (error) *error = m;
        return std::nullopt;
    };
    if (!p.is_array() || p.as_array().size() < 9) return fail("notify: expected 9 params");
    work::Job j;
    j.job_id = p.at(0).as_string();
    if (j.job_id.empty()) return fail("notify: empty job id");

    // prevhash: 8 little-endian words in big-endian word order -> swap each word.
    auto prev = hash_from_hex(p.at(1).as_string());
    if (!prev) return fail("notify: bad prevhash");
    for (int w = 0; w < 8; ++w)
        for (int b = 0; b < 4; ++b) j.prev_hash[std::size_t(4 * w + b)] = (*prev)[std::size_t(4 * w + 3 - b)];

    auto c1 = from_hex(p.at(2).as_string());
    auto c2 = from_hex(p.at(3).as_string());
    if (!c1 || !c2) return fail("notify: bad coinbase");
    j.coinb1 = std::move(*c1);
    j.coinb2 = std::move(*c2);

    for (const auto& br : p.at(4).as_array()) {
        auto h = hash_from_hex(br.as_string());
        if (!h) return fail("notify: bad merkle branch");
        j.merkle_branch.push_back(*h);
    }
    auto version = parse_u32_hex(p.at(5));
    auto nbits = parse_u32_hex(p.at(6));
    auto ntime = parse_u32_hex(p.at(7));
    if (!version || !nbits || !ntime) return fail("notify: bad version/nbits/ntime");
    j.version = *version;
    j.nbits = *nbits;
    j.ntime = *ntime;
    j.clean_jobs = p.at(8).as_bool(false);

    j.extranonce1 = s.extranonce1;
    j.extranonce2_size = s.extranonce2_size;
    j.version_mask = s.version_mask;
    j.pool_difficulty = s.difficulty;
    return j;
}

json::Value submit_params(const std::string& user, const Share& sh) {
    json::Array a{json::Value(user), json::Value(sh.job_id), json::Value(to_hex(sh.extranonce2)),
                  json::Value(u32_hex(sh.ntime)), json::Value(u32_hex(sh.nonce))};
    if (sh.version_mask) a.emplace_back(u32_hex(sh.version & sh.version_mask));  // BIP310
    return json::Value(std::move(a));
}

std::optional<StratumUrl> parse_stratum_url(const std::string& url) {
    std::string rest = url;
    for (const char* scheme : {"stratum+tcp://", "stratum://", "tcp://"}) {
        if (rest.rfind(scheme, 0) == 0) { rest = rest.substr(std::string(scheme).size()); break; }
    }
    if (rest.find("://") != std::string::npos) return std::nullopt;  // unsupported scheme (e.g. ssl)
    const auto colon = rest.rfind(':');
    if (colon == std::string::npos) return std::nullopt;
    StratumUrl u;
    u.host = rest.substr(0, colon);
    if (u.host.size() > 2 && u.host.front() == '[' && u.host.back() == ']') u.host = u.host.substr(1, u.host.size() - 2);
    try { u.port = unsigned(std::stoul(rest.substr(colon + 1))); } catch (...) { return std::nullopt; }
    if (u.host.empty() || u.port == 0 || u.port > 65535) return std::nullopt;
    return u;
}

namespace {
class StratumV1Client final : public IPoolClient {
public:
    explicit StratumV1Client(board::PoolProfile p) : profile_(std::move(p)) {}
    ~StratumV1Client() override { stop(); }

    Status start(PoolEvents ev) override {
        auto url = parse_stratum_url(profile_.url);
        if (!url) return Status::error("bad stratum url: " + profile_.url);
        url_ = *url;
        events_ = std::move(ev);
        stop_.store(false);
        thread_ = std::thread([this] { run(); });
        return Status::ok();
    }

    void stop() override {
        if (stop_.exchange(true)) return;
        sock_.close();
        if (thread_.joinable()) thread_.join();
    }

    void submit(const Share& sh) override {
        int id;
        {
            std::lock_guard lk(mx_);
            id = next_id_++;
            pending_submits_[id] = sh.seq;
        }
        json::Object req{{"id", json::Value(id)}, {"method", json::Value("mining.submit")},
                         {"params", submit_params(profile_.user, sh)}};
        if (Status s = sock_.send_line(json::Value(req).dump()); !s) {
            log::warn(kLog, "submit failed: ", s.message());
            resolve_submit(id, false, "not connected");
        }
    }

    bool connected() const override { return connected_.load(); }
    std::string describe() const override { return profile_.url; }

private:
    void run() {
        auto backoff = 1s;
        while (!stop_.load()) {
            log::info(kLog, "connecting to ", url_.host, ":", url_.port);
            if (Status s = sock_.connect(url_.host, url_.port, 10s); !s) {
                log::warn(kLog, s.message(), " – retry in ", backoff.count(), "s");
                sleep_interruptible(backoff);
                backoff = std::min(backoff * 2, std::chrono::seconds(60));
                continue;
            }
            backoff = 1s;
            session_loop();
            set_connected(false);
            fail_pending("connection lost");
            sock_.close();
            if (!stop_.load()) sleep_interruptible(2s);
        }
    }

    void session_loop() {
        {
            std::lock_guard lk(mx_);
            session_ = StratumSession{};
            subscribed_ = false;
        }
        // Pipelined handshake; responses are matched by id.
        send({{"id", json::Value(1)}, {"method", json::Value("mining.configure")},
              {"params", json::Value(json::Array{
                             json::Value(json::Array{json::Value("version-rolling")}),
                             json::Value(json::Object{{"version-rolling.mask", json::Value("1fffe000")},
                                                      {"version-rolling.min-bit-count", json::Value(2)}})})}});
        send({{"id", json::Value(2)}, {"method", json::Value("mining.subscribe")},
              {"params", json::Value(json::Array{json::Value("minerd/0.1")})}});
        send({{"id", json::Value(3)}, {"method", json::Value("mining.authorize")},
              {"params", json::Value(json::Array{json::Value(profile_.user), json::Value(profile_.password)})}});
        if (profile_.suggest_difficulty > 0)
            send({{"id", json::Value(4)}, {"method", json::Value("mining.suggest_difficulty")},
                  {"params", json::Value(json::Array{json::Value(profile_.suggest_difficulty)})}});

        auto last_rx = std::chrono::steady_clock::now();
        while (!stop_.load()) {
            bool closed = false;
            auto line = sock_.read_line(1s, closed);
            if (closed) { log::warn(kLog, "connection closed by pool"); return; }
            if (!line) {
                if (std::chrono::steady_clock::now() - last_rx > 180s) { log::warn(kLog, "no data for 180s, reconnecting"); return; }
                continue;
            }
            last_rx = std::chrono::steady_clock::now();
            std::string err;
            auto msg = json::parse(*line, &err);
            if (!msg) { log::warn(kLog, "bad JSON from pool: ", err); continue; }
            handle(*msg);
        }
    }

    void handle(const json::Value& m) {
        const auto& method = m["method"];
        if (method.is_string()) return handle_notification(method.as_string(), m["params"]);

        const int id = int(m["id"].as_number(-1));
        const auto& result = m["result"];
        const auto& error = m["error"];
        switch (id) {
        case 1: {  // configure
            const auto& vr = result["version-rolling"];
            if (vr.as_bool(false)) {
                auto mask = from_hex(result["version-rolling.mask"].as_string());
                std::lock_guard lk(mx_);
                if (mask && mask->size() == 4) session_.version_mask = read_be32(mask->data());
                log::info(kLog, "version rolling enabled, mask ", result["version-rolling.mask"].as_string());
            } else {
                log::info(kLog, "pool does not support version rolling");
            }
            return;
        }
        case 2: {  // subscribe
            auto en1 = from_hex(result.at(1).as_string());
            const auto en2 = result.at(2).as_number(0);
            if (!en1 || en2 <= 0 || en2 > 16) {
                log::error(kLog, "bad subscribe response");
                sock_.close();
                return;
            }
            std::lock_guard lk(mx_);
            session_.extranonce1 = std::move(*en1);
            session_.extranonce2_size = std::size_t(en2);
            subscribed_ = true;
            log::info(kLog, "subscribed, extranonce1=", to_hex(session_.extranonce1), " en2_size=", session_.extranonce2_size);
            return;
        }
        case 3:
            if (result.as_bool(false)) { log::info(kLog, "authorized as ", profile_.user); set_connected(true); }
            else log::error(kLog, "authorization failed: ", error.dump());
            return;
        case 4:
            return;
        default:
            if (id >= 100) {
                const bool ok = result.as_bool(false) && error.is_null();
                resolve_submit(id, ok, ok ? "" : error.dump());
            }
        }
    }

    void handle_notification(const std::string& method, const json::Value& params) {
        if (method == "mining.notify") {
            std::shared_ptr<work::Job> job;
            {
                std::lock_guard lk(mx_);
                if (!subscribed_) { log::warn(kLog, "notify before subscribe response – ignored"); return; }
                std::string err;
                auto j = parse_notify(params, session_, &err);
                if (!j) { log::warn(kLog, err); return; }
                if (j->clean_jobs) ++epoch_;
                j->epoch = epoch_;
                job = std::make_shared<work::Job>(std::move(*j));
                last_job_ = job;
            }
            log::debug(kLog, "job ", job->job_id, job->clean_jobs ? " (clean)" : "");
            if (events_.on_job) events_.on_job(job);
        } else if (method == "mining.set_difficulty") {
            const double d = params.at(0).as_number(0);
            if (d <= 0) return;
            std::shared_ptr<work::Job> reissue;
            {
                std::lock_guard lk(mx_);
                session_.difficulty = d;
                if (last_job_) {
                    reissue = std::make_shared<work::Job>(*last_job_);
                    reissue->pool_difficulty = d;
                    reissue->clean_jobs = false;
                    last_job_ = reissue;
                }
            }
            log::info(kLog, "pool difficulty ", d);
            if (reissue && events_.on_job) events_.on_job(reissue);
        } else if (method == "mining.set_version_mask") {
            auto mask = from_hex(params.at(0).as_string());
            if (mask && mask->size() == 4) {
                std::lock_guard lk(mx_);
                session_.version_mask = read_be32(mask->data());
            }
        } else if (method == "client.reconnect") {
            log::info(kLog, "pool requested reconnect");
            sock_.close();
        } else {
            log::debug(kLog, "ignored notification ", method);
        }
    }

    void send(json::Object o) {
        if (Status s = sock_.send_line(json::Value(std::move(o)).dump()); !s) log::warn(kLog, s.message());
    }

    void resolve_submit(int id, bool ok, const std::string& reason) {
        std::uint64_t seq = 0;
        {
            std::lock_guard lk(mx_);
            auto it = pending_submits_.find(id);
            if (it == pending_submits_.end()) return;
            seq = it->second;
            pending_submits_.erase(it);
        }
        if (events_.on_share_result) events_.on_share_result(seq, ok, reason);
    }

    void fail_pending(const std::string& reason) {
        std::map<int, std::uint64_t> pending;
        {
            std::lock_guard lk(mx_);
            pending.swap(pending_submits_);
        }
        for (const auto& [id, seq] : pending)
            if (events_.on_share_result) events_.on_share_result(seq, false, reason);
    }

    void set_connected(bool c) {
        if (connected_.exchange(c) != c && events_.on_connection) events_.on_connection(c);
    }

    void sleep_interruptible(std::chrono::seconds d) {
        const auto until = std::chrono::steady_clock::now() + d;
        while (!stop_.load() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(100ms);
    }

    board::PoolProfile profile_;
    StratumUrl url_;
    PoolEvents events_;
    TcpLineSocket sock_;
    std::thread thread_;
    std::atomic<bool> stop_{true};
    std::atomic<bool> connected_{false};

    std::mutex mx_;
    StratumSession session_;
    bool subscribed_ = false;
    std::uint64_t epoch_ = 0;
    std::shared_ptr<work::Job> last_job_;
    int next_id_ = 100;
    std::map<int, std::uint64_t> pending_submits_;
};
} // namespace

std::unique_ptr<IPoolClient> make_stratum_v1_client(const board::PoolProfile& p) {
    return std::make_unique<StratumV1Client>(p);
}

std::unique_ptr<IPoolClient> make_pool_client(const board::PoolProfile& p, Status& status) {
    status = Status::ok();
    if (p.url == "local-test") {
        LocalTestPoolOptions o;
        if (p.suggest_difficulty > 0) o.difficulty = p.suggest_difficulty;
        return make_local_test_pool(o);
    }
    if (!parse_stratum_url(p.url)) {
        status = Status::error("unsupported pool url '" + p.url + "' (use stratum+tcp://host:port or local-test)");
        return nullptr;
    }
    return make_stratum_v1_client(p);
}

} // namespace minerd::pool

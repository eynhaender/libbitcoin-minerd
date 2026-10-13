// Offline job source: fabricates jobs on a random previous hash, verifies
// submitted shares exactly like a pool would, and never touches the network.

#include "minerd/pool/pool_client.hpp"
#include "minerd/util/log.hpp"
#include "minerd/work/share.hpp"
#include "minerd/work/work_generator.hpp"

#include <atomic>
#include <ctime>
#include <map>
#include <condition_variable>
#include <mutex>
#include <random>
#include <thread>

namespace minerd::pool {
namespace {

class LocalTestPool final : public IPoolClient {
public:
    explicit LocalTestPool(LocalTestPoolOptions o) : opts_(o), rng_(std::random_device{}()) {}
    ~LocalTestPool() override { stop(); }

    Status start(PoolEvents ev) override {
        events_ = std::move(ev);
        stop_.store(false);
        thread_ = std::thread([this] { run(); });
        return Status::ok();
    }

    void stop() override {
        {
            std::lock_guard lk(mx_);
            if (stop_.exchange(true)) return;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    void submit(const Share& sh) override {
        std::shared_ptr<const work::Job> job;
        {
            std::lock_guard lk(mx_);
            auto it = jobs_.find(sh.job_id);
            if (it != jobs_.end()) job = it->second;
        }
        bool ok = false;
        std::string reason;
        if (!job) {
            reason = "job not found (stale)";
        } else {
            const auto hdr = work::build_header(*job, sh.extranonce2, sh.ntime,
                                                (job->version & ~job->version_mask) | (sh.version & job->version_mask),
                                                sh.nonce);
            const double d = work::hash_difficulty(crypto::sha256d(hdr));
            ok = d >= job->pool_difficulty;
            if (!ok) reason = "low difficulty share";
        }
        if (ok) ++accepted_; else ++rejected_;
        if (events_.on_share_result) events_.on_share_result(sh.seq, ok, reason);
    }

    bool connected() const override { return !stop_.load(); }
    std::string describe() const override { return "local-test"; }

private:
    void run() {
        if (events_.on_connection) events_.on_connection(true);
        std::uint64_t epoch = 0;
        unsigned n = 0;
        while (!stop_.load()) {
            auto job = std::make_shared<work::Job>();
            job->job_id = std::to_string(++n);
            for (auto& b : job->prev_hash) b = std::uint8_t(rng_());
            job->coinb1 = *from_hex("01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff20");
            job->coinb2 = *from_hex("ffffffff0100f2052a010000001976a914000000000000000000000000000000000000000088ac00000000");
            job->extranonce1 = {0xde, 0xad, 0xbe, 0xef};
            job->extranonce2_size = 4;
            job->version = 0x20000000;
            job->version_mask = opts_.version_mask;
            job->nbits = 0x1d00ffff;
            job->ntime = std::uint32_t(std::time(nullptr));
            job->clean_jobs = true;
            job->epoch = ++epoch;
            job->pool_difficulty = opts_.difficulty;
            {
                std::lock_guard lk(mx_);
                jobs_.clear();  // clean job: old ones become stale
                jobs_[job->job_id] = job;
            }
            log::info("local-pool", "new job ", job->job_id, " diff ", opts_.difficulty);
            if (events_.on_job) events_.on_job(job);

            std::unique_lock lk(mx_);
            cv_.wait_for(lk, opts_.job_interval, [&] { return stop_.load(); });
        }
        if (events_.on_connection) events_.on_connection(false);
    }

    LocalTestPoolOptions opts_;
    PoolEvents events_;
    std::mt19937 rng_;
    std::thread thread_;
    std::atomic<bool> stop_{true};
    std::mutex mx_;
    std::condition_variable cv_;
    std::map<std::string, std::shared_ptr<const work::Job>> jobs_;
    std::atomic<std::uint64_t> accepted_{0}, rejected_{0};
};

} // namespace

std::unique_ptr<IPoolClient> make_local_test_pool(LocalTestPoolOptions opts) {
    return std::make_unique<LocalTestPool>(opts);
}

} // namespace minerd::pool

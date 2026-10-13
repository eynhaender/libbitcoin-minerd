#pragma once
// Orchestrator: wires pool -> work generation -> chains -> verification -> pool,
// runs the thermal/tuning control loop and owns the safety supervisor.
//
// Threads:
//   pool client        (inside IPoolClient)
//   dispatcher x N     feeds each chain at its work_interval()
//   collector  x N     polls each chain, verifies nonces, submits shares
//   control    x 1     1 Hz: telemetry, fans, throttling, autotuning
//   safety     x 1     independent supervisor (see safety.hpp)

#include "minerd/board/platform.hpp"
#include "minerd/control/autotuner.hpp"
#include "minerd/control/safety.hpp"
#include "minerd/control/thermal.hpp"
#include "minerd/pool/pool_client.hpp"
#include "minerd/work/work_generator.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace minerd::control {

struct ChainStats {
    unsigned index = 0;
    std::string chip;
    bool running = false;
    unsigned chips = 0;
    double avg_freq_mhz = 0;
    std::optional<double> max_temp_c;
    std::optional<double> voltage_v;
    double hashrate_ghs_1m = 0;
    std::uint64_t nonces = 0;
    std::uint64_t hw_errors = 0;
};

struct MinerStats {
    double uptime_s = 0;
    double hashrate_ghs_1m = 0;
    double hashrate_ghs_15m = 0;
    double expected_ghs = 0;
    std::uint64_t accepted = 0, rejected = 0, stale = 0, hw_errors = 0, nonces = 0, blocks = 0;
    double best_share = 0;
    std::vector<ChainStats> chains;
    std::vector<std::optional<unsigned>> fan_rpm;
    double fan_duty = 0;
    std::optional<double> max_temp_c;
    bool throttling = false;
    bool pool_connected = false;
    std::string pool;
    bool tripped = false;
    std::string trip_reason;
};

class Miner {
public:
    Miner(board::BoardProfile profile, board::Hardware hardware, std::unique_ptr<pool::IPoolClient> pool);
    ~Miner();
    Miner(const Miner&) = delete;
    Miner& operator=(const Miner&) = delete;

    Status start();
    void stop();
    void heartbeat();

    bool tripped() const { return tripped_.load(); }
    MinerStats stats() const;

    /// For tests/tools: inject a tuning strategy before start().
    void set_tuning_strategy(std::unique_ptr<ITuningStrategy> s) { tuner_ = std::move(s); }

private:
    struct ChipCounter {
        std::uint64_t nonces = 0, hw_errors = 0;
        std::uint64_t nonces_at_tune = 0, hw_errors_at_tune = 0;
    };

    void on_job(std::shared_ptr<const work::Job> job);
    void on_share_result(std::uint64_t seq, bool accepted, const std::string& reason);
    void dispatcher_loop(std::size_t board);
    void collector_loop(std::size_t board);
    void control_loop();
    void emergency_stop(const std::string& reason);
    void autotune_step(std::optional<double> max_temp);

    std::shared_ptr<const work::WorkItem> find_work(std::uint64_t id);
    void remember_work(std::shared_ptr<const work::WorkItem> w);

    board::BoardProfile profile_;
    board::Hardware hw_;
    std::unique_ptr<pool::IPoolClient> pool_;
    std::unique_ptr<SafetySupervisor> safety_;
    FanController fans_;
    std::unique_ptr<ITuningStrategy> tuner_;

    std::atomic<bool> stop_{false};
    std::atomic<bool> started_{false};
    std::atomic<bool> tripped_{false};
    std::string trip_reason_;
    std::chrono::steady_clock::time_point start_time_{};

    // current job
    mutable std::mutex job_mx_;
    std::condition_variable job_cv_;
    std::shared_ptr<work::WorkGenerator> generator_;
    std::uint64_t job_seq_ = 0;
    std::atomic<std::uint64_t> current_epoch_{0};
    std::atomic<std::uint64_t> next_work_id_{1};
    std::vector<std::uint32_t> chain_version_mask_;

    // recently dispatched work
    std::mutex work_mx_;
    std::unordered_map<std::uint64_t, std::shared_ptr<const work::WorkItem>> work_;
    std::deque<std::uint64_t> work_order_;

    // stats
    mutable std::mutex stats_mx_;
    std::vector<std::vector<ChipCounter>> chip_counters_;
    std::vector<double> chain_hashes_;          ///< accumulated hashes since start
    std::vector<double> chain_ema_1m_;
    std::vector<double> chain_hashes_prev_;
    double ema_1m_ = 0, ema_15m_ = 0;
    std::atomic<std::uint64_t> accepted_{0}, rejected_{0}, stale_{0}, hw_errors_{0}, nonces_{0}, blocks_{0};
    std::atomic<std::uint64_t> share_seq_{0};
    double best_share_ = 0;
    std::vector<board::BoardTelemetry> last_telemetry_;
    std::vector<std::optional<unsigned>> last_fan_rpm_;
    std::optional<double> last_max_temp_;
    double last_fan_duty_ = 100;
    std::atomic<bool> throttling_{false};

    std::vector<std::thread> threads_;
};

} // namespace minerd::control

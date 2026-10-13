#include "minerd/control/miner.hpp"

#include "minerd/util/log.hpp"
#include "minerd/work/share.hpp"

#include <algorithm>
#include <cmath>

namespace minerd::control {
namespace {
using namespace std::chrono_literals;
constexpr std::string_view kLog = "miner";
constexpr std::size_t kWorkHistory = 8192;
constexpr double kDefaultVersionMask = 0x1FFFE000;
} // namespace

Miner::Miner(board::BoardProfile profile, board::Hardware hw, std::unique_ptr<pool::IPoolClient> pool)
    : profile_(std::move(profile)), hw_(std::move(hw)), pool_(std::move(pool)), fans_(profile_.thermal) {
    const std::size_t n = hw_.boards.size();
    chip_counters_.resize(n);
    chain_hashes_.assign(n, 0);
    chain_hashes_prev_.assign(n, 0);
    chain_ema_1m_.assign(n, 0);
    chain_version_mask_.assign(n, std::uint32_t(kDefaultVersionMask));
    if (profile_.tuning.enabled) tuner_ = std::make_unique<HwErrorStepTuner>(profile_.tuning, profile_.safety);
}

Miner::~Miner() { stop(); }

Status Miner::start() {
    if (started_.exchange(true)) return Status::error("already started");
    start_time_ = std::chrono::steady_clock::now();

    // 1. Cooling first.
    for (auto& f : hw_.fans) (void)f->set_duty(100);

    // 2. Safety supervisor before any power is applied.
    safety_ = std::make_unique<SafetySupervisor>(profile_.safety, [this](const std::string& r) { emergency_stop(r); });
    safety_->start();

    // 3. Control loop early so the supervisor gets telemetry during power-up.
    threads_.emplace_back([this] { control_loop(); });

    // 4. Power.
    if (Status s = hw_.psu->power_on(profile_.psu_voltage_v); !s) {
        stop();
        return Status::error("PSU: " + s.message());
    }

    unsigned running = 0;
    for (std::size_t i = 0; i < hw_.boards.size() && !stop_.load(); ++i) {
        auto& b = *hw_.boards[i];
        if (Status s = b.power_up(chain_version_mask_[i]); !s) {
            log::error(kLog, "chain ", b.profile().index, " failed to start: ", s.message());
            b.power_down();
            continue;
        }
        {
            std::lock_guard lk(stats_mx_);
            chip_counters_[i].assign(b.chain().chip_count(), ChipCounter{});
        }
        ++running;
        heartbeat();  // board bring-up can take seconds per chain
    }
    if (running == 0 || stop_.load()) {
        stop();
        return Status::error(tripped_.load() ? "safety trip during start-up: " + stats().trip_reason : "no hashboard started");
    }

    // 5. Work threads per running board.
    for (std::size_t i = 0; i < hw_.boards.size(); ++i) {
        if (!hw_.boards[i]->running()) continue;
        threads_.emplace_back([this, i] { dispatcher_loop(i); });
        threads_.emplace_back([this, i] { collector_loop(i); });
    }

    // 6. Upstream.
    pool::PoolEvents ev;
    ev.on_job = [this](std::shared_ptr<const work::Job> j) { on_job(std::move(j)); };
    ev.on_share_result = [this](std::uint64_t seq, bool ok, const std::string& r) { on_share_result(seq, ok, r); };
    ev.on_connection = [](bool c) { log::info(kLog, c ? "pool connected" : "pool disconnected"); };
    if (Status s = pool_->start(std::move(ev)); !s) {
        stop();
        return Status::error("pool: " + s.message());
    }
    log::info(kLog, running, "/", hw_.boards.size(), " hashboards running, pool ", pool_->describe());
    return Status::ok();
}

void Miner::stop() {
    if (!started_.load()) return;
    const bool first = !stop_.exchange(true);
    {
        std::lock_guard lk(job_mx_);
    }
    job_cv_.notify_all();
    if (pool_) pool_->stop();
    for (auto& t : threads_)
        if (t.joinable()) t.join();
    threads_.clear();
    {
        for (auto& b : hw_.boards) b->power_down();
        if (hw_.psu) hw_.psu->power_off();
        // Leave fans running to cool down; the control board keeps them powered.
        for (auto& f : hw_.fans) (void)f->set_duty(100);
    }
    if (safety_) safety_->stop();
    if (first) log::info(kLog, "stopped");
}

void Miner::heartbeat() {
    if (safety_) safety_->heartbeat();
}

void Miner::emergency_stop(const std::string& reason) {
    // Called from the safety thread. Cut power first, ask questions later.
    if (tripped_.exchange(true)) return;
    {
        std::lock_guard lk(stats_mx_);
        trip_reason_ = reason;
    }
    for (auto& b : hw_.boards) b->power_down();
    if (hw_.psu) hw_.psu->power_off();
    for (auto& f : hw_.fans) (void)f->set_duty(100);
    stop_.store(true);
    {
        std::lock_guard lk(job_mx_);
    }
    job_cv_.notify_all();
}

// ---------------------------------------------------------------- work flow --

void Miner::on_job(std::shared_ptr<const work::Job> job) {
    {
        std::lock_guard lk(job_mx_);
        generator_ = std::make_shared<work::WorkGenerator>(job);
        ++job_seq_;
        if (job->clean_jobs) current_epoch_.store(job->epoch);
    }
    job_cv_.notify_all();
}

void Miner::remember_work(std::shared_ptr<const work::WorkItem> w) {
    std::lock_guard lk(work_mx_);
    work_order_.push_back(w->id);
    work_.emplace(w->id, std::move(w));
    while (work_order_.size() > kWorkHistory) {
        work_.erase(work_order_.front());
        work_order_.pop_front();
    }
}

std::shared_ptr<const work::WorkItem> Miner::find_work(std::uint64_t id) {
    std::lock_guard lk(work_mx_);
    auto it = work_.find(id);
    return it == work_.end() ? nullptr : it->second;
}

void Miner::dispatcher_loop(std::size_t i) {
    auto& board = *hw_.boards[i];
    auto& chain = board.chain();
    const auto& traits = chain.traits();
    const unsigned midstates = traits.work_format == asic::WorkFormat::Midstates ? std::max(1u, traits.midstates) : 0;
    std::uint64_t seen = 0;
    unsigned consecutive_errors = 0;

    while (!stop_.load()) {
        std::shared_ptr<work::WorkGenerator> gen;
        {
            std::unique_lock lk(job_mx_);
            job_cv_.wait_for(lk, chain.work_interval(), [&] { return stop_.load() || job_seq_ != seen; });
            if (stop_.load()) break;
            gen = generator_;
            seen = job_seq_;
        }
        if (!gen || !board.running()) continue;

        const std::uint32_t mask = gen->job()->version_mask;
        if (mask != chain_version_mask_[i]) {
            if (Status s = chain.set_version_mask(mask); s) chain_version_mask_[i] = mask;
            else log::warn(kLog, "chain ", i, ": set_version_mask: ", s.message());
        }

        auto item = std::make_shared<work::WorkItem>(gen->next(next_work_id_.fetch_add(1), midstates));
        remember_work(item);
        if (Status s = chain.send_work(*item); !s) {
            if (++consecutive_errors == 1 || consecutive_errors % 100 == 0)
                log::warn(kLog, "chain ", i, ": send_work: ", s.message(), " (", consecutive_errors, "x)");
        } else {
            consecutive_errors = 0;
        }
    }
}

void Miner::collector_loop(std::size_t i) {
    auto& chain = hw_.boards[i]->chain();
    while (!stop_.load()) {
        auto r = chain.poll_result(200ms);
        if (!r) continue;
        auto w = find_work(r->work_id);
        if (!w) continue;  // result for work older than our history window

        const double chip_diff = chain.effective_chip_difficulty();
        const auto check = work::check_share(*w, r->nonce, r->version);
        const bool valid = check.difficulty >= chip_diff * 0.5;  // BM13xx rounds difficulty down to 2^n

        {
            std::lock_guard lk(stats_mx_);
            auto& chips = chip_counters_[i];
            if (r->chip < chips.size()) {
                if (valid) ++chips[r->chip].nonces; else ++chips[r->chip].hw_errors;
            }
            if (valid) {
                chain_hashes_[i] += work::hashes_per_nonce(chip_diff);
                best_share_ = std::max(best_share_, check.difficulty);
            }
        }
        if (!valid) { ++hw_errors_; continue; }
        ++nonces_;

        if (check.meets_network) {
            ++blocks_;
            log::info(kLog, "*** BLOCK CANDIDATE *** job ", w->job->job_id, " hash ", to_hex(reversed(check.hash)));
        }
        if (!check.meets_pool) continue;
        if (w->job->epoch < current_epoch_.load()) { ++stale_; continue; }

        pool::Share sh;
        sh.seq = share_seq_.fetch_add(1) + 1;
        sh.job_id = w->job->job_id;
        sh.extranonce2 = w->extranonce2;
        sh.ntime = work::header::ntime(w->header);
        sh.nonce = r->nonce;
        sh.version = r->version;
        sh.version_mask = w->version_mask;
        sh.difficulty = check.difficulty;
        pool_->submit(sh);
    }
}

void Miner::on_share_result(std::uint64_t seq, bool accepted, const std::string& reason) {
    if (accepted) ++accepted_;
    else {
        ++rejected_;
        log::warn(kLog, "share ", seq, " rejected: ", reason);
    }
}

// ------------------------------------------------------------- control loop --

void Miner::control_loop() {
    auto last = std::chrono::steady_clock::now();
    auto last_tune = last;
    while (!stop_.load()) {
        std::this_thread::sleep_for(1s);
        if (stop_.load()) break;
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - last).count();
        last = now;

        // Telemetry
        std::vector<board::BoardTelemetry> tel;
        SafetySnapshot snap;
        snap.taken = now;
        std::optional<double> max_temp;
        for (auto& b : hw_.boards) {
            tel.push_back(b->telemetry());
            snap.boards_running |= tel.back().running;
            for (const auto& t : tel.back().temps) {
                snap.temperatures_c.push_back(t.celsius);
                max_temp = std::max(max_temp.value_or(t.celsius), t.celsius);
            }
        }
        for (auto& s : hw_.temp_sensors) {
            if (auto c = s->read_celsius()) {
                snap.temperatures_c.push_back(*c);
                max_temp = std::max(max_temp.value_or(*c), *c);
            }
        }
        for (auto& f : hw_.fans) snap.fan_rpm.push_back(f->rpm());
        snap.psu_fault = hw_.psu ? hw_.psu->read().fault : false;

        // Fans
        const double duty = fans_.update(max_temp, dt);
        for (auto& f : hw_.fans) (void)f->set_duty(duty);

        // Soft thermal limit: step every chip down until back under the limit.
        const bool hot = max_temp && *max_temp >= profile_.safety.max_chip_temp_c;
        if (hot) {
            if (!throttling_.exchange(true)) log::warn(kLog, "throttling: ", *max_temp, " C");
            for (auto& b : hw_.boards) {
                if (!b->running()) continue;
                auto& c = b->chain();
                for (unsigned k = 0; k < c.chip_count(); ++k)
                    (void)c.set_chip_frequency(k, std::max(c.traits().freq_min_mhz, c.chip_frequency(k) - 25.0));
            }
        } else if (throttling_.load() && max_temp && *max_temp < profile_.safety.max_chip_temp_c - 5) {
            throttling_.store(false);
            log::info(kLog, "throttling released");
        }

        safety_->publish(std::move(snap));

        // Hashrate EMAs
        {
            std::lock_guard lk(stats_mx_);
            double total_delta = 0;
            for (std::size_t i = 0; i < chain_hashes_.size(); ++i) {
                const double delta = chain_hashes_[i] - chain_hashes_prev_[i];
                chain_hashes_prev_[i] = chain_hashes_[i];
                const double inst = delta / dt / 1e9;
                chain_ema_1m_[i] += (inst - chain_ema_1m_[i]) * std::min(1.0, dt / 60.0);
                total_delta += delta;
            }
            const double inst = total_delta / dt / 1e9;
            ema_1m_ += (inst - ema_1m_) * std::min(1.0, dt / 60.0);
            ema_15m_ += (inst - ema_15m_) * std::min(1.0, dt / 900.0);
            last_telemetry_ = std::move(tel);
            last_fan_rpm_.clear();
            for (auto& f : hw_.fans) last_fan_rpm_.push_back(f->rpm());
            last_max_temp_ = max_temp;
            last_fan_duty_ = duty;
        }

        if (tuner_ && !hot && !throttling_.load() &&
            now - last_tune >= std::chrono::seconds(profile_.tuning.interval_s)) {
            last_tune = now;
            autotune_step(max_temp);
        }
    }
}

void Miner::autotune_step(std::optional<double> max_temp) {
    std::vector<ChipSample> samples;
    {
        std::lock_guard lk(stats_mx_);
        for (std::size_t i = 0; i < hw_.boards.size(); ++i) {
            auto& b = *hw_.boards[i];
            if (!b.running()) continue;
            auto& c = b.chain();
            for (unsigned k = 0; k < chip_counters_[i].size(); ++k) {
                auto& cc = chip_counters_[i][k];
                ChipSample s;
                s.chain = unsigned(i);
                s.chip = k;
                s.freq_mhz = c.chip_frequency(k);
                s.min_mhz = c.traits().freq_min_mhz;
                s.max_mhz = c.traits().freq_max_mhz;
                s.nonces = cc.nonces - cc.nonces_at_tune;
                s.hw_errors = cc.hw_errors - cc.hw_errors_at_tune;
                cc.nonces_at_tune = cc.nonces;
                cc.hw_errors_at_tune = cc.hw_errors;
                samples.push_back(s);
            }
        }
    }
    const auto changes = tuner_->step(samples, max_temp);
    for (const auto& ch : changes) {
        auto& c = hw_.boards[ch.chain]->chain();
        if (Status s = c.set_chip_frequency(ch.chip, ch.new_mhz); !s)
            log::warn(kLog, "tuning chain ", ch.chain, " chip ", ch.chip, ": ", s.message());
    }
    if (!changes.empty()) log::info(kLog, "autotune: ", changes.size(), " chip frequency changes");
}

MinerStats Miner::stats() const {
    MinerStats s;
    s.uptime_s = started_.load() ? std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_).count() : 0;
    s.accepted = accepted_.load();
    s.rejected = rejected_.load();
    s.stale = stale_.load();
    s.hw_errors = hw_errors_.load();
    s.nonces = nonces_.load();
    s.blocks = blocks_.load();
    s.throttling = throttling_.load();
    s.pool_connected = pool_ && pool_->connected();
    s.pool = pool_ ? pool_->describe() : "";
    s.tripped = tripped_.load();

    std::lock_guard lk(stats_mx_);
    s.fan_duty = last_fan_duty_;
    s.trip_reason = trip_reason_;
    s.hashrate_ghs_1m = ema_1m_;
    s.hashrate_ghs_15m = ema_15m_;
    s.best_share = best_share_;
    s.fan_rpm = last_fan_rpm_;
    s.max_temp_c = last_max_temp_;
    for (std::size_t i = 0; i < last_telemetry_.size() && i < hw_.boards.size(); ++i) {
        const auto& t = last_telemetry_[i];
        ChainStats c;
        c.index = t.index;
        c.chip = hw_.boards[i]->chain().traits().name;
        c.running = t.running;
        c.chips = t.chips;
        c.avg_freq_mhz = t.avg_freq_mhz;
        c.max_temp_c = t.max_temp_c;
        c.voltage_v = t.voltage_v;
        c.hashrate_ghs_1m = chain_ema_1m_[i];
        for (const auto& cc : chip_counters_[i]) {
            c.nonces += cc.nonces;
            c.hw_errors += cc.hw_errors;
        }
        if (t.running) s.expected_ghs += hw_.boards[i]->chain().traits().expected_ghs(t.avg_freq_mhz) * t.chips;
        s.chains.push_back(std::move(c));
    }
    return s;
}

} // namespace minerd::control

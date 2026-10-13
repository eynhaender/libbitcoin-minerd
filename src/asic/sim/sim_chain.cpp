#include "minerd/asic/sim/sim_chain.hpp"

#include "minerd/util/log.hpp"
#include "minerd/work/share.hpp"
#include "minerd/work/work_generator.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <random>
#include <thread>

namespace minerd::asic::sim {
namespace {
using namespace std::chrono_literals;

double opt(const ChainContext& c, const std::string& k, double fb) {
    auto it = c.options.find(k);
    if (it == c.options.end()) return fb;
    try { return std::stod(it->second); } catch (...) { return fb; }
}

class SimChain final : public IAsicChain {
public:
    explicit SimChain(ChainContext ctx)
        : ctx_(std::move(ctx)),
          traits_(sim_traits()),
          hw_error_rate_(opt(ctx_, "sim_hw_error_rate", 0.0)),
          max_mhs_(opt(ctx_, "sim_max_mhs", 0.0)),
          temp_base_(opt(ctx_, "sim_temp_base_c", 45.0)),
          rng_(std::random_device{}()) {}

    ~SimChain() override { shutdown(); }

    const ChipTraits& traits() const override { return traits_; }

    Status init(const ChainConfig& cfg) override {
        const unsigned chips = cfg.expected_chips ? cfg.expected_chips : unsigned(opt(ctx_, "sim_chips", 4));
        freq_.assign(chips, traits_.freq_min_mhz);
        version_mask_ = cfg.version_mask;
        chip_difficulty_ = cfg.chip_difficulty;
        stop_.store(false);
        worker_ = std::thread([this] { run(); });
        log::info("sim", "chain ", ctx_.chain_index, ": ", chips, " simulated chips");
        return Status::ok();
    }

    Status ramp_frequency(double mhz) override {
        for (auto& f : freq_) f = std::clamp(mhz, traits_.freq_min_mhz, traits_.freq_max_mhz);
        return Status::ok();
    }
    Status set_chip_frequency(unsigned chip, double mhz) override {
        if (chip >= freq_.size()) return Status::error("chip index out of range");
        freq_[chip] = std::clamp(mhz, traits_.freq_min_mhz, traits_.freq_max_mhz);
        return Status::ok();
    }
    Status set_chip_difficulty(double d) override { chip_difficulty_.store(d); return Status::ok(); }
    Status set_version_mask(std::uint32_t m) override { version_mask_ = m; return Status::ok(); }

    void shutdown() override {
        if (stop_.exchange(true)) return;
        { std::lock_guard lk(mx_); }  // order the flag before waiters re-check
        cv_.notify_all();
        results_cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    unsigned chip_count() const override { return unsigned(freq_.size()); }
    double effective_chip_difficulty() const override { return chip_difficulty_.load(); }
    double chip_frequency(unsigned chip) const override { return chip < freq_.size() ? freq_[chip] : 0; }

    Status send_work(const work::WorkItem& item) override {
        if (stop_.load()) return Status::error("chain shut down");
        {
            std::lock_guard lk(mx_);
            current_ = std::make_shared<work::WorkItem>(item);
            ++work_seq_;
        }
        cv_.notify_all();
        return Status::ok();
    }

    std::optional<work::NonceResult> poll_result(std::chrono::milliseconds timeout) override {
        std::unique_lock lk(mx_);
        if (!results_cv_.wait_for(lk, timeout, [&] { return !results_.empty() || stop_.load(); })) return std::nullopt;
        if (results_.empty()) return std::nullopt;
        auto r = results_.front();
        results_.pop_front();
        return r;
    }

    std::chrono::microseconds work_interval() const override { return 500ms; }
    unsigned max_outstanding_work() const override { return 16; }

    std::vector<TemperatureReading> read_temperatures() override {
        double avg = 0;
        for (double f : freq_) avg += f;
        avg = freq_.empty() ? 0 : avg / double(freq_.size());
        // Toy thermal model: hotter with frequency.
        return {{"chain" + std::to_string(ctx_.chain_index) + "/sim", temp_base_ + avg / 40.0}};
    }

private:
    void run() {
        std::uint64_t seen_seq = 0;
        std::shared_ptr<work::WorkItem> w;
        std::uint32_t nonce = 0, version_n = 0;
        auto window_start = std::chrono::steady_clock::now();
        std::uint64_t window_hashes = 0;

        while (!stop_.load()) {
            {
                std::unique_lock lk(mx_);
                cv_.wait(lk, [&] { return stop_.load() || current_ != nullptr; });
                if (stop_.load()) break;
                if (work_seq_ != seen_seq) {
                    seen_seq = work_seq_;
                    w = current_;
                    nonce = std::uniform_int_distribution<std::uint32_t>()(rng_);
                    version_n = 0;
                }
            }
            const std::uint32_t mask = w->version_mask;
            const std::uint32_t version = work::rolled_version(w->base_version(), mask, version_n);
            auto hdr = w->header;
            work::header::set_version(hdr, version);
            const auto mid = crypto::header_midstate(hdr.data());
            const double diff = chip_difficulty_.load();

            constexpr int kBatch = 4096;
            for (int i = 0; i < kBatch; ++i, ++nonce) {
                work::header::set_nonce(hdr, nonce);
                const auto h = crypto::sha256d_header_from_midstate(mid, hdr.data());
                if (h[31] == 0 && h[30] == 0 && work::hash_difficulty(h) >= diff) emit(*w, nonce, version);
            }
            if (nonce < kBatch) ++version_n;  // wrapped -> roll version
            window_hashes += kBatch;

            if (max_mhs_ > 0) {
                const auto elapsed = std::chrono::steady_clock::now() - window_start;
                const auto budget = std::chrono::duration<double>(double(window_hashes) / (max_mhs_ * 1e6));
                if (budget > elapsed) std::this_thread::sleep_for(budget - elapsed);
                if (elapsed > 1s) { window_start = std::chrono::steady_clock::now(); window_hashes = 0; }
            }
        }
    }

    void emit(const work::WorkItem& w, std::uint32_t nonce, std::uint32_t version) {
        work::NonceResult r;
        r.work_id = w.id;
        r.nonce = nonce;
        r.version = version;
        r.chain = ctx_.chain_index;
        r.chip = freq_.empty() ? 0 : unsigned(nonce % freq_.size());
        if (hw_error_rate_ > 0 && std::uniform_real_distribution<double>(0, 1)(rng_) < hw_error_rate_)
            r.nonce ^= 0x00010000;  // corrupted result, must be caught by host verification
        {
            std::lock_guard lk(mx_);
            if (results_.size() < 1024) results_.push_back(r);
        }
        results_cv_.notify_one();
    }

    ChainContext ctx_;
    ChipTraits traits_;
    double hw_error_rate_;
    double max_mhs_;
    double temp_base_;
    std::mt19937 rng_;

    std::vector<double> freq_;
    std::uint32_t version_mask_ = 0;
    std::atomic<double> chip_difficulty_{1.0};

    std::mutex mx_;
    std::condition_variable cv_;
    std::condition_variable results_cv_;
    std::shared_ptr<work::WorkItem> current_;
    std::uint64_t work_seq_ = 0;
    std::deque<work::NonceResult> results_;
    std::atomic<bool> stop_{false};
    std::thread worker_;
};
} // namespace

ChipTraits sim_traits() {
    ChipTraits t;
    t.name = "SIM";
    t.chip_id = 0xFFFF;
    t.hashes_per_clock = 0.001;  // ~0.4 MH/s per chip at 400 MHz, matches sim_max_mhs-capped CPU hashing
    t.work_format = WorkFormat::FullHeader;
    t.version_rolling = true;
    t.freq_min_mhz = 50;
    t.freq_max_mhz = 800;
    t.freq_default_mhz = 400;
    t.chain_voltage_min_v = 0;
    t.chain_voltage_max_v = 20;
    t.verified = true;
    return t;
}

std::unique_ptr<IAsicChain> make_sim_chain(ChainContext ctx) { return std::make_unique<SimChain>(std::move(ctx)); }

} // namespace minerd::asic::sim

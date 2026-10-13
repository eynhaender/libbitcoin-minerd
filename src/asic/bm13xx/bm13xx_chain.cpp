// Generic BM13xx chain driver. Chip-specific details come from Bm13xxVariant.

#include "minerd/asic/bm13xx/variants.hpp"
#include "minerd/util/log.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>

namespace minerd::asic::bm13xx {
namespace {

using namespace std::chrono_literals;
constexpr std::string_view kLog = "bm13xx";

class Bm13xxChain final : public IAsicChain {
public:
    Bm13xxChain(const Bm13xxVariant& v, ChainContext ctx)
        : v_(v), ctx_(std::move(ctx)), framer_(v.response_len) {}

    const ChipTraits& traits() const override { return v_.traits; }

    Status init(const ChainConfig& cfg) override {
        if (!ctx_.serial) return Status::error("BM13xx chain needs a serial port");
        cfg_ = cfg;
        shutdown_.store(false);
        if (!v_.traits.verified)
            log::warn(kLog, "chain ", ctx_.chain_index, ": ", v_.traits.name,
                      " variant is UNVERIFIED on multi-chip hardware – see docs/PORTING.md");

        MINERD_TRY(ctx_.serial->set_baud(cfg.baud));
        if (ctx_.reset) {
            MINERD_TRY(ctx_.reset->set(false));
            std::this_thread::sleep_for(100ms);
            MINERD_TRY(ctx_.reset->set(true));
            std::this_thread::sleep_for(500ms);
        }
        ctx_.serial->flush_input();

        // Version mask is written before enumeration on BM1366+ (as stock firmware does).
        const std::uint32_t vmask = v_.version_mask_value(cfg.version_mask);
        for (unsigned i = 0; i < v_.version_mask_writes_before_enum; ++i)
            MINERD_TRY(tx(cmd_write_register_all(v_.reg_version_mask, vmask)));

        MINERD_TRY(enumerate());
        for (unsigned i = 0; i < v_.version_mask_writes_after_enum; ++i)
            MINERD_TRY(tx(cmd_write_register_all(v_.reg_version_mask, vmask)));
        if (cfg.expected_chips && chips_.size() != cfg.expected_chips) {
            return Status::error("chain " + std::to_string(ctx_.chain_index) + ": found " + std::to_string(chips_.size()) +
                                 " chips, expected " + std::to_string(cfg.expected_chips));
        }

        // Address assignment
        MINERD_TRY(tx(cmd_chain_inactive()));
        address_interval_ = std::max(1u, 256u / unsigned(chips_.size()));
        for (std::size_t i = 0; i < chips_.size(); ++i) {
            chips_[i].address = std::uint8_t(i * address_interval_);
            MINERD_TRY(tx(cmd_set_address(chips_[i].address)));
        }

        // Variant init sequence
        for (const auto& w : v_.init_registers) {
            if (w.per_chip) {
                for (const auto& c : chips_) MINERD_TRY(tx(cmd_write_register(c.address, w.reg, w.value)));
            } else {
                MINERD_TRY(tx(cmd_write_register_all(w.reg, w.value)));
            }
        }

        MINERD_TRY(set_chip_difficulty(cfg.chip_difficulty));
        MINERD_TRY(set_all_frequency(v_.traits.freq_min_mhz));
        MINERD_TRY(write_hash_counting(cfg.frequency_mhz));

        if (cfg.fast_baud && cfg.fast_baud != cfg.baud) {
            if (auto w = v_.fast_uart(cfg.fast_baud)) {
                MINERD_TRY(tx(cmd_write_register_all(w->reg, w->value)));
                std::this_thread::sleep_for(10ms);
                MINERD_TRY(ctx_.serial->set_baud(cfg.fast_baud));
            } else {
                log::warn(kLog, "fast UART for ", v_.traits.name, " unknown, staying at ", cfg.baud, " baud");
            }
        }
        log::info(kLog, "chain ", ctx_.chain_index, ": ", chips_.size(), " x ", v_.traits.name,
                  " initialised, address interval ", address_interval_);
        return Status::ok();
    }

    Status ramp_frequency(double target) override {
        target = std::clamp(target, v_.traits.freq_min_mhz, v_.traits.freq_max_mhz);
        double f = chip_frequency(0);
        if (f <= 0) f = v_.traits.freq_min_mhz;
        const double step = v_.traits.freq_step_mhz;
        while (std::fabs(f - target) > 1e-6) {
            if (shutdown_.load()) return Status::error("shutdown during ramp");
            f = (f < target) ? std::min(target, f + step) : std::max(target, f - step);
            MINERD_TRY(set_all_frequency(f));
            std::this_thread::sleep_for(20ms);
        }
        return write_hash_counting(target);
    }

    Status set_chip_frequency(unsigned chip, double mhz) override {
        if (chip >= chips_.size()) return Status::error("chip index out of range");
        auto pll = solve_pll(std::clamp(mhz, v_.traits.freq_min_mhz, v_.traits.freq_max_mhz), v_.pll);
        if (!pll) return Status::error("no PLL setting for " + std::to_string(mhz) + " MHz");
        MINERD_TRY(tx(cmd_write_register(chips_[chip].address, v_.reg_pll0, pll_register_value(*pll, v_.pll))));
        std::lock_guard lk(state_mx_);
        chips_[chip].freq_mhz = pll->freq_mhz;
        return Status::ok();
    }

    Status set_chip_difficulty(double d) override {
        MINERD_TRY(tx(cmd_write_register_all(v_.reg_ticket_mask, ticket_mask_value(d))));
        chip_difficulty_.store(effective_ticket_difficulty(d));
        return Status::ok();
    }

    Status set_version_mask(std::uint32_t mask) override {
        cfg_.version_mask = mask;
        return tx(cmd_write_register_all(v_.reg_version_mask, v_.version_mask_value(mask)));
    }

    void shutdown() override {
        if (shutdown_.exchange(true)) return;
        // Best effort: drop clocks to the minimum. The real power cut is done by
        // the board's voltage regulator / PSU, never rely on the chips alone.
        if (auto pll = solve_pll(v_.traits.freq_min_mhz, v_.pll))
            (void)tx_unchecked(cmd_write_register_all(v_.reg_pll0, pll_register_value(*pll, v_.pll)));
    }

    unsigned chip_count() const override { return unsigned(chips_.size()); }
    double chip_frequency(unsigned chip) const override {
        std::lock_guard lk(state_mx_);
        return chip < chips_.size() ? chips_[chip].freq_mhz : 0;
    }
    double effective_chip_difficulty() const override { return chip_difficulty_.load(); }

    Status send_work(const work::WorkItem& w) override {
        if (shutdown_.load()) return Status::error("chain shut down");
        const unsigned slots = std::min<unsigned>(v_.job_id_count, unsigned(jobs_.size()));
        const unsigned slot = next_slot_.fetch_add(1) % slots;
        const std::uint8_t chip_job_id = std::uint8_t(slot * v_.job_id_step);
        {
            std::lock_guard lk(jobs_mx_);
            jobs_[slot] = JobSlot{w.id, w.base_version(), w.version_mask, true};
        }
        return tx(frame_job(v_.encode_job(w, chip_job_id)));
    }

    std::optional<work::NonceResult> poll_result(std::chrono::milliseconds timeout) override {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::lock_guard rx_lock(rx_mx_);
        while (true) {
            while (auto frame = framer_.next()) {
                if (v_.check_response_crc && !response_crc_valid(*frame)) { ++crc_errors_; continue; }
                auto r = v_.decode_response(*frame);
                if (!r) { ++decode_errors_; continue; }
                if (r->kind != DecodedResponse::Kind::Nonce) continue;  // late register reply
                const unsigned slot = r->chip_job_id / v_.job_id_step;
                JobSlot js;
                {
                    std::lock_guard lk(jobs_mx_);
                    if (slot >= jobs_.size() || !jobs_[slot].valid) { ++decode_errors_; continue; }
                    js = jobs_[slot];
                }
                work::NonceResult out;
                out.work_id = js.work_id;
                out.nonce = r->nonce;
                out.version = js.base_version | (r->version_bits & js.version_mask);
                out.chain = ctx_.chain_index;
                out.chip = std::min<unsigned>(r->chip_address / address_interval_, chip_count() ? chip_count() - 1 : 0);
                return out;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline || shutdown_.load()) return std::nullopt;
            std::array<std::uint8_t, 256> buf{};
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            const std::size_t n = ctx_.serial->read(buf, std::max(left, 1ms));
            if (n) framer_.feed(std::span<const std::uint8_t>(buf.data(), n));
        }
    }

    std::chrono::microseconds work_interval() const override {
        // Time for the whole chain to sweep a 2^32 nonce range, scaled by the variant.
        double f = chip_frequency(0);
        if (f <= 0) f = v_.traits.freq_min_mhz;
        const double chain_hs = v_.traits.hashes_per_clock * f * 1e6 * std::max<std::size_t>(1, chips_.size());
        const double us = v_.work_interval_factor * 4294967296.0 / chain_hs * 1e6;
        return std::chrono::microseconds(std::int64_t(std::clamp(us, 2000.0, 2'000'000.0)));
    }

    unsigned max_outstanding_work() const override { return v_.job_id_count; }

private:
    struct ChipState {
        std::uint8_t address = 0;
        double freq_mhz = 0;
    };
    struct JobSlot {
        std::uint64_t work_id = 0;
        std::uint32_t base_version = 0;
        std::uint32_t version_mask = 0;
        bool valid = false;
    };

    Status tx(const Bytes& frame) {
        std::lock_guard lk(tx_mx_);
        return ctx_.serial->write(frame);
    }
    Status tx_unchecked(const Bytes& frame) { return tx(frame); }

    Status enumerate() {
        chips_.clear();
        MINERD_TRY(tx(cmd_read_register_all(v_.reg_chip_id)));
        // Collect replies until the line is quiet for 200 ms.
        std::lock_guard rx_lock(rx_mx_);
        auto quiet_until = std::chrono::steady_clock::now() + 500ms;
        std::array<std::uint8_t, 256> buf{};
        while (std::chrono::steady_clock::now() < quiet_until) {
            const std::size_t n = ctx_.serial->read(buf, 50ms);
            if (n) {
                framer_.feed(std::span<const std::uint8_t>(buf.data(), n));
                quiet_until = std::chrono::steady_clock::now() + 200ms;
            }
            while (auto frame = framer_.next()) {
                if (v_.check_response_crc && !response_crc_valid(*frame)) { ++crc_errors_; continue; }
                auto r = v_.decode_response(*frame);
                if (r && r->kind == DecodedResponse::Kind::Register && (r->value >> 16) == v_.traits.chip_id)
                    chips_.push_back(ChipState{});
                else if (r && r->kind == DecodedResponse::Kind::Register)
                    log::warn(kLog, "unexpected chip id 0x", to_hex(std::array<std::uint8_t, 2>{std::uint8_t(r->value >> 24), std::uint8_t(r->value >> 16)}));
            }
        }
        if (chips_.empty()) return Status::error("chain " + std::to_string(ctx_.chain_index) + ": no " + v_.traits.name + " chips answered");
        return Status::ok();
    }

    Status write_hash_counting(double freq_mhz) {
        return tx(cmd_write_register_all(v_.reg_hash_counting,
                                         v_.hash_counting_value(unsigned(chips_.size()), freq_mhz)));
    }

    Status set_all_frequency(double mhz) {
        auto pll = solve_pll(mhz, v_.pll);
        if (!pll) return Status::error("no PLL setting for " + std::to_string(mhz) + " MHz");
        MINERD_TRY(tx(cmd_write_register_all(v_.reg_pll0, pll_register_value(*pll, v_.pll))));
        std::lock_guard lk(state_mx_);
        for (auto& c : chips_) c.freq_mhz = pll->freq_mhz;
        return Status::ok();
    }

    const Bm13xxVariant& v_;
    ChainContext ctx_;
    ChainConfig cfg_;
    std::vector<ChipState> chips_;
    unsigned address_interval_ = 1;
    std::atomic<double> chip_difficulty_{1};
    mutable std::mutex state_mx_;   ///< guards chips_[].freq_mhz

    std::mutex tx_mx_;
    std::mutex rx_mx_;
    ResponseFramer framer_;
    std::uint64_t decode_errors_ = 0;
    std::uint64_t crc_errors_ = 0;

    std::mutex jobs_mx_;
    std::array<JobSlot, 128> jobs_{};
    std::atomic<unsigned> next_slot_{0};
    std::atomic<bool> shutdown_{false};
};

} // namespace

std::unique_ptr<IAsicChain> make_chain(const Bm13xxVariant& variant, ChainContext ctx) {
    return std::make_unique<Bm13xxChain>(variant, std::move(ctx));
}

} // namespace minerd::asic::bm13xx

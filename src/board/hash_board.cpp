#include "minerd/board/hash_board.hpp"

#include "minerd/util/log.hpp"

#include <algorithm>
#include <thread>

namespace minerd::board {

HashBoard::HashBoard(ChainProfile profile, std::unique_ptr<asic::IAsicChain> chain,
                     std::unique_ptr<power::IVoltageRegulator> regulator)
    : profile_(std::move(profile)), chain_(std::move(chain)), regulator_(std::move(regulator)) {}

Status HashBoard::power_up(std::uint32_t version_mask) {
    std::lock_guard lk(power_mx_);
    const auto& t = chain_->traits();
    const std::string tag = "chain" + std::to_string(profile_.index);

    if (profile_.voltage_v > 0 && t.chain_voltage_max_v > 0 &&
        (profile_.voltage_v < t.chain_voltage_min_v || profile_.voltage_v > t.chain_voltage_max_v)) {
        return Status::error(tag + ": voltage " + std::to_string(profile_.voltage_v) + " V outside " + t.name +
                             " window [" + std::to_string(t.chain_voltage_min_v) + ", " +
                             std::to_string(t.chain_voltage_max_v) + "]");
    }

    log::info(tag, "enabling ", regulator_->name(), " at ", profile_.voltage_v, " V");
    MINERD_TRY(regulator_->enable(profile_.voltage_v));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));  // let the rail settle

    asic::ChainConfig cfg;
    cfg.expected_chips = profile_.expected_chips;
    cfg.frequency_mhz = profile_.frequency_mhz > 0 ? profile_.frequency_mhz : t.freq_default_mhz;
    cfg.version_mask = version_mask;
    cfg.chip_difficulty = profile_.chip_difficulty;
    cfg.baud = profile_.baud;
    cfg.fast_baud = profile_.fast_baud;

    if (Status s = chain_->init(cfg); !s) {
        regulator_->power_off();
        return s;
    }
    if (Status s = chain_->ramp_frequency(cfg.frequency_mhz); !s) {
        chain_->shutdown();
        regulator_->power_off();
        return s;
    }
    running_.store(true);
    log::info(tag, chain_->chip_count(), " chips at ", cfg.frequency_mhz, " MHz, expected ~",
              t.expected_ghs(cfg.frequency_mhz) * chain_->chip_count() / 1000.0, " TH/s");
    return Status::ok();
}

void HashBoard::power_down() {
    // No lock on the hot path of an emergency: shutdown()/power_off() are
    // idempotent and thread-safe by contract. Cut chips first, then power.
    running_.store(false);
    chain_->shutdown();
    regulator_->power_off();
}

BoardTelemetry HashBoard::telemetry() {
    BoardTelemetry t;
    t.index = profile_.index;
    t.running = running_.load();
    t.chips = chain_->chip_count();
    double sum = 0;
    for (unsigned i = 0; i < t.chips; ++i) sum += chain_->chip_frequency(i);
    t.avg_freq_mhz = t.chips ? sum / t.chips : 0;
    t.voltage_v = regulator_->read_voltage();
    if (t.running) t.temps = chain_->read_temperatures();
    for (const auto& r : t.temps) t.max_temp_c = std::max(t.max_temp_c.value_or(r.celsius), r.celsius);
    return t;
}

} // namespace minerd::board

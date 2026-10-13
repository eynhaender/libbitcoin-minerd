#pragma once

#include "minerd/asic/chip.hpp"
#include "minerd/board/profile.hpp"
#include "minerd/power/power.hpp"

#include <atomic>
#include <memory>
#include <mutex>

namespace minerd::board {

struct BoardTelemetry {
    unsigned index = 0;
    bool running = false;
    unsigned chips = 0;
    double avg_freq_mhz = 0;
    std::optional<double> max_temp_c;
    std::optional<double> voltage_v;
    std::vector<asic::TemperatureReading> temps;
};

/// One hashboard = one chain driver + its power regulator. Owns the power-up
/// order (voltage -> chain init -> frequency ramp) and the reverse on shutdown.
class HashBoard {
public:
    HashBoard(ChainProfile profile, std::unique_ptr<asic::IAsicChain> chain,
              std::unique_ptr<power::IVoltageRegulator> regulator);

    Status power_up(std::uint32_t version_mask);
    /// Idempotent, thread-safe; safe to call from the safety supervisor.
    void power_down();

    asic::IAsicChain& chain() { return *chain_; }
    const asic::IAsicChain& chain() const { return *chain_; }
    const ChainProfile& profile() const { return profile_; }
    bool running() const { return running_.load(); }
    BoardTelemetry telemetry();

private:
    ChainProfile profile_;
    std::unique_ptr<asic::IAsicChain> chain_;
    std::unique_ptr<power::IVoltageRegulator> regulator_;
    std::atomic<bool> running_{false};
    std::mutex power_mx_;
};

} // namespace minerd::board

#pragma once
// Per-chip frequency tuning. Strategies are pluggable and chip-agnostic: they
// see counters and limits, never registers. The miner applies their decisions
// through IAsicChain::set_chip_frequency().

#include "minerd/board/profile.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace minerd::control {

struct ChipSample {
    unsigned chain = 0;
    unsigned chip = 0;
    double freq_mhz = 0;
    double min_mhz = 0;
    double max_mhz = 0;
    std::uint64_t nonces = 0;      ///< since previous step
    std::uint64_t hw_errors = 0;   ///< since previous step
};

struct FrequencyChange {
    unsigned chain = 0;
    unsigned chip = 0;
    double new_mhz = 0;
};

class ITuningStrategy {
public:
    virtual ~ITuningStrategy() = default;
    virtual std::vector<FrequencyChange> step(const std::vector<ChipSample>& samples,
                                              std::optional<double> max_temp_c) = 0;
};

/// Baseline strategy: lower chips whose HW-error rate is above the limit,
/// raise stable chips one step while there is thermal headroom.
/// (Efficiency-optimal tuning – J/TH with voltage per domain – builds on this
/// once power read-back from the PSU is available.)
class HwErrorStepTuner final : public ITuningStrategy {
public:
    HwErrorStepTuner(board::TuningProfile tuning, board::SafetyLimits limits, std::uint64_t min_samples = 20)
        : t_(tuning), l_(limits), min_samples_(min_samples) {}

    std::vector<FrequencyChange> step(const std::vector<ChipSample>& samples, std::optional<double> max_temp_c) override;

private:
    board::TuningProfile t_;
    board::SafetyLimits l_;
    std::uint64_t min_samples_;
};

} // namespace minerd::control

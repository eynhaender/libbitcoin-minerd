#include "minerd/control/autotuner.hpp"

#include <algorithm>

namespace minerd::control {

std::vector<FrequencyChange> HwErrorStepTuner::step(const std::vector<ChipSample>& samples,
                                                    std::optional<double> max_temp_c) {
    std::vector<FrequencyChange> out;
    const bool thermal_headroom = max_temp_c && *max_temp_c < (l_.max_chip_temp_c - t_.min_temp_margin_c);

    for (const auto& s : samples) {
        const std::uint64_t total = s.nonces + s.hw_errors;
        const double ceiling = (t_.max_mhz > 0) ? std::min(t_.max_mhz, s.max_mhz) : s.max_mhz;

        if (total == 0) {
            // A chip that returns nothing at all is dead or badly overclocked: back off.
            if (s.freq_mhz - t_.step_mhz >= s.min_mhz)
                out.push_back({s.chain, s.chip, s.freq_mhz - t_.step_mhz});
            continue;
        }
        if (total < min_samples_) continue;  // not enough evidence yet

        const double rate = double(s.hw_errors) / double(total);
        if (rate > t_.max_hw_error_rate) {
            out.push_back({s.chain, s.chip, std::max(s.min_mhz, s.freq_mhz - t_.step_mhz)});
        } else if (rate < t_.max_hw_error_rate / 4 && thermal_headroom && s.freq_mhz + t_.step_mhz <= ceiling) {
            out.push_back({s.chain, s.chip, s.freq_mhz + t_.step_mhz});
        }
    }
    return out;
}

} // namespace minerd::control

#include "minerd/control/thermal.hpp"

#include <algorithm>

namespace minerd::control {

double FanController::update(std::optional<double> t, double dt) {
    if (!t) {
        duty_ = p_.max_fan_percent;
        return duty_;
    }
    const double error = *t - p_.target_temp_c;  // positive = too hot
    const double candidate_integral = integral_ + error * dt;
    double out = p_.min_fan_percent + p_.kp * error + p_.ki * candidate_integral;
    // Anti-windup: only integrate while not saturated in the same direction.
    const bool sat_hi = out > p_.max_fan_percent && error > 0;
    const bool sat_lo = out < p_.min_fan_percent && error < 0;
    if (!sat_hi && !sat_lo) integral_ = candidate_integral;
    out = p_.min_fan_percent + p_.kp * error + p_.ki * integral_;
    duty_ = std::clamp(out, p_.min_fan_percent, p_.max_fan_percent);
    return duty_;
}

} // namespace minerd::control

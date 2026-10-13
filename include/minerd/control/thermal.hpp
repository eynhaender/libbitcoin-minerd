#pragma once

#include "minerd/board/profile.hpp"

#include <optional>

namespace minerd::control {

/// PI fan controller on the hottest temperature reading. Missing temperature
/// data always yields maximum fan speed (fail safe).
class FanController {
public:
    explicit FanController(board::ThermalProfile p) : p_(p) {}
    double update(std::optional<double> max_temp_c, double dt_s);
    double duty() const { return duty_; }

private:
    board::ThermalProfile p_;
    double integral_ = 0;
    double duty_ = 100;
};

} // namespace minerd::control

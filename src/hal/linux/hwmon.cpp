#include "minerd/hal/linux_hal.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace minerd::hal::linux_hal {
namespace {
class HwmonFan final : public IFan {
public:
    HwmonFan(std::string pwm, std::string rpm, unsigned pwm_max) : pwm_(std::move(pwm)), rpm_(std::move(rpm)), max_(pwm_max) {}

    Status set_duty(double percent) override {
        const unsigned v = unsigned(std::lround(std::clamp(percent, 0.0, 100.0) / 100.0 * max_));
        std::ofstream f(pwm_);
        if (!(f << v)) return Status::error(pwm_ + ": write failed");
        return Status::ok();
    }
    std::optional<unsigned> rpm() override {
        if (rpm_.empty()) return std::nullopt;
        std::ifstream f(rpm_);
        unsigned v = 0;
        if (!(f >> v)) return std::nullopt;
        return v;
    }
    std::string describe() const override { return pwm_; }

private:
    std::string pwm_, rpm_;
    unsigned max_;
};

class MilliSensor final : public ITempSensor {
public:
    MilliSensor(std::string path, std::string name) : path_(std::move(path)), name_(std::move(name)) {}
    std::optional<double> read_celsius() override {
        std::ifstream f(path_);
        long v = 0;
        if (!(f >> v)) return std::nullopt;
        return double(v) / 1000.0;
    }
    std::string describe() const override { return name_; }

private:
    std::string path_, name_;
};
} // namespace

std::unique_ptr<IFan> open_hwmon_fan(const std::string& pwm, const std::string& rpm, unsigned pwm_max, Status& status) {
    if (!std::ofstream(pwm, std::ios::app)) {
        status = Status::error(pwm + ": not writable");
        return nullptr;
    }
    status = Status::ok();
    return std::make_unique<HwmonFan>(pwm, rpm, pwm_max);
}

std::unique_ptr<ITempSensor> open_millidegree_sensor(const std::string& path, std::string name, Status& status) {
    if (!std::ifstream(path)) {
        status = Status::error(path + ": not readable");
        return nullptr;
    }
    status = Status::ok();
    return std::make_unique<MilliSensor>(path, std::move(name));
}

} // namespace minerd::hal::linux_hal

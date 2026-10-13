#include "minerd/hal/linux_hal.hpp"

#include <fstream>
#include <thread>

namespace minerd::hal::linux_hal {
namespace {
bool write_file(const std::string& path, const std::string& value) {
    std::ofstream f(path);
    if (!f) return false;
    f << value;
    return bool(f);
}

class SysfsGpio final : public IGpioLine {
public:
    SysfsGpio(int gpio, bool active_low) : base_("/sys/class/gpio/gpio" + std::to_string(gpio)), active_low_(active_low) {}

    Status set(bool high) override {
        const bool level = active_low_ ? !high : high;
        if (!write_file(base_ + "/value", level ? "1" : "0")) return Status::error(base_ + "/value: write failed");
        return Status::ok();
    }
    std::optional<bool> get() override {
        std::ifstream f(base_ + "/value");
        int v = -1;
        if (!(f >> v)) return std::nullopt;
        return active_low_ ? v == 0 : v != 0;
    }

private:
    std::string base_;
    bool active_low_;
};
} // namespace

std::unique_ptr<IGpioLine> open_gpio_sysfs(int gpio, bool output, bool active_low, Status& status) {
    const std::string base = "/sys/class/gpio/gpio" + std::to_string(gpio);
    if (!std::ifstream(base + "/value")) {
        write_file("/sys/class/gpio/export", std::to_string(gpio));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));  // udev needs a moment
    }
    if (!write_file(base + "/direction", output ? "out" : "in")) {
        status = Status::error("gpio " + std::to_string(gpio) + ": cannot set direction");
        return nullptr;
    }
    status = Status::ok();
    return std::make_unique<SysfsGpio>(gpio, active_low);
}

} // namespace minerd::hal::linux_hal

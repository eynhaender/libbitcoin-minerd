#pragma once
// Linux implementations of the HAL interfaces. Device paths come from the
// board profile, so the same code serves BeagleBone, Zynq and Amlogic boards.

#include "minerd/hal/hal.hpp"

#include <memory>
#include <string>

namespace minerd::hal::linux_hal {

/// termios2-based UART supporting arbitrary baud rates (e.g. 3 Mbaud).
std::unique_ptr<ISerialPort> open_serial(const std::string& device, Status& status);

/// /dev/i2c-N via ioctl(I2C_SLAVE / I2C_RDWR).
std::unique_ptr<II2cBus> open_i2c(const std::string& device, Status& status);

/// Legacy sysfs GPIO (/sys/class/gpio). Stock Bitmain kernels still use it.
/// TODO: add a libgpiod/char-device backend for newer kernels.
std::unique_ptr<IGpioLine> open_gpio_sysfs(int gpio, bool output, bool active_low, Status& status);

/// Fan via hwmon-style files: pwm file (0..pwm_max) and optional tach file (rpm).
std::unique_ptr<IFan> open_hwmon_fan(const std::string& pwm_path, const std::string& rpm_path, unsigned pwm_max,
                                     Status& status);

/// Temperature from a file containing millidegrees (hwmon temp*_input, thermal_zone*/temp).
std::unique_ptr<ITempSensor> open_millidegree_sensor(const std::string& path, std::string name, Status& status);

} // namespace minerd::hal::linux_hal

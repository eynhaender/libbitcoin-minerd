#include "minerd/hal/linux_hal.hpp"

#include <mutex>

#ifdef __linux__
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace minerd::hal::linux_hal {

#ifdef __linux__
namespace {
class LinuxI2c final : public II2cBus {
public:
    LinuxI2c(int fd, std::string dev) : fd_(fd), dev_(std::move(dev)) {}
    ~LinuxI2c() override { if (fd_ >= 0) ::close(fd_); }

    Status write(std::uint8_t addr, std::span<const std::uint8_t> data) override {
        i2c_msg msg{addr, 0, std::uint16_t(data.size()), const_cast<std::uint8_t*>(data.data())};
        return transfer(&msg, 1);
    }
    Status read(std::uint8_t addr, std::span<std::uint8_t> out) override {
        i2c_msg msg{addr, I2C_M_RD, std::uint16_t(out.size()), out.data()};
        return transfer(&msg, 1);
    }
    Status write_read(std::uint8_t addr, std::span<const std::uint8_t> wr, std::span<std::uint8_t> rd) override {
        i2c_msg msgs[2] = {{addr, 0, std::uint16_t(wr.size()), const_cast<std::uint8_t*>(wr.data())},
                           {addr, I2C_M_RD, std::uint16_t(rd.size()), rd.data()}};
        return transfer(msgs, 2);
    }

private:
    Status transfer(i2c_msg* msgs, unsigned n) {
        std::lock_guard lk(mx_);  // one bus, shared by PSU and regulators
        i2c_rdwr_ioctl_data d{msgs, n};
        if (::ioctl(fd_, I2C_RDWR, &d) < 0) return Status::error(dev_ + ": I2C_RDWR: " + std::strerror(errno));
        return Status::ok();
    }
    int fd_;
    std::string dev_;
    std::mutex mx_;
};
} // namespace

std::unique_ptr<II2cBus> open_i2c(const std::string& device, Status& status) {
    const int fd = ::open(device.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        status = Status::error(device + ": open: " + std::strerror(errno));
        return nullptr;
    }
    status = Status::ok();
    return std::make_unique<LinuxI2c>(fd, device);
}
#else
std::unique_ptr<II2cBus> open_i2c(const std::string& device, Status& status) {
    status = Status::error("I2C is only supported on Linux: " + device);
    return nullptr;
}
#endif

} // namespace minerd::hal::linux_hal

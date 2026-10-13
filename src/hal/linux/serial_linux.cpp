#include "minerd/hal/linux_hal.hpp"

#ifdef __linux__
#include <asm/termbits.h>  // termios2 / BOTHER (do not mix with <termios.h>)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace minerd::hal::linux_hal {

#ifdef __linux__
namespace {
class LinuxSerial final : public ISerialPort {
public:
    LinuxSerial(int fd, std::string dev) : fd_(fd), dev_(std::move(dev)) {}
    ~LinuxSerial() override { if (fd_ >= 0) ::close(fd_); }

    Status set_baud(unsigned baud) override {
        struct termios2 tio {};
        if (::ioctl(fd_, TCGETS2, &tio) != 0) return err("TCGETS2");
        tio.c_cflag &= ~tcflag_t(CBAUD | CSIZE | PARENB | CSTOPB | CRTSCTS);
        tio.c_cflag |= BOTHER | CS8 | CLOCAL | CREAD;
        tio.c_iflag = 0;
        tio.c_oflag = 0;
        tio.c_lflag = 0;
        tio.c_cc[VMIN] = 0;
        tio.c_cc[VTIME] = 0;
        tio.c_ispeed = baud;
        tio.c_ospeed = baud;
        if (::ioctl(fd_, TCSETS2, &tio) != 0) return err("TCSETS2");
        return Status::ok();
    }

    Status write(std::span<const std::uint8_t> data) override {
        std::size_t off = 0;
        while (off < data.size()) {
            const ssize_t n = ::write(fd_, data.data() + off, data.size() - off);
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN) {
                    pollfd p{fd_, POLLOUT, 0};
                    ::poll(&p, 1, 100);
                    continue;
                }
                return err("write");
            }
            off += std::size_t(n);
        }
        return Status::ok();
    }

    std::size_t read(std::span<std::uint8_t> buf, std::chrono::milliseconds timeout) override {
        pollfd p{fd_, POLLIN, 0};
        const int r = ::poll(&p, 1, int(timeout.count()));
        if (r <= 0) return 0;
        const ssize_t n = ::read(fd_, buf.data(), buf.size());
        return n > 0 ? std::size_t(n) : 0;
    }

    void flush_input() override { ::ioctl(fd_, TCFLSH, TCIFLUSH); }
    std::string describe() const override { return dev_; }

private:
    Status err(const char* what) { return Status::error(dev_ + ": " + what + ": " + std::strerror(errno)); }
    int fd_;
    std::string dev_;
};
} // namespace

std::unique_ptr<ISerialPort> open_serial(const std::string& device, Status& status) {
    const int fd = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        status = Status::error(device + ": open: " + std::strerror(errno));
        return nullptr;
    }
    status = Status::ok();
    return std::make_unique<LinuxSerial>(fd, device);
}
#else
std::unique_ptr<ISerialPort> open_serial(const std::string& device, Status& status) {
    status = Status::error("serial ports are only supported on Linux: " + device);
    return nullptr;
}
#endif

} // namespace minerd::hal::linux_hal

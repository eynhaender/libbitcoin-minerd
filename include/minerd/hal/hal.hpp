#pragma once
// Hardware abstraction layer: the only place that touches /dev, sysfs or
// memory-mapped registers. Everything above it (chip drivers, boards, control
// loop) talks to these interfaces, which is what makes the simulator and new
// control boards possible.

#include "minerd/util/bytes.hpp"
#include "minerd/util/status.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace minerd::hal {

/// Byte stream to an ASIC chain (UART on Bitmain boards; could be SPI or USB
/// on other hardware). Implementations must allow write() and read() to be
/// called concurrently from two threads (TX dispatcher / RX poller).
class ISerialPort {
public:
    virtual ~ISerialPort() = default;
    virtual Status set_baud(unsigned baud) = 0;
    virtual Status write(std::span<const std::uint8_t> data) = 0;
    /// Reads up to buf.size() bytes; returns 0 on timeout.
    virtual std::size_t read(std::span<std::uint8_t> buf, std::chrono::milliseconds timeout) = 0;
    virtual void flush_input() = 0;
    virtual std::string describe() const = 0;
};

class II2cBus {
public:
    virtual ~II2cBus() = default;
    virtual Status write(std::uint8_t addr, std::span<const std::uint8_t> data) = 0;
    virtual Status read(std::uint8_t addr, std::span<std::uint8_t> out) = 0;
    virtual Status write_read(std::uint8_t addr, std::span<const std::uint8_t> wr, std::span<std::uint8_t> rd) = 0;
};

class IGpioLine {
public:
    virtual ~IGpioLine() = default;
    virtual Status set(bool high) = 0;
    virtual std::optional<bool> get() = 0;
};

class IFan {
public:
    virtual ~IFan() = default;
    virtual Status set_duty(double percent) = 0;   ///< 0..100
    virtual std::optional<unsigned> rpm() = 0;
    virtual std::string describe() const = 0;
};

class ITempSensor {
public:
    virtual ~ITempSensor() = default;
    virtual std::optional<double> read_celsius() = 0;
    virtual std::string describe() const = 0;
};

} // namespace minerd::hal

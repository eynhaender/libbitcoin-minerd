#pragma once

#include "minerd/hal/hal.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

namespace minerd::hal::sim {

class SimFan final : public IFan {
public:
    explicit SimFan(unsigned index, unsigned max_rpm = 6000) : index_(index), max_rpm_(max_rpm) {}
    Status set_duty(double percent) override;
    std::optional<unsigned> rpm() override;
    std::string describe() const override { return "sim-fan" + std::to_string(index_); }
    double duty() const { return duty_.load(); }
    void set_failed(bool f) { failed_.store(f); }

private:
    unsigned index_;
    unsigned max_rpm_;
    std::atomic<double> duty_{100.0};
    std::atomic<bool> failed_{false};
};

class SimTempSensor final : public ITempSensor {
public:
    explicit SimTempSensor(std::string name, double celsius) : name_(std::move(name)), value_(celsius) {}
    std::optional<double> read_celsius() override;
    std::string describe() const override { return name_; }
    void set(double c) { value_.store(c); }
    void set_failed(bool f) { failed_.store(f); }

private:
    std::string name_;
    std::atomic<double> value_;
    std::atomic<bool> failed_{false};
};

class SimGpio final : public IGpioLine {
public:
    Status set(bool high) override { value_.store(high); return Status::ok(); }
    std::optional<bool> get() override { return value_.load(); }

private:
    std::atomic<bool> value_{false};
};

/// Serial port whose "device side" is a callback. Used to unit-test chain
/// drivers against a scripted chip model.
class ScriptedSerial final : public ISerialPort {
public:
    /// Called with every frame the driver writes; may push response bytes.
    using Responder = std::function<void(std::span<const std::uint8_t> written, ScriptedSerial& port)>;
    explicit ScriptedSerial(Responder r) : responder_(std::move(r)) {}

    Status set_baud(unsigned baud) override { baud_ = baud; return Status::ok(); }
    Status write(std::span<const std::uint8_t> data) override;
    std::size_t read(std::span<std::uint8_t> buf, std::chrono::milliseconds timeout) override;
    void flush_input() override;
    std::string describe() const override { return "scripted-serial"; }

    void push_rx(std::span<const std::uint8_t> bytes);
    unsigned baud() const { return baud_; }
    std::vector<Bytes> written_frames() const;

private:
    Responder responder_;
    unsigned baud_ = 0;
    mutable std::mutex mx_;
    std::condition_variable cv_;
    std::deque<std::uint8_t> rx_;
    std::vector<Bytes> written_;
};

} // namespace minerd::hal::sim

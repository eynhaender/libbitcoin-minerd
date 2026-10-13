#include "minerd/hal/sim_hal.hpp"

#include <algorithm>

namespace minerd::hal::sim {

Status SimFan::set_duty(double p) {
    duty_.store(std::clamp(p, 0.0, 100.0));
    return Status::ok();
}

std::optional<unsigned> SimFan::rpm() {
    if (failed_.load()) return 0u;
    return unsigned(max_rpm_ * duty_.load() / 100.0);
}

std::optional<double> SimTempSensor::read_celsius() {
    if (failed_.load()) return std::nullopt;
    return value_.load();
}

Status ScriptedSerial::write(std::span<const std::uint8_t> data) {
    {
        std::lock_guard lk(mx_);
        written_.emplace_back(data.begin(), data.end());
    }
    if (responder_) responder_(data, *this);
    return Status::ok();
}

std::size_t ScriptedSerial::read(std::span<std::uint8_t> buf, std::chrono::milliseconds timeout) {
    std::unique_lock lk(mx_);
    if (!cv_.wait_for(lk, timeout, [&] { return !rx_.empty(); })) return 0;
    std::size_t n = 0;
    while (n < buf.size() && !rx_.empty()) {
        buf[n++] = rx_.front();
        rx_.pop_front();
    }
    return n;
}

void ScriptedSerial::flush_input() {
    std::lock_guard lk(mx_);
    rx_.clear();
}

void ScriptedSerial::push_rx(std::span<const std::uint8_t> bytes) {
    {
        std::lock_guard lk(mx_);
        rx_.insert(rx_.end(), bytes.begin(), bytes.end());
    }
    cv_.notify_all();
}

std::vector<Bytes> ScriptedSerial::written_frames() const {
    std::lock_guard lk(mx_);
    return written_;
}

} // namespace minerd::hal::sim

#include "minerd/control/safety.hpp"

#include "minerd/util/log.hpp"

#include <sstream>

namespace minerd::control {

using namespace std::chrono_literals;

SafetySupervisor::SafetySupervisor(board::SafetyLimits l, TripHandler h, std::chrono::seconds grace)
    : limits_(l), on_trip_(std::move(h)), grace_(grace) {}

SafetySupervisor::~SafetySupervisor() { stop(); }

void SafetySupervisor::start() {
    std::lock_guard lk(mx_);
    if (!stop_.load()) return;
    stop_.store(false);
    started_ = Clock::now();
    last_heartbeat_ = started_;
    thread_ = std::thread([this] { run(); });
}

void SafetySupervisor::stop() {
    {
        std::lock_guard lk(mx_);
        if (stop_.exchange(true)) return;
    }
    cv_.notify_all();
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
}

void SafetySupervisor::publish(SafetySnapshot s) {
    std::lock_guard lk(mx_);
    snapshot_ = std::move(s);
}

void SafetySupervisor::heartbeat() {
    std::lock_guard lk(mx_);
    last_heartbeat_ = Clock::now();
}

std::string SafetySupervisor::trip_reason() const {
    std::lock_guard lk(mx_);
    return reason_;
}

std::optional<std::string> SafetySupervisor::evaluate(const std::optional<SafetySnapshot>& snap, Clock::time_point hb,
                                                      Clock::time_point started, Clock::time_point now) const {
    using dsec = std::chrono::duration<double>;
    const bool in_grace = (now - started) < grace_;

    if (dsec(now - hb).count() > limits_.heartbeat_timeout_s) return "main loop heartbeat lost";

    if (!snap) {
        if (!in_grace) return "no telemetry received";
        return std::nullopt;
    }
    if (dsec(now - snap->taken).count() > limits_.telemetry_timeout_s) return "telemetry stale (control loop hung?)";

    for (double t : snap->temperatures_c) {
        if (t >= limits_.shutdown_temp_c) {
            std::ostringstream os;
            os << "temperature " << t << " C >= shutdown limit " << limits_.shutdown_temp_c << " C";
            return os.str();
        }
    }
    if (snap->boards_running && limits_.require_temperature && snap->temperatures_c.empty() && !in_grace)
        return "hashboards running without any temperature reading";

    if (!in_grace) {
        for (std::size_t i = 0; i < snap->fan_rpm.size(); ++i) {
            const auto& rpm = snap->fan_rpm[i];
            if (rpm && *rpm < limits_.min_fan_rpm)
                return "fan " + std::to_string(i) + " at " + std::to_string(*rpm) + " rpm < " +
                       std::to_string(limits_.min_fan_rpm);
        }
    }
    if (snap->psu_fault) return "PSU fault";
    return std::nullopt;
}

void SafetySupervisor::run() {
    while (!stop_.load()) {
        std::optional<SafetySnapshot> snap;
        Clock::time_point hb, started;
        {
            std::unique_lock lk(mx_);
            cv_.wait_for(lk, 250ms, [&] { return stop_.load(); });
            if (stop_.load()) break;
            snap = snapshot_;
            hb = last_heartbeat_;
            started = started_;
        }
        if (auto reason = evaluate(snap, hb, started, Clock::now())) {
            trip(*reason);
            break;
        }
    }
}

void SafetySupervisor::trip(const std::string& reason) {
    if (tripped_.exchange(true)) return;
    {
        std::lock_guard lk(mx_);
        reason_ = reason;
    }
    log::fatal("safety", "TRIP: ", reason);
    if (on_trip_) on_trip_(reason);
}

} // namespace minerd::control

#pragma once
// Independent safety supervisor.
//
// Runs on its own thread and trips (cuts hashboard power, fans to 100 %) when
//   * any temperature exceeds shutdown_temp_c
//   * hashboards run but no temperature source answers (require_temperature)
//   * a fan with a tachometer reports < min_fan_rpm after the start-up grace
//   * the PSU reports a fault
//   * the control loop stops publishing telemetry (telemetry_timeout_s)
//   * the main loop stops sending heartbeats (heartbeat_timeout_s)
//
// It never waits on the control loop: a hung control thread is exactly one of
// the conditions it must catch. In production pair it with the SoC hardware
// watchdog (/dev/watchdog) so a hung process also cuts power.

#include "minerd/board/profile.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace minerd::control {

struct SafetySnapshot {
    std::chrono::steady_clock::time_point taken{};
    bool boards_running = false;
    std::vector<double> temperatures_c;
    std::vector<std::optional<unsigned>> fan_rpm;   ///< nullopt = fan without tachometer
    bool psu_fault = false;
};

class SafetySupervisor {
public:
    using Clock = std::chrono::steady_clock;
    using TripHandler = std::function<void(const std::string& reason)>;

    SafetySupervisor(board::SafetyLimits limits, TripHandler on_trip,
                     std::chrono::seconds startup_grace = std::chrono::seconds(15));
    ~SafetySupervisor();

    void start();
    void stop();
    void publish(SafetySnapshot snapshot);
    void heartbeat();

    bool tripped() const { return tripped_.load(); }
    std::string trip_reason() const;

    /// Pure evaluation (unit-testable): returns a trip reason or nullopt.
    std::optional<std::string> evaluate(const std::optional<SafetySnapshot>& snap, Clock::time_point last_heartbeat,
                                        Clock::time_point started, Clock::time_point now) const;

private:
    void run();
    void trip(const std::string& reason);

    board::SafetyLimits limits_;
    TripHandler on_trip_;
    std::chrono::seconds grace_;

    mutable std::mutex mx_;
    std::condition_variable cv_;
    std::optional<SafetySnapshot> snapshot_;
    Clock::time_point last_heartbeat_{};
    Clock::time_point started_{};
    std::string reason_;
    std::atomic<bool> tripped_{false};
    std::atomic<bool> stop_{true};
    std::thread thread_;
};

} // namespace minerd::control

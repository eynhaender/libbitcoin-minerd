#include "check.hpp"
#include "minerd/control/safety.hpp"
#include "minerd/control/thermal.hpp"

#include <atomic>
#include <thread>

using namespace minerd;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

static control::SafetySnapshot healthy(Clock::time_point t) {
    control::SafetySnapshot s;
    s.taken = t;
    s.boards_running = true;
    s.temperatures_c = {60, 65};
    s.fan_rpm = {4000u, 4100u};
    return s;
}

TEST(healthy_snapshot_does_not_trip) {
    control::SafetySupervisor sup({}, nullptr, 0s);
    const auto now = Clock::now();
    CHECK(!sup.evaluate(healthy(now), now, now - 60s, now).has_value());
}

TEST(over_temperature_trips) {
    control::SafetySupervisor sup({}, nullptr, 0s);
    const auto now = Clock::now();
    auto s = healthy(now);
    s.temperatures_c.push_back(96);
    CHECK(sup.evaluate(s, now, now - 60s, now).has_value());
}

TEST(missing_temperature_trips_after_grace) {
    control::SafetySupervisor sup({}, nullptr, 10s);
    const auto now = Clock::now();
    auto s = healthy(now);
    s.temperatures_c.clear();
    CHECK(!sup.evaluate(s, now, now - 5s, now).has_value());   // within grace
    CHECK(sup.evaluate(s, now, now - 30s, now).has_value());   // after grace
}

TEST(stopped_fan_trips) {
    control::SafetySupervisor sup({}, nullptr, 0s);
    const auto now = Clock::now();
    auto s = healthy(now);
    s.fan_rpm[1] = 0u;
    CHECK(sup.evaluate(s, now, now - 60s, now).has_value());
    s.fan_rpm[1] = std::nullopt;  // fan without tachometer: not judged
    CHECK(!sup.evaluate(s, now, now - 60s, now).has_value());
}

TEST(stale_telemetry_and_lost_heartbeat_trip) {
    control::SafetySupervisor sup({}, nullptr, 0s);
    const auto now = Clock::now();
    CHECK(sup.evaluate(healthy(now - 30s), now, now - 60s, now).has_value());
    CHECK(sup.evaluate(healthy(now), now - 30s, now - 60s, now).has_value());
}

TEST(supervisor_thread_calls_trip_handler_once) {
    std::atomic<int> trips{0};
    board::SafetyLimits l;
    l.heartbeat_timeout_s = 0.3;
    control::SafetySupervisor sup(l, [&](const std::string&) { ++trips; }, 0s);
    sup.start();
    sup.publish(healthy(Clock::now()));
    std::this_thread::sleep_for(1200ms);  // no heartbeats
    sup.stop();
    CHECK_EQ(trips.load(), 1);
    CHECK(sup.tripped());
    CHECK(sup.trip_reason().find("heartbeat") != std::string::npos);
}

TEST(fan_controller_is_fail_safe_and_bounded) {
    board::ThermalProfile p;
    control::FanController fc(p);
    CHECK_EQ(fc.update(std::nullopt, 1), p.max_fan_percent);
    double d = 0;
    for (int i = 0; i < 100; ++i) d = fc.update(40.0, 1);
    CHECK_EQ(d, p.min_fan_percent);
    for (int i = 0; i < 100; ++i) d = fc.update(90.0, 1);
    CHECK_EQ(d, p.max_fan_percent);
}

TEST_MAIN()

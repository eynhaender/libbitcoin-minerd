#include "minerd/util/log.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace minerd::log {
namespace {
std::atomic<Level> g_level{Level::info};
std::mutex g_mutex;

const char* name_of(Level l) {
    switch (l) {
    case Level::trace: return "TRACE";
    case Level::debug: return "DEBUG";
    case Level::info:  return "INFO ";
    case Level::warn:  return "WARN ";
    case Level::error: return "ERROR";
    case Level::fatal: return "FATAL";
    }
    return "?";
}
} // namespace

void set_level(Level l) { g_level.store(l); }
Level level() { return g_level.load(std::memory_order_relaxed); }

Level parse_level(std::string_view n, Level fallback) {
    if (n == "trace") return Level::trace;
    if (n == "debug") return Level::debug;
    if (n == "info")  return Level::info;
    if (n == "warn")  return Level::warn;
    if (n == "error") return Level::error;
    if (n == "fatal") return Level::fatal;
    return fallback;
}

void write(Level lvl, std::string_view component, const std::string& message) {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t = system_clock::to_time_t(now);
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm{};
    gmtime_r(&t, &tm);
    char ts[32];
    std::strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%S", &tm);

    std::lock_guard lock(g_mutex);
    std::fprintf(stderr, "%s.%03lldZ %s [%.*s] %s\n", ts, static_cast<long long>(ms), name_of(lvl),
                 static_cast<int>(component.size()), component.data(), message.c_str());
}

} // namespace minerd::log

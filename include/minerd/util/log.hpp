#pragma once
// Minimal thread-safe logger. Replace the sink to forward to syslog/journald.

#include <sstream>
#include <string>
#include <string_view>

namespace minerd::log {

enum class Level { trace = 0, debug, info, warn, error, fatal };

void set_level(Level level);
Level level();
Level parse_level(std::string_view name, Level fallback = Level::info);
void write(Level level, std::string_view component, const std::string& message);

template <typename... Args>
void emit(Level lvl, std::string_view component, Args&&... args) {
    if (lvl < level()) return;
    std::ostringstream os;
    (os << ... << std::forward<Args>(args));
    write(lvl, component, os.str());
}

template <typename... A> void trace(std::string_view c, A&&... a) { emit(Level::trace, c, std::forward<A>(a)...); }
template <typename... A> void debug(std::string_view c, A&&... a) { emit(Level::debug, c, std::forward<A>(a)...); }
template <typename... A> void info (std::string_view c, A&&... a) { emit(Level::info,  c, std::forward<A>(a)...); }
template <typename... A> void warn (std::string_view c, A&&... a) { emit(Level::warn,  c, std::forward<A>(a)...); }
template <typename... A> void error(std::string_view c, A&&... a) { emit(Level::error, c, std::forward<A>(a)...); }
template <typename... A> void fatal(std::string_view c, A&&... a) { emit(Level::fatal, c, std::forward<A>(a)...); }

} // namespace minerd::log

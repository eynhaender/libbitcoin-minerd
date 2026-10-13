#pragma once
// Lightweight error type. Hardware code returns Status instead of throwing so
// that failure paths are explicit and cheap on small control boards.

#include <string>
#include <utility>

namespace minerd {

class [[nodiscard]] Status {
public:
    Status() = default;

    static Status ok() { return {}; }
    static Status error(std::string message) {
        Status s;
        s.ok_ = false;
        s.message_ = std::move(message);
        return s;
    }
    /// Use for drivers whose hardware protocol is not (yet) implemented.
    /// Unknown hardware must fail closed: never "pretend" to have set a voltage.
    static Status not_implemented(std::string what) {
        return error("not implemented: " + std::move(what));
    }

    explicit operator bool() const { return ok_; }
    bool is_ok() const { return ok_; }
    const std::string& message() const { return message_; }

private:
    bool ok_ = true;
    std::string message_;
};

} // namespace minerd

#define MINERD_TRY(expr)                    \
    do {                                    \
        ::minerd::Status _minerd_s = (expr); \
        if (!_minerd_s) return _minerd_s;   \
    } while (0)

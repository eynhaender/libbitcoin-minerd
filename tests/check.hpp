#pragma once
// Minimal test harness (no external dependency).

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace check {
inline int& failures() { static int f = 0; return f; }
inline std::vector<std::pair<std::string, std::function<void()>>>& registry() {
    static std::vector<std::pair<std::string, std::function<void()>>> r;
    return r;
}
struct Reg {
    Reg(const char* n, std::function<void()> f) { registry().emplace_back(n, std::move(f)); }
};
inline int run_all() {
    for (auto& [name, fn] : registry()) {
        const int before = failures();
        fn();
        std::printf("%s %s\n", failures() == before ? "[ OK ]" : "[FAIL]", name.c_str());
    }
    std::printf("%d failure(s)\n", failures());
    return failures() ? 1 : 0;
}
} // namespace check

#define CHECK_CAT2(a, b) a##b
#define CHECK_CAT(a, b) CHECK_CAT2(a, b)
#define TEST(name)                                                       \
    static void name();                                                  \
    static check::Reg CHECK_CAT(reg_, name)(#name, name);                \
    static void name()

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            ++check::failures();                                                      \
            std::printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);    \
        }                                                                             \
    } while (0)

#define CHECK_EQ(a, b)                                                                          \
    do {                                                                                        \
        const auto _a = (a);                                                                    \
        const auto _b = (b);                                                                    \
        if (!(_a == _b)) {                                                                      \
            ++check::failures();                                                                \
            std::printf("  %s:%d: CHECK_EQ(%s, %s) failed\n", __FILE__, __LINE__, #a, #b);      \
        }                                                                                       \
    } while (0)

#define TEST_MAIN() \
    int main() { return check::run_all(); }

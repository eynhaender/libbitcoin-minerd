#pragma once
// Tiny INI reader for board profiles. Deliberately dependency-free so the
// daemon builds against any sysroot.
//
//   # comment            ; comment
//   [section]
//   key = value          # trailing comments allowed
//   [chain.0]            # dotted section names group repeated blocks

#include "minerd/util/status.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace minerd {

class IniConfig {
public:
    static Status load_file(const std::string& path, IniConfig& out);
    static Status parse(const std::string& text, IniConfig& out);

    bool has(const std::string& section, const std::string& key) const;
    std::optional<std::string> get(const std::string& section, const std::string& key) const;

    std::string get_string(const std::string& section, const std::string& key, std::string fallback = {}) const;
    double get_double(const std::string& section, const std::string& key, double fallback) const;
    std::int64_t get_int(const std::string& section, const std::string& key, std::int64_t fallback) const;
    bool get_bool(const std::string& section, const std::string& key, bool fallback) const;

    /// Section names starting with `prefix` (e.g. "chain." -> chain.0, chain.1), sorted.
    std::vector<std::string> sections_with_prefix(const std::string& prefix) const;

    /// All key/values of a section (empty if absent).
    std::map<std::string, std::string> section(const std::string& name) const;

    void set(const std::string& section, const std::string& key, std::string value);

private:
    std::map<std::string, std::map<std::string, std::string>> data_;
};

} // namespace minerd

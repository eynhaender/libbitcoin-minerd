#include "minerd/util/config.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <sstream>

namespace minerd {
namespace {
std::string trim(std::string s) {
    auto ws = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), ws));
    s.erase(std::find_if_not(s.rbegin(), s.rend(), ws).base(), s.end());
    return s;
}
std::string strip_comment(const std::string& line) {
    // A comment starts with '#' or ';' at line start or after whitespace.
    for (std::size_t i = 0; i < line.size(); ++i) {
        if ((line[i] == '#' || line[i] == ';') && (i == 0 || std::isspace(static_cast<unsigned char>(line[i - 1]))))
            return line.substr(0, i);
    }
    return line;
}
} // namespace

Status IniConfig::load_file(const std::string& path, IniConfig& out) {
    std::ifstream in(path);
    if (!in) return Status::error("cannot open config file: " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    return parse(ss.str(), out);
}

Status IniConfig::parse(const std::string& text, IniConfig& out) {
    std::istringstream in(text);
    std::string raw;
    std::string section;
    int lineno = 0;
    while (std::getline(in, raw)) {
        ++lineno;
        std::string line = trim(strip_comment(raw));
        if (line.empty()) continue;
        if (line.front() == '[') {
            if (line.back() != ']') return Status::error("config line " + std::to_string(lineno) + ": bad section");
            section = trim(line.substr(1, line.size() - 2));
            out.data_[section];
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) return Status::error("config line " + std::to_string(lineno) + ": expected key = value");
        out.data_[section][trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return Status::ok();
}

bool IniConfig::has(const std::string& s, const std::string& k) const { return get(s, k).has_value(); }

std::optional<std::string> IniConfig::get(const std::string& s, const std::string& k) const {
    auto it = data_.find(s);
    if (it == data_.end()) return std::nullopt;
    auto kt = it->second.find(k);
    if (kt == it->second.end()) return std::nullopt;
    return kt->second;
}

std::string IniConfig::get_string(const std::string& s, const std::string& k, std::string fb) const {
    auto v = get(s, k);
    return v ? *v : fb;
}

double IniConfig::get_double(const std::string& s, const std::string& k, double fb) const {
    auto v = get(s, k);
    if (!v) return fb;
    try { return std::stod(*v); } catch (...) { return fb; }
}

std::int64_t IniConfig::get_int(const std::string& s, const std::string& k, std::int64_t fb) const {
    auto v = get(s, k);
    if (!v) return fb;
    try { return std::stoll(*v, nullptr, 0); } catch (...) { return fb; }  // base 0: accepts 0x..
}

bool IniConfig::get_bool(const std::string& s, const std::string& k, bool fb) const {
    auto v = get(s, k);
    if (!v) return fb;
    std::string x = *v;
    std::transform(x.begin(), x.end(), x.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (x == "1" || x == "true" || x == "yes" || x == "on") return true;
    if (x == "0" || x == "false" || x == "no" || x == "off") return false;
    return fb;
}

std::vector<std::string> IniConfig::sections_with_prefix(const std::string& prefix) const {
    std::vector<std::string> out;
    for (const auto& [name, _] : data_)
        if (name.rfind(prefix, 0) == 0) out.push_back(name);
    // natural sort on the numeric suffix so chain.10 follows chain.9
    std::sort(out.begin(), out.end(), [&](const std::string& a, const std::string& b) {
        auto num = [&](const std::string& s) -> long {
            try { return std::stol(s.substr(prefix.size())); } catch (...) { return -1; }
        };
        const long na = num(a), nb = num(b);
        return (na != nb) ? na < nb : a < b;
    });
    return out;
}

std::map<std::string, std::string> IniConfig::section(const std::string& name) const {
    auto it = data_.find(name);
    return it == data_.end() ? std::map<std::string, std::string>{} : it->second;
}

void IniConfig::set(const std::string& s, const std::string& k, std::string v) { data_[s][k] = std::move(v); }

} // namespace minerd

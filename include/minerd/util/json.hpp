#pragma once
// Minimal JSON value + parser + serializer, sufficient for Stratum V1 and the
// cgminer-style API. Numbers are stored as double (Stratum ids are small).

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace minerd::json {

class Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;

class Value {
public:
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    Value() : v_(nullptr) {}
    Value(std::nullptr_t) : v_(nullptr) {}
    Value(bool b) : v_(b) {}
    Value(double d) : v_(d) {}
    Value(int i) : v_(double(i)) {}
    Value(long i) : v_(double(i)) {}
    Value(long long i) : v_(double(i)) {}
    Value(unsigned i) : v_(double(i)) {}
    Value(unsigned long i) : v_(double(i)) {}
    Value(unsigned long long i) : v_(double(i)) {}
    Value(const char* s) : v_(std::string(s)) {}
    Value(std::string s) : v_(std::move(s)) {}
    Value(Array a) : v_(std::move(a)) {}
    Value(Object o) : v_(std::move(o)) {}

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(v_); }
    bool is_bool() const { return std::holds_alternative<bool>(v_); }
    bool is_number() const { return std::holds_alternative<double>(v_); }
    bool is_string() const { return std::holds_alternative<std::string>(v_); }
    bool is_array() const { return std::holds_alternative<Array>(v_); }
    bool is_object() const { return std::holds_alternative<Object>(v_); }

    bool as_bool(bool fb = false) const { return is_bool() ? std::get<bool>(v_) : fb; }
    double as_number(double fb = 0) const { return is_number() ? std::get<double>(v_) : fb; }
    const std::string& as_string() const;
    const Array& as_array() const;
    const Object& as_object() const;
    Array& as_array_mut() { return std::get<Array>(v_); }
    Object& as_object_mut() { return std::get<Object>(v_); }

    /// Object member access; returns a null Value if absent or not an object.
    const Value& operator[](const std::string& key) const;
    /// Array element access; returns a null Value if out of range.
    const Value& at(std::size_t i) const;

    std::string dump() const;

private:
    Storage v_;
};

std::optional<Value> parse(std::string_view text, std::string* error = nullptr);

} // namespace minerd::json

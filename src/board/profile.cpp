#include "minerd/board/profile.hpp"

namespace minerd::board {

Status load_profile(const IniConfig& ini, BoardProfile& p) {
    p.name = ini.get_string("board", "name", "unnamed");
    p.platform = ini.get_string("board", "platform", "sim");
    p.i2c_bus = ini.get_string("board", "i2c_bus");
    p.allow_unverified_chips = ini.get_bool("board", "allow_unverified_chips", false);

    p.psu_type = ini.get_string("psu", "type", "none");
    p.psu_voltage_v = ini.get_double("psu", "voltage_v", 0);
    p.psu_options = ini.section("psu");

    for (const auto& sec : ini.sections_with_prefix("chain.")) {
        ChainProfile c;
        try { c.index = unsigned(std::stoul(sec.substr(6))); } catch (...) {
            return Status::error("bad chain section name: " + sec);
        }
        c.enabled = ini.get_bool(sec, "enabled", true);
        c.chip = ini.get_string(sec, "chip");
        if (c.chip.empty()) return Status::error(sec + ": 'chip' is required");
        c.expected_chips = unsigned(ini.get_int(sec, "chips", 0));
        c.uart = ini.get_string(sec, "uart");
        c.baud = unsigned(ini.get_int(sec, "baud", 115200));
        c.fast_baud = unsigned(ini.get_int(sec, "fast_baud", 0));
        c.reset_gpio = int(ini.get_int(sec, "reset_gpio", -1));
        c.reset_active_low = ini.get_bool(sec, "reset_active_low", false);
        c.frequency_mhz = ini.get_double(sec, "frequency_mhz", 0);
        c.voltage_v = ini.get_double(sec, "voltage_v", 0);
        c.chip_difficulty = ini.get_double(sec, "chip_difficulty", 256);
        c.regulator = ini.get_string(sec, "regulator", "none");
        c.options = ini.section(sec);
        p.chains.push_back(std::move(c));
    }
    if (p.chains.empty()) return Status::error("profile defines no [chain.N] sections");

    for (const auto& sec : ini.sections_with_prefix("fan.")) {
        FanProfile f;
        f.type = ini.get_string(sec, "type", "sim");
        f.pwm_path = ini.get_string(sec, "pwm");
        f.rpm_path = ini.get_string(sec, "rpm");
        f.pwm_max = unsigned(ini.get_int(sec, "pwm_max", 255));
        p.fans.push_back(std::move(f));
    }
    for (const auto& sec : ini.sections_with_prefix("temp.")) {
        TempSensorProfile t;
        t.type = ini.get_string(sec, "type", "millidegree");
        t.path = ini.get_string(sec, "path");
        t.name = ini.get_string(sec, "name", sec);
        p.temp_sensors.push_back(std::move(t));
    }

    auto& s = p.safety;
    s.max_chip_temp_c = ini.get_double("safety", "max_chip_temp_c", s.max_chip_temp_c);
    s.max_board_temp_c = ini.get_double("safety", "max_board_temp_c", s.max_board_temp_c);
    s.shutdown_temp_c = ini.get_double("safety", "shutdown_temp_c", s.shutdown_temp_c);
    s.min_fan_rpm = unsigned(ini.get_int("safety", "min_fan_rpm", s.min_fan_rpm));
    s.telemetry_timeout_s = ini.get_double("safety", "telemetry_timeout_s", s.telemetry_timeout_s);
    s.heartbeat_timeout_s = ini.get_double("safety", "heartbeat_timeout_s", s.heartbeat_timeout_s);
    s.require_temperature = ini.get_bool("safety", "require_temperature", s.require_temperature);

    auto& t = p.thermal;
    t.target_temp_c = ini.get_double("thermal", "target_temp_c", t.target_temp_c);
    t.min_fan_percent = ini.get_double("thermal", "min_fan_percent", t.min_fan_percent);
    t.max_fan_percent = ini.get_double("thermal", "max_fan_percent", t.max_fan_percent);
    t.kp = ini.get_double("thermal", "kp", t.kp);
    t.ki = ini.get_double("thermal", "ki", t.ki);

    auto& tu = p.tuning;
    tu.enabled = ini.get_bool("tuning", "enabled", tu.enabled);
    tu.step_mhz = ini.get_double("tuning", "step_mhz", tu.step_mhz);
    tu.max_hw_error_rate = ini.get_double("tuning", "max_hw_error_rate", tu.max_hw_error_rate);
    tu.min_temp_margin_c = ini.get_double("tuning", "min_temp_margin_c", tu.min_temp_margin_c);
    tu.interval_s = unsigned(ini.get_int("tuning", "interval_s", tu.interval_s));
    tu.max_mhz = ini.get_double("tuning", "max_mhz", tu.max_mhz);

    p.pool.url = ini.get_string("pool", "url");
    p.pool.user = ini.get_string("pool", "user");
    p.pool.password = ini.get_string("pool", "password", "x");
    p.pool.suggest_difficulty = ini.get_double("pool", "suggest_difficulty", 0);

    p.api_bind = ini.get_string("api", "bind", "127.0.0.1");
    p.api_port = unsigned(ini.get_int("api", "port", 4028));

    // Sanity: thermal limits must be ordered.
    if (!(t.target_temp_c < s.max_chip_temp_c && s.max_chip_temp_c <= s.shutdown_temp_c))
        return Status::error("thermal limits must satisfy target < max_chip_temp <= shutdown_temp");
    return Status::ok();
}

Status load_profile_file(const std::string& path, BoardProfile& out) {
    IniConfig ini;
    MINERD_TRY(IniConfig::load_file(path, ini));
    return load_profile(ini, out);
}

} // namespace minerd::board

#pragma once
// A board profile describes one miner model on one control-board variant.
// It is pure data (an .ini file): switching miners = writing a new profile,
// as long as a driver for its chip exists in the ChipRegistry.

#include "minerd/util/config.hpp"
#include "minerd/util/status.hpp"

#include <map>
#include <string>
#include <vector>

namespace minerd::board {

struct ChainProfile {
    unsigned index = 0;
    bool enabled = true;
    std::string chip;                 ///< ChipRegistry name: "BM1362", "BM1366", "SIM", ...
    unsigned expected_chips = 0;
    std::string uart;                 ///< "/dev/ttyS1"; empty for SIM
    unsigned baud = 115200;
    unsigned fast_baud = 0;
    int reset_gpio = -1;
    bool reset_active_low = false;
    double frequency_mhz = 0;         ///< 0 = chip default
    double voltage_v = 0;             ///< board/domain voltage target
    double chip_difficulty = 256;
    std::string regulator = "none";   ///< "none" | "sim" | "pic" | "dcdc-direct"
    std::map<std::string, std::string> options;  ///< everything in [chain.N], passed to drivers
};

struct FanProfile {
    std::string type = "sim";         ///< "sim" | "hwmon"
    std::string pwm_path;
    std::string rpm_path;
    unsigned pwm_max = 255;
};

struct TempSensorProfile {
    std::string type = "millidegree"; ///< file with millidegrees
    std::string path;
    std::string name;
};

struct SafetyLimits {
    double max_chip_temp_c = 90;
    double max_board_temp_c = 85;
    double shutdown_temp_c = 95;      ///< hard trip
    unsigned min_fan_rpm = 1000;
    double telemetry_timeout_s = 5;
    double heartbeat_timeout_s = 10;
    bool require_temperature = true;  ///< trip if no temperature source answers
};

struct ThermalProfile {
    double target_temp_c = 70;
    double min_fan_percent = 30;
    double max_fan_percent = 100;
    double kp = 4.0;
    double ki = 0.2;
};

struct TuningProfile {
    bool enabled = false;
    double step_mhz = 6.25;
    double max_hw_error_rate = 0.002; ///< errors / (errors + nonces) per chip
    double min_temp_margin_c = 8;     ///< only raise clocks below max_chip_temp - margin
    unsigned interval_s = 120;
    double max_mhz = 0;               ///< 0 = chip maximum
};

struct PoolProfile {
    std::string url;                  ///< stratum+tcp://host:port | local-test
    std::string user;
    std::string password = "x";
    double suggest_difficulty = 0;
};

struct BoardProfile {
    std::string name;
    std::string platform = "sim";     ///< "sim" | "beaglebone" | "zynq" | "amlogic" | "linux-generic"
    std::string i2c_bus;              ///< "/dev/i2c-0"
    std::string psu_type = "none";
    std::map<std::string, std::string> psu_options;
    double psu_voltage_v = 0;
    std::vector<ChainProfile> chains;
    std::vector<FanProfile> fans;
    std::vector<TempSensorProfile> temp_sensors;
    SafetyLimits safety;
    ThermalProfile thermal;
    TuningProfile tuning;
    PoolProfile pool;
    std::string api_bind = "127.0.0.1";
    unsigned api_port = 4028;
    bool allow_unverified_chips = false;
};

Status load_profile(const IniConfig& ini, BoardProfile& out);
Status load_profile_file(const std::string& path, BoardProfile& out);

} // namespace minerd::board

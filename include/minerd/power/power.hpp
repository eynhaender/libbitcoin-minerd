#pragma once
// Power path abstraction.
//
//   IPowerSupply      – the 12-15 V supply feeding all hashboards (APW12 on S19)
//   IVoltageRegulator – per-hashboard enable/voltage control (PIC MCU or
//                       direct DC-DC on "noPIC" boards, other vendors' PMICs)
//
// SAFETY RULE: drivers for hardware whose protocol is not implemented MUST
// return Status::not_implemented and never pretend success. Every power_off()
// must be idempotent and callable from the safety thread at any time.

#include "minerd/hal/hal.hpp"
#include "minerd/util/status.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>

namespace minerd::power {

struct PsuReading {
    std::optional<double> voltage_v;
    std::optional<double> current_a;
    std::optional<double> power_w;
    std::optional<double> temperature_c;
    bool fault = false;
};

class IPowerSupply {
public:
    virtual ~IPowerSupply() = default;
    virtual std::string name() const = 0;
    virtual Status power_on(double voltage_v) = 0;
    virtual Status set_voltage(double voltage_v) = 0;
    virtual void power_off() = 0;
    virtual PsuReading read() = 0;
};

class IVoltageRegulator {
public:
    virtual ~IVoltageRegulator() = default;
    virtual std::string name() const = 0;
    virtual Status enable(double voltage_v) = 0;
    virtual Status set_voltage(double voltage_v) = 0;
    virtual void power_off() = 0;
    virtual std::optional<double> read_voltage() = 0;
};

struct PowerContext {
    std::shared_ptr<hal::II2cBus> i2c;               ///< may be null
    std::map<std::string, std::string> options;      ///< profile key/values
};

/// "none" (externally controlled / bench supply), "sim", "apw12"
std::unique_ptr<IPowerSupply> make_psu(const std::string& type, const PowerContext& ctx, Status& status);
/// "none" (always-on board), "sim", "pic" (Bitmain PIC MCU), "dcdc-direct" (noPIC boards)
std::unique_ptr<IVoltageRegulator> make_regulator(const std::string& type, unsigned chain_index,
                                                   const PowerContext& ctx, Status& status);

} // namespace minerd::power

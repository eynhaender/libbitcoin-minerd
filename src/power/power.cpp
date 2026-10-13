#include "minerd/power/power.hpp"

#include "minerd/util/log.hpp"

#include <atomic>
#include <mutex>

namespace minerd::power {
namespace {

std::string hex8(std::uint8_t v) { return to_hex(std::span<const std::uint8_t>(&v, 1)); }

std::string opt(const PowerContext& c, const std::string& k, const std::string& fb = {}) {
    auto it = c.options.find(k);
    return it == c.options.end() ? fb : it->second;
}

// --- externally controlled supply (bench PSU, fixed-voltage APW) -------------
class ExternalPsu final : public IPowerSupply {
public:
    std::string name() const override { return "external"; }
    Status power_on(double) override {
        log::warn("power", "PSU is externally controlled – make sure it is set to a safe voltage");
        return Status::ok();
    }
    Status set_voltage(double) override { return Status::not_implemented("voltage control on external PSU"); }
    void power_off() override {
        log::warn("power", "external PSU cannot be switched off by software – cut power manually if needed");
    }
    PsuReading read() override { return {}; }
};

// --- simulation ---------------------------------------------------------------
class SimPsu final : public IPowerSupply {
public:
    std::string name() const override { return "sim-psu"; }
    Status power_on(double v) override { on_ = true; v_ = v; return Status::ok(); }
    Status set_voltage(double v) override { v_ = v; return Status::ok(); }
    void power_off() override { on_ = false; }
    PsuReading read() override {
        PsuReading r;
        r.voltage_v = on_ ? v_.load() : 0.0;
        r.power_w = on_ ? 120.0 : 0.0;
        return r;
    }

private:
    std::atomic<bool> on_{false};
    std::atomic<double> v_{0};
};

class SimRegulator final : public IVoltageRegulator {
public:
    explicit SimRegulator(unsigned idx) : idx_(idx) {}
    std::string name() const override { return "sim-reg" + std::to_string(idx_); }
    Status enable(double v) override { on_ = true; v_ = v; return Status::ok(); }
    Status set_voltage(double v) override { v_ = v; return Status::ok(); }
    void power_off() override { on_ = false; }
    std::optional<double> read_voltage() override { return on_ ? v_.load() : 0.0; }

private:
    unsigned idx_;
    std::atomic<bool> on_{false};
    std::atomic<double> v_{0};
};

class AlwaysOnRegulator final : public IVoltageRegulator {
public:
    std::string name() const override { return "none"; }
    Status enable(double) override { return Status::ok(); }
    Status set_voltage(double) override { return Status::not_implemented("voltage control on always-on board"); }
    void power_off() override {}
    std::optional<double> read_voltage() override { return std::nullopt; }
};

// --- Bitmain APW12 ------------------------------------------------------------
// The APW12 is controlled over I2C from the control board (voltage set-point,
// enable, read-back). The command set is not publicly documented.
// TODO(capture): record the stock firmware's I2C traffic at power-up and
// implement power_on/set_voltage/read. Until then this fails closed.
class Apw12Psu final : public IPowerSupply {
public:
    explicit Apw12Psu(std::shared_ptr<hal::II2cBus> bus, std::uint8_t addr) : bus_(std::move(bus)), addr_(addr) {}
    std::string name() const override { return "apw12"; }
    Status power_on(double) override {
        return Status::not_implemented("APW12 I2C protocol at 0x" + hex8(addr_) + " (see docs/PORTING.md)");
    }
    Status set_voltage(double) override { return Status::not_implemented("APW12 I2C protocol"); }
    void power_off() override {
        // TODO(capture): send the APW12 disable command. Until implemented the
        // PSU stays in whatever state the bootloader left it in, so the board
        // regulators are the effective power switch.
        log::error("power", "APW12 power_off not implemented – relying on hashboard regulators");
    }
    PsuReading read() override { return {}; }

private:
    std::shared_ptr<hal::II2cBus> bus_;
    std::uint8_t addr_;
};

// --- Bitmain hashboard PIC ----------------------------------------------------
// S19-generation hashboards (non "noPIC") carry a PIC MCU on the shared I2C bus
// that enables the board DC-DC and sets its output. Protocol undocumented.
class PicRegulator final : public IVoltageRegulator {
public:
    PicRegulator(std::shared_ptr<hal::II2cBus> bus, std::uint8_t addr, unsigned idx) : bus_(std::move(bus)), addr_(addr), idx_(idx) {}
    std::string name() const override { return "pic" + std::to_string(idx_); }
    Status enable(double) override {
        return Status::not_implemented("Bitmain PIC protocol at 0x" + hex8(addr_) + " (see docs/PORTING.md)");
    }
    Status set_voltage(double) override { return Status::not_implemented("Bitmain PIC protocol"); }
    void power_off() override {
        // TODO(capture): PIC "disable DC-DC" command.
        log::error("power", "PIC power_off not implemented for chain ", idx_);
    }
    std::optional<double> read_voltage() override { return std::nullopt; }

private:
    std::shared_ptr<hal::II2cBus> bus_;
    std::uint8_t addr_;
    unsigned idx_;
};

} // namespace

std::unique_ptr<IPowerSupply> make_psu(const std::string& type, const PowerContext& ctx, Status& status) {
    status = Status::ok();
    if (type == "none" || type == "external") return std::make_unique<ExternalPsu>();
    if (type == "sim") return std::make_unique<SimPsu>();
    if (type == "apw12") {
        if (!ctx.i2c) { status = Status::error("apw12 needs an I2C bus"); return nullptr; }
        const auto addr = std::uint8_t(std::stoul(opt(ctx, "i2c_addr", "0x10"), nullptr, 0));
        return std::make_unique<Apw12Psu>(ctx.i2c, addr);
    }
    status = Status::error("unknown PSU type: " + type);
    return nullptr;
}

std::unique_ptr<IVoltageRegulator> make_regulator(const std::string& type, unsigned idx, const PowerContext& ctx,
                                                   Status& status) {
    status = Status::ok();
    if (type == "none") return std::make_unique<AlwaysOnRegulator>();
    if (type == "sim") return std::make_unique<SimRegulator>(idx);
    if (type == "pic" || type == "dcdc-direct") {
        if (!ctx.i2c) { status = Status::error(type + " regulator needs an I2C bus"); return nullptr; }
        const auto addr = std::uint8_t(std::stoul(opt(ctx, "regulator_i2c_addr", "0x20"), nullptr, 0));
        // dcdc-direct (noPIC) boards: separate driver to be written once the
        // DC-DC controller part number is known; share the fail-closed stub for now.
        return std::make_unique<PicRegulator>(ctx.i2c, addr, idx);
    }
    status = Status::error("unknown regulator type: " + type);
    return nullptr;
}

} // namespace minerd::power

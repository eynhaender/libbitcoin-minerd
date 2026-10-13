#include "minerd/board/platform.hpp"

#include "minerd/hal/linux_hal.hpp"
#include "minerd/hal/sim_hal.hpp"
#include "minerd/util/log.hpp"

namespace minerd::board {

Status build_hardware(const BoardProfile& p, const asic::ChipRegistry& chips, Hardware& hw) {
    const bool sim = (p.platform == "sim");
    if (!sim && p.platform != "beaglebone" && p.platform != "zynq" && p.platform != "amlogic" &&
        p.platform != "linux-generic")
        return Status::error("unknown platform: " + p.platform);

    if (p.platform == "zynq")
        log::warn("platform", "Zynq boards route hashboard UARTs through FPGA logic; the stock bitstream must be "
                              "loaded before minerd starts (see docs/PORTING.md)");
    if (p.platform == "amlogic")
        log::warn("platform", "Amlogic boards may enforce signed boot images; running minerd requires a userland "
                              "you control (see docs/PORTING.md)");

    // I2C bus (PSU + regulators)
    if (!p.i2c_bus.empty() && !sim) {
        Status s;
        auto bus = hal::linux_hal::open_i2c(p.i2c_bus, s);
        MINERD_TRY(s);
        hw.i2c = std::move(bus);
    }

    // PSU
    {
        power::PowerContext ctx{hw.i2c, p.psu_options};
        Status s;
        hw.psu = power::make_psu(p.psu_type, ctx, s);
        MINERD_TRY(s);
    }

    // Chains
    for (const auto& cp : p.chains) {
        if (!cp.enabled) continue;
        const asic::ChipTraits* traits = chips.traits(cp.chip);
        if (!traits) return Status::error("chain " + std::to_string(cp.index) + ": no driver for chip '" + cp.chip + "'");
        if (!traits->verified && !p.allow_unverified_chips)
            return Status::error("chain " + std::to_string(cp.index) + ": driver for " + cp.chip +
                                 " is not verified on hardware; set [board] allow_unverified_chips = true only on a "
                                 "bench setup with current-limited power");

        asic::ChainContext ctx;
        ctx.chain_index = cp.index;
        ctx.options = cp.options;
        if (!cp.uart.empty()) {
            Status s;
            ctx.serial = hal::linux_hal::open_serial(cp.uart, s);
            MINERD_TRY(s);
        }
        if (cp.reset_gpio >= 0) {
            Status s;
            ctx.reset = hal::linux_hal::open_gpio_sysfs(cp.reset_gpio, true, cp.reset_active_low, s);
            MINERD_TRY(s);
        }
        auto chain = chips.create(cp.chip, std::move(ctx));
        if (!chain) return Status::error("chain " + std::to_string(cp.index) + ": driver creation failed");

        power::PowerContext pctx{hw.i2c, cp.options};
        Status s;
        auto reg = power::make_regulator(cp.regulator, cp.index, pctx, s);
        MINERD_TRY(s);

        hw.boards.push_back(std::make_unique<HashBoard>(cp, std::move(chain), std::move(reg)));
    }
    if (hw.boards.empty()) return Status::error("no enabled chains");

    // Fans
    unsigned fi = 0;
    for (const auto& f : p.fans) {
        if (f.type == "sim") {
            hw.fans.push_back(std::make_unique<hal::sim::SimFan>(fi++));
        } else if (f.type == "hwmon") {
            Status s;
            auto fan = hal::linux_hal::open_hwmon_fan(f.pwm_path, f.rpm_path, f.pwm_max, s);
            MINERD_TRY(s);
            hw.fans.push_back(std::move(fan));
        } else {
            return Status::error("unknown fan type: " + f.type);
        }
    }

    // Extra temperature sensors
    for (const auto& t : p.temp_sensors) {
        if (t.type == "millidegree") {
            Status s;
            auto sensor = hal::linux_hal::open_millidegree_sensor(t.path, t.name, s);
            MINERD_TRY(s);
            hw.temp_sensors.push_back(std::move(sensor));
        } else {
            return Status::error("unknown temperature sensor type: " + t.type);
        }
    }
    return Status::ok();
}

} // namespace minerd::board

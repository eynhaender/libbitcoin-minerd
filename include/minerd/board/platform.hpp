#pragma once
// Turns a BoardProfile into live hardware objects. This is the only file that
// knows which HAL implementation backs which profile entry.

#include "minerd/asic/chip_registry.hpp"
#include "minerd/board/hash_board.hpp"
#include "minerd/board/profile.hpp"
#include "minerd/hal/hal.hpp"
#include "minerd/power/power.hpp"

#include <memory>
#include <vector>

namespace minerd::board {

struct Hardware {
    std::shared_ptr<hal::II2cBus> i2c;
    std::unique_ptr<power::IPowerSupply> psu;
    std::vector<std::unique_ptr<HashBoard>> boards;
    std::vector<std::unique_ptr<hal::IFan>> fans;
    std::vector<std::unique_ptr<hal::ITempSensor>> temp_sensors;  ///< control-board / external sensors
};

Status build_hardware(const BoardProfile& profile, const asic::ChipRegistry& chips, Hardware& out);

} // namespace minerd::board

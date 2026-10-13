#pragma once
// Name -> factory map for chain drivers.
//
// Registration is explicit (register_builtin_chips) instead of via static
// initializers, because static self-registration silently disappears when
// drivers are linked from a static library.

#include "minerd/asic/chip.hpp"
#include "minerd/hal/hal.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace minerd::asic {

/// Everything a chain driver may need from the board/platform layer.
struct ChainContext {
    unsigned chain_index = 0;
    std::unique_ptr<hal::ISerialPort> serial;   ///< may be null for simulated chips
    std::unique_ptr<hal::IGpioLine> reset;      ///< optional chain reset line
    /// Free-form driver options from the profile's [chain.N] section
    /// (e.g. "sim_hashrate_mhs", "address_interval").
    std::map<std::string, std::string> options;
};

using ChainFactory = std::function<std::unique_ptr<IAsicChain>(ChainContext ctx)>;

class ChipRegistry {
public:
    void add(const std::string& chip_name, ChainFactory factory, ChipTraits traits);
    bool contains(const std::string& chip_name) const;
    std::unique_ptr<IAsicChain> create(const std::string& chip_name, ChainContext ctx) const;
    const ChipTraits* traits(const std::string& chip_name) const;
    std::vector<std::string> names() const;

private:
    struct Entry {
        ChainFactory factory;
        ChipTraits traits;
    };
    std::map<std::string, Entry> entries_;
};

/// Registers all drivers compiled into this binary (BM1362, BM1366, SIM, ...).
void register_builtin_chips(ChipRegistry& registry);

} // namespace minerd::asic

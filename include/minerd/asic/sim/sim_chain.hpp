#pragma once
// Software chain: hashes on the CPU with the real SHA-256 path, returns nonces
// above a configurable chip difficulty and can inject hardware errors. Lets the
// whole daemon (pool, dispatcher, validation, tuning, safety) run in CI and on
// a laptop without a single ASIC.

#include "minerd/asic/chip_registry.hpp"

namespace minerd::asic::sim {

ChipTraits sim_traits();
/// Options (profile [chain.N]): sim_chips, sim_hw_error_rate (0..1),
/// sim_max_mhs (CPU cap), sim_temp_base_c.
std::unique_ptr<IAsicChain> make_sim_chain(ChainContext ctx);

} // namespace minerd::asic::sim

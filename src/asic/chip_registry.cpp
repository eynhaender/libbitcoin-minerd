#include "minerd/asic/chip_registry.hpp"

#include "minerd/asic/bm13xx/variants.hpp"
#include "minerd/asic/sim/sim_chain.hpp"

namespace minerd::asic {

void ChipRegistry::add(const std::string& name, ChainFactory factory, ChipTraits traits) {
    entries_[name] = Entry{std::move(factory), std::move(traits)};
}

bool ChipRegistry::contains(const std::string& name) const { return entries_.count(name) != 0; }

std::unique_ptr<IAsicChain> ChipRegistry::create(const std::string& name, ChainContext ctx) const {
    auto it = entries_.find(name);
    if (it == entries_.end()) return nullptr;
    return it->second.factory(std::move(ctx));
}

const ChipTraits* ChipRegistry::traits(const std::string& name) const {
    auto it = entries_.find(name);
    return it == entries_.end() ? nullptr : &it->second.traits;
}

std::vector<std::string> ChipRegistry::names() const {
    std::vector<std::string> out;
    for (const auto& [n, _] : entries_) out.push_back(n);
    return out;
}

void register_builtin_chips(ChipRegistry& r) {
    // Bitmain BM13xx family: one driver, per-chip variant tables.
    for (const auto* variant : bm13xx::builtin_variants()) {
        r.add(variant->traits.name,
              [variant](ChainContext ctx) { return bm13xx::make_chain(*variant, std::move(ctx)); },
              variant->traits);
    }
    // Pure software chain for development and CI.
    r.add("SIM", [](ChainContext ctx) { return sim::make_sim_chain(std::move(ctx)); }, sim::sim_traits());

    // Add other vendors here, e.g.:
    //   r.add("A3HB70601", [](ChainContext c){ return canaan::make_chain(std::move(c)); }, canaan::traits());
}

} // namespace minerd::asic

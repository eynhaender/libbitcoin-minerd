// BM1362 (Antminer S19j Pro / S19j Pro+), 5 nm.
//
// STATUS: SKELETON – NOT VERIFIED ON HARDWARE.
//
// Bitmain publishes no datasheet. This variant starts from the BM1366 codec
// and register map because both belong to the same BM13xx generation with
// chip-side midstate computation and version rolling. Every value marked
// VERIFY must be confirmed against a UART capture of the stock S19j Pro
// firmware (logic analyser on a chain's TX/RX between control board and
// hashboard) before the daemon is allowed to drive real hashboards.
//
// Capture checklist (fill in docs/PORTING.md as you go):
//   [ ] chip-id register reply  -> expect 0x1362xxxx
//   [ ] init register sequence  -> replace init_registers below
//   [ ] job frame length/layout -> compare with encode_job_bm1366_style (82B)
//   [ ] response length + flag  -> response_len, nonce/register flag bit
//   [ ] chip address bits in nonce
//   [ ] hash-counting register value for 126 chips
//   [ ] fast UART register + divider
//   [ ] PLL encoding and VCO limits

#include "minerd/asic/bm13xx/variants.hpp"

namespace minerd::asic::bm13xx {
namespace {
Bm13xxVariant build_bm1362() {
    Bm13xxVariant v = bm1366();  // start from the closest documented sibling

    v.traits.name = "BM1362";
    v.traits.chip_id = 0x1362;                // VERIFY
    // 104 TH/s / 378 chips ~= 275 GH/s per chip at stock clock. With an
    // assumed stock frequency of ~525 MHz this gives ~525 hashes/clock.
    v.traits.hashes_per_clock = 525;          // VERIFY (derived, not measured)
    v.traits.freq_min_mhz = 50;
    v.traits.freq_max_mhz = 650;              // VERIFY
    v.traits.freq_default_mhz = 525;          // VERIFY
    v.traits.chain_voltage_min_v = 12.0;      // sanity clamp; VERIFY against stock logs
    v.traits.chain_voltage_max_v = 15.0;
    v.traits.verified = false;

    // Antminer chains carry 126 chips; spread addresses over the 8-bit space.
    v.hash_counting_value = [](unsigned /*chips*/) -> std::uint32_t { return 0x0000151C; };  // VERIFY

    return v;
}
} // namespace

const Bm13xxVariant& bm1362() {
    static const Bm13xxVariant v = build_bm1362();
    return v;
}

} // namespace minerd::asic::bm13xx

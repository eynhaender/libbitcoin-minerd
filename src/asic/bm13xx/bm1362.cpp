// BM1362 (Antminer S19j Pro / S19j Pro+), 5 nm.
//
// STATUS: NOT VERIFIED ON HARDWARE BY THIS PROJECT.
//
// Values are taken from Mujina's BM13xx chip reference
// (github.com/256foundation/mujina, mujina-miner/src/asic/bm13xx/REFERENCE.md,
// GPL-3.0-or-later), which compiles open-source implementations and captures
// of production hardware, including an S19j Pro. Each value carries a tag:
//
//   [CAPTURE]  observed in a published capture (S19j Pro unless noted)
//   [REF]      stated in the reference, not tied to a specific capture
//   [INFERRED] inferred in the reference, not observed directly
//   [DERIVED]  computed by this project from documented formulas
//   [UNKNOWN]  placeholder until our own capture (docs/PORTING.md)
//
// `traits.verified` stays false until our own S19j Pro capture confirms the
// init sequence end to end.

#include "minerd/asic/bm13xx/variants.hpp"

#include <cmath>

namespace minerd::asic::bm13xx {
namespace {

// MIDSTATE_CONFIG / core mailbox / soft-reset encodings (REF).
constexpr std::uint32_t core_mailbox(std::uint8_t core_reg, std::uint8_t value) {
    // [31] all cores [15] write [12:8] core register [7:0] value
    return 0x80000000u | 0x8000u | (std::uint32_t(core_reg & 0x1F) << 8) | value;
}
constexpr std::uint8_t kCoreRegClockDelay = 0x00;
constexpr std::uint8_t kCoreRegCoreEnable = 0x02;
constexpr std::uint8_t kCoreRegClockSelect = 0x05;

constexpr std::uint8_t kRegMiscControl = 0x18;
constexpr std::uint8_t kRegUartBaud = 0x28;
constexpr std::uint8_t kRegCoreMailbox = 0x3C;
constexpr std::uint8_t kRegIoDriverStrength = 0x58;
constexpr std::uint8_t kRegSoftReset = 0xA8;

constexpr std::uint32_t kMiscOpenCore = 0xB000C100;        // [CAPTURE] BM1362 open-core value
constexpr std::uint32_t kSoftResetNone = 0x00000000;       // [CAPTURE] broadcast during bring-up
constexpr std::uint32_t kSoftResetCoreFast = 0x00000002;   // [CAPTURE] per chip before core config
constexpr std::uint32_t kIoDriverStrength = 0x00011111;    // [INFERRED] all fields at strength 1

std::uint8_t job_byte_to_id(std::uint8_t b) {
    // Response result header: [7:3] job id, [2:0] subcore id  [CAPTURE]
    // Job frame header byte:  [7:3] job id, [2:0] zero         [REF]
    // -> the response byte with the subcore bits cleared equals the job-frame byte.
    return std::uint8_t(b & 0xF8);
}

std::optional<DecodedResponse> decode_bm1362(std::span<const std::uint8_t> f) {
    return decode_response_full(f, job_byte_to_id, 0x07);
}

Bm13xxVariant build_bm1362() {
    Bm13xxVariant v = bm1366();  // share the generic register map, then override

    v.traits.name = "BM1362";
    v.traits.chip_id = 0x1362;                 // [CAPTURE] CHIP_ID reads 13 62 03 00
    v.traits.hashes_per_clock = 65 * 8;        // [REF] 65 cores x 8 sub-cores
    v.traits.work_format = WorkFormat::FullHeader;  // [REF] chip computes midstates
    v.traits.version_rolling = true;
    v.traits.freq_min_mhz = 50;
    v.traits.freq_max_mhz = 650;               // [UNKNOWN] conservative cap
    v.traits.freq_default_mhz = 525;           // [UNKNOWN] 520 x 525 MHz x 378 chips ~ 103 TH/s (spec 104)
    v.traits.chain_voltage_min_v = 12.0;       // [UNKNOWN] sanity clamp only
    v.traits.chain_voltage_max_v = 15.0;
    v.traits.verified = false;

    // PLL: FB_DIV 0x10-0xFA, VCO 2000-3200 MHz  [REF]
    v.pll.fb_min = 0x10;
    v.pll.fb_max = 0xFA;
    v.pll.vco_min_mhz = 2000;
    v.pll.vco_max_mhz = 3200;

    // Responses: 11 bytes, type in bits 7-5 of the last byte, CRC5 checked  [REF]
    v.response_len = 11;
    v.decode_response = decode_bm1362;
    v.check_response_crc = true;

    // Job ids: 5-bit field in bits 7-3 of the job header byte  [CAPTURE: ids > 15 seen]
    v.job_id_count = 32;
    v.job_id_step = 8;

    // One job keeps a chip busy for 8192 sweeps (2^16 versions / 8 midstates)  [REF]
    v.work_interval_factor = 4096;

    // MIDSTATE_CONFIG: once before discovery, twice after  [CAPTURE]
    // (value 0x9000FFFF for the pool mask 0x1FFFE000, see version_mask_value)
    v.version_mask_writes_before_enum = 1;
    v.version_mask_writes_after_enum = 2;

    // Init sequence after addressing. The published capture is truncated after
    // MISC_CONTROL, so ordering beyond that is partly [INFERRED].
    v.init_registers = {
        {kRegSoftReset, kSoftResetNone, false},                                   // [CAPTURE]
        {kRegMiscControl, kMiscOpenCore, false},                                  // [CAPTURE]
        {kRegIoDriverStrength, kIoDriverStrength, false},                         // [INFERRED]
        {kRegCoreMailbox, core_mailbox(kCoreRegClockSelect, 0x40), false},        // [REF] broadcast form derived
        {kRegCoreMailbox, core_mailbox(kCoreRegClockDelay, 0x08), false},         // [REF]
        {kRegSoftReset, kSoftResetCoreFast, true},                                // [CAPTURE] per chip
        {kRegMiscControl, kMiscOpenCore, true},                                   // [CAPTURE] per chip
        {kRegCoreMailbox, core_mailbox(kCoreRegClockSelect, 0x40), true},         // [REF]
        {kRegCoreMailbox, core_mailbox(kCoreRegClockDelay, 0x08), true},          // [REF]
        {kRegCoreMailbox, core_mailbox(kCoreRegCoreEnable, 0xAA), true},          // [REF] per-chip pass only
    };
    // Not written on the S19j Pro: UART_RELAY (0x2C)  [CAPTURE]
    // 126 chips in 42 voltage domains of 3; address step 2 (chip 110 = 0xDC)  [CAPTURE]

    // HASH_COUNTING_NUMBER, in 25 MHz ticks. Each core sweeps an 18-bit counter
    // and advances 2 nonces per hash clock, so one sweep takes 2^17 hash clocks
    // = 2^17 * 25 / f[MHz] ticks.  [DERIVED] – the reference's full-coverage
    // formula is not published; verify against the capture.
    v.hash_counting_value = [](unsigned /*chips*/, double freq_mhz) -> std::uint32_t {
        if (freq_mhz <= 0) freq_mhz = 525;
        return std::uint32_t(std::lround(131072.0 * 25.0 / freq_mhz));
    };

    // UART_BAUD (0x28): divider in [15:8], baud = 25 MHz / (8 x (div+1)),
    // bit 28 set on BM1362-generation firmware. S19j Pro: 0x11300000 = 3.125 Mbaud.  [CAPTURE]
    v.fast_uart = [](unsigned baud) -> std::optional<RegisterWrite> {
        auto div = uart_divider_for(baud);
        if (!div) return std::nullopt;
        return RegisterWrite{kRegUartBaud, 0x11300000u | (std::uint32_t(*div) << 8), false};
    };

    return v;
}
} // namespace

const Bm13xxVariant& bm1362() {
    static const Bm13xxVariant v = build_bm1362();
    return v;
}

} // namespace minerd::asic::bm13xx

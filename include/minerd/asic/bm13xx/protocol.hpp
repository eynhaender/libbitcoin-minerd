#pragma once
// BM13xx serial protocol primitives shared by all Bitmain chips since BM1387.
//
// Sources: the publicly documented framing used by open firmware (cgminer
// Bitmain drivers, ESP-Miner for BM1366/68/70/97). Values verified by unit
// tests against well-known frames, e.g. "55 AA 52 05 00 00 0A" (read chip-id
// from all chips) and "55 AA 53 05 00 00 03" (chain inactive).
//
// Command frame:  55 AA | hdr | len | data... | crc5
// Job frame:      55 AA | hdr | len | data... | crc16(BE)
// Response frame: AA 55 | payload ... (length is chip-specific)

#include "minerd/util/bytes.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace minerd::asic::bm13xx {

namespace hdr {
constexpr std::uint8_t TYPE_JOB = 0x20;
constexpr std::uint8_t TYPE_CMD = 0x40;
constexpr std::uint8_t GROUP_SINGLE = 0x00;
constexpr std::uint8_t GROUP_ALL = 0x10;
constexpr std::uint8_t CMD_SETADDRESS = 0x00;
constexpr std::uint8_t CMD_WRITE = 0x01;
constexpr std::uint8_t CMD_READ = 0x02;
constexpr std::uint8_t CMD_INACTIVE = 0x03;
} // namespace hdr

/// CRC-5, poly x^5+x^2+1, init 0x1F, MSB first (BM13xx command frames).
std::uint8_t crc5(std::span<const std::uint8_t> data);
/// CRC-16/CCITT-FALSE, poly 0x1021, init 0xFFFF (BM13xx job frames).
std::uint16_t crc16_false(std::span<const std::uint8_t> data);

Bytes frame_command(std::uint8_t header, std::span<const std::uint8_t> data);
Bytes frame_job(std::span<const std::uint8_t> data);

Bytes cmd_chain_inactive();
Bytes cmd_set_address(std::uint8_t address);
Bytes cmd_read_register_all(std::uint8_t reg);
Bytes cmd_read_register(std::uint8_t address, std::uint8_t reg);
Bytes cmd_write_register_all(std::uint8_t reg, std::uint32_t value);
Bytes cmd_write_register(std::uint8_t address, std::uint8_t reg, std::uint32_t value);

// --- PLL -------------------------------------------------------------------
struct PllSetting {
    unsigned fb_div = 0;
    unsigned ref_div = 0;
    unsigned post_div1 = 0;
    unsigned post_div2 = 0;
    double freq_mhz = 0;   ///< actually achieved frequency
};
struct PllLimits {
    double ref_clock_mhz = 25.0;
    unsigned fb_min = 0xA0, fb_max = 0xEF;
    unsigned ref_min = 1, ref_max = 2;
    unsigned post_min = 1, post_max = 7;
    double vco_min_mhz = 1600, vco_max_mhz = 3200;
};
/// Closest achievable frequency <= target (never overshoot).
std::optional<PllSetting> solve_pll(double target_mhz, const PllLimits& limits = {});
/// PLL0 register value in the BM1366-style encoding:
///   [31:24] 0x40 | (VCO >= 2400 MHz ? 0x10 : 0)
///   [23:16] fb_div  [15:8] ref_div  [7:4] post1-1  [3:0] post2-1
std::uint32_t pll_register_value(const PllSetting& s, const PllLimits& limits = {});

// --- difficulty ------------------------------------------------------------
/// Ticket-mask register value for a chip-side difficulty (rounded down to a
/// power of two). Bits are reversed per byte as the chips expect.
std::uint32_t ticket_mask_value(double difficulty);
/// Difficulty actually configured by ticket_mask_value(difficulty).
double effective_ticket_difficulty(double difficulty);

// --- response framing ------------------------------------------------------
/// Reassembles fixed-length responses (starting with AA 55) from a byte stream,
/// resynchronising on garbage.
class ResponseFramer {
public:
    explicit ResponseFramer(std::size_t frame_len) : frame_len_(frame_len) {}
    void feed(std::span<const std::uint8_t> bytes);
    std::optional<Bytes> next();
    std::size_t discarded_bytes() const { return discarded_; }

private:
    std::size_t frame_len_;
    Bytes buf_;
    std::size_t discarded_ = 0;
};

} // namespace minerd::asic::bm13xx

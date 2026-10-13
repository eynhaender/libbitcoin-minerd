#pragma once
// A BM13xx "variant" is a data table + two codecs. Adding a new Bitmain chip
// normally means adding one .cpp file that fills a Bm13xxVariant – the
// transport, enumeration, addressing, PLL ramping, job-id bookkeeping and
// result decoding loop are shared in Bm13xxChain.

#include "minerd/asic/bm13xx/protocol.hpp"
#include "minerd/asic/chip.hpp"
#include "minerd/asic/chip_registry.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace minerd::asic::bm13xx {

struct RegisterWrite {
    std::uint8_t reg;
    std::uint32_t value;
    bool per_chip = false;   ///< write to every chip individually instead of broadcast
};

struct DecodedResponse {
    enum class Kind { Register, Nonce } kind = Kind::Nonce;
    // Register reply
    std::uint8_t chip_address = 0;
    std::uint8_t reg = 0;
    std::uint32_t value = 0;
    // Nonce reply
    std::uint32_t nonce = 0;          ///< header byte order (as written into the header nonce field)
    std::uint8_t chip_job_id = 0;     ///< same encoding as the job-id byte sent in the job frame
    std::uint8_t subcore = 0;
    std::uint8_t excess_difficulty = 0;
    std::uint32_t version_bits = 0;   ///< rolled bits already in header position
};

struct Bm13xxVariant {
    ChipTraits traits;

    // Register map
    std::uint8_t reg_chip_id = 0x00;
    std::uint8_t reg_pll0 = 0x08;
    std::uint8_t reg_hash_counting = 0x10;
    std::uint8_t reg_ticket_mask = 0x14;
    std::uint8_t reg_version_mask = 0xA4;
    PllLimits pll;

    std::size_t response_len = 11;
    unsigned job_id_count = 16;       ///< distinct job ids the chip accepts
    unsigned job_id_step = 8;         ///< job id increment (job ids are multiples of this)
    /// Multiplier on the "exhaust 2^32 nonces" time for the dispatcher interval.
    /// Chips that roll versions themselves keep busy on one job for many sweeps.
    double work_interval_factor = 1.0;
    /// Version-mask (MIDSTATE_CONFIG) writes before / after chip enumeration.
    unsigned version_mask_writes_before_enum = 3;
    unsigned version_mask_writes_after_enum = 0;
    /// Verify the CRC5 of every response frame (drop corrupted frames).
    bool check_response_crc = true;

    /// Broadcast after addressing, before ticket mask/PLL.
    std::vector<RegisterWrite> init_registers;

    // Codecs ----------------------------------------------------------------
    /// Job payload (without 55 AA/header/len/crc) for one WorkItem.
    std::function<Bytes(const work::WorkItem&, std::uint8_t chip_job_id)> encode_job;
    /// Decodes one fixed-length response frame (starting with AA 55).
    std::function<std::optional<DecodedResponse>(std::span<const std::uint8_t>)> decode_response;
    /// Version-mask register value for a BIP310 mask.
    std::function<std::uint32_t(std::uint32_t mask)> version_mask_value;
    /// HASH_COUNTING_NUMBER value for a chain of `chips` chips at `freq_mhz`.
    std::function<std::uint32_t(unsigned chips, double freq_mhz)> hash_counting_value;
    /// Fast UART: register/value pair for a baud rate, if known.
    std::function<std::optional<RegisterWrite>(unsigned baud)> fast_uart;
};

const Bm13xxVariant& bm1362();
const Bm13xxVariant& bm1366();
const std::vector<const Bm13xxVariant*>& builtin_variants();

std::unique_ptr<IAsicChain> make_chain(const Bm13xxVariant& variant, ChainContext ctx);

// Shared codec helpers for chips that use the full (82-byte) job format.
Bytes encode_job_bm1366_style(const work::WorkItem& w, std::uint8_t chip_job_id);
/// 11-byte response decoder. `job_byte_to_id` maps the response's result-header
/// byte to the job-id byte used in the job frame (chip specific).
std::optional<DecodedResponse> decode_response_full(std::span<const std::uint8_t> frame,
                                                    std::uint8_t (*job_byte_to_id)(std::uint8_t),
                                                    std::uint8_t subcore_mask);
std::optional<DecodedResponse> decode_response_bm1366_style(std::span<const std::uint8_t> frame);
std::uint32_t version_mask_value_bm1366_style(std::uint32_t mask);
/// Chip address from the nonce (BM1362/BM1366): address bits 7-1 are nonce bits
/// 24-18 when the wire bytes are read MSB-first.
std::uint8_t chip_address_from_nonce(std::uint32_t header_nonce);

} // namespace minerd::asic::bm13xx

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
    std::uint32_t nonce = 0;
    std::uint8_t chip_job_id = 0;
    std::uint8_t midstate_index = 0;
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
    double work_interval_factor = 1.0;

    /// Broadcast after addressing, before ticket mask/PLL.
    std::vector<RegisterWrite> init_registers;

    // Codecs ----------------------------------------------------------------
    /// Job payload (without 55 AA/header/len/crc) for one WorkItem.
    std::function<Bytes(const work::WorkItem&, std::uint8_t chip_job_id)> encode_job;
    /// Decodes one fixed-length response frame (starting with AA 55).
    std::function<std::optional<DecodedResponse>(std::span<const std::uint8_t>)> decode_response;
    /// Version-mask register value for a BIP310 mask.
    std::function<std::uint32_t(std::uint32_t mask)> version_mask_value;
    /// Hash-counting register value for a chain of `chips` chips (nonce range split).
    std::function<std::uint32_t(unsigned chips)> hash_counting_value;
    /// Fast UART: register/value pair for a baud rate, if known.
    std::function<std::optional<RegisterWrite>(unsigned baud)> fast_uart;
};

const Bm13xxVariant& bm1362();
const Bm13xxVariant& bm1366();
const std::vector<const Bm13xxVariant*>& builtin_variants();

std::unique_ptr<IAsicChain> make_chain(const Bm13xxVariant& variant, ChainContext ctx);

// Shared codec helpers for chips that use the BM1366-style job/response layout.
Bytes encode_job_bm1366_style(const work::WorkItem& w, std::uint8_t chip_job_id);
std::optional<DecodedResponse> decode_response_bm1366_style(std::span<const std::uint8_t> frame);
std::uint32_t version_mask_value_bm1366_style(std::uint32_t mask);

} // namespace minerd::asic::bm13xx

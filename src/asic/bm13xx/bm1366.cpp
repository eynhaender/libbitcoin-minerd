// BM1366 (Antminer S19 XP / S19k Pro, Bitaxe Ultra).
//
// The register values below follow the public single-chip bring-up in
// ESP-Miner (bitaxeorg). They are the reference for the BM1366-style codec
// that BM1362 currently reuses. Multi-chip Antminer chains may need additional
// writes (I/O driver strength per chip, domain-specific settings) – confirm
// with a UART capture of stock firmware before trusting them on 3 kW hardware.

#include "minerd/asic/bm13xx/variants.hpp"

#include <cstring>

namespace minerd::asic::bm13xx {

Bytes encode_job_bm1366_style(const work::WorkItem& w, std::uint8_t chip_job_id) {
    // Layout: job_id | num_midstates | starting_nonce[4] | nbits[4] | ntime[4]
    //         | merkle_root[32] | prev_hash[32] | version[4]           (82 bytes)
    Bytes p(82, 0);
    p[0] = chip_job_id;
    p[1] = 0x01;  // chip derives midstates itself
    // starting nonce = 0 (bytes 2..5)
    std::memcpy(p.data() + 6, w.header.data() + work::header::kNbits, 4);   // LE, as in header
    std::memcpy(p.data() + 10, w.header.data() + work::header::kNtime, 4);  // LE, as in header
    std::memcpy(p.data() + 14, w.header.data() + work::header::kMerkleRoot, 32);  // internal order
    // Previous hash in Stratum word order (each 32-bit word byte-swapped).
    const std::uint8_t* prev = w.header.data() + work::header::kPrevHash;
    for (int i = 0; i < 8; ++i)
        for (int b = 0; b < 4; ++b) p[std::size_t(46 + 4 * i + b)] = prev[4 * i + (3 - b)];
    std::memcpy(p.data() + 78, w.header.data() + work::header::kVersion, 4);  // LE
    return p;
}

std::optional<DecodedResponse> decode_response_bm1366_style(std::span<const std::uint8_t> f) {
    // AA 55 | v0 v1 v2 v3 | b6 | b7 | b8 b9 | flags/crc
    if (f.size() < 11 || f[0] != 0xAA || f[1] != 0x55) return std::nullopt;
    DecodedResponse r;
    const bool is_nonce = (f[10] & 0x80) != 0;  // UNVERIFIED for BM1362: type flag in last byte
    if (!is_nonce) {
        r.kind = DecodedResponse::Kind::Register;
        r.value = read_be32(f.data() + 2);
        r.chip_address = f[6];
        r.reg = f[7];
        return r;
    }
    r.kind = DecodedResponse::Kind::Nonce;
    r.nonce = read_le32(f.data() + 2);
    r.midstate_index = f[6];
    r.chip_job_id = std::uint8_t((f[7] & 0xF0) >> 1);
    r.version_bits = std::uint32_t((std::uint32_t(f[8]) << 8) | f[9]) << 13;
    // Chip address is encoded in the nonce's top bits (nonce space is split by address).
    r.chip_address = std::uint8_t(bswap32(r.nonce) >> 24);  // UNVERIFIED bit position
    return r;
}

std::uint32_t version_mask_value_bm1366_style(std::uint32_t mask) {
    return 0x90000000u | ((mask >> 13) & 0xFFFFu);
}

namespace {
Bm13xxVariant build_bm1366() {
    Bm13xxVariant v;
    v.traits.name = "BM1366";
    v.traits.chip_id = 0x1366;
    v.traits.hashes_per_clock = 894;      // 112 cores x 8 small cores (ESP-Miner)
    v.traits.work_format = WorkFormat::FullHeader;
    v.traits.version_rolling = true;
    v.traits.freq_min_mhz = 50;
    v.traits.freq_max_mhz = 700;
    v.traits.freq_default_mhz = 485;
    v.traits.chain_voltage_min_v = 11.0;  // sanity clamp only
    v.traits.chain_voltage_max_v = 15.5;
    v.traits.verified = false;            // single-chip only; Antminer chains unverified

    v.response_len = 11;
    v.job_id_count = 16;
    v.job_id_step = 8;
    v.work_interval_factor = 1.0;

    v.init_registers = {
        {0xA8, 0x00070000, false},
        {0x18, 0xFF0FC100, false},   // misc control
        {0x3C, 0x80008540, false},   // core register control
        {0x3C, 0x80008020, false},
        {0x54, 0x00000003, false},   // analog mux
        {0x58, 0x02111111, false},   // I/O driver strength
        {0xA8, 0x000701F0, true},
        {0x18, 0xF000C100, true},
        {0x3C, 0x80008540, true},
        {0x3C, 0x80008020, true},
        {0x3C, 0x800082AA, true},
        {0xB9, 0x00004480, false},
        {0x54, 0x00000002, false},
        {0xB9, 0x00004480, false},
        {0x3C, 0x80008DEE, false},
    };

    v.encode_job = encode_job_bm1366_style;
    v.decode_response = decode_response_bm1366_style;
    v.version_mask_value = version_mask_value_bm1366_style;
    v.hash_counting_value = [](unsigned /*chips*/) -> std::uint32_t { return 0x0000151C; };  // single-chip value
    v.fast_uart = [](unsigned /*baud*/) -> std::optional<RegisterWrite> { return std::nullopt; };  // TODO
    return v;
}
} // namespace

const Bm13xxVariant& bm1366() {
    static const Bm13xxVariant v = build_bm1366();
    return v;
}

const std::vector<const Bm13xxVariant*>& builtin_variants() {
    static const std::vector<const Bm13xxVariant*> all = {&bm1362(), &bm1366()};
    return all;
}

} // namespace minerd::asic::bm13xx

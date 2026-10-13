// BM1366 (Antminer S19 XP / S19k Pro, Bitaxe Ultra).
//
// Register values follow the public single-chip bring-up in ESP-Miner
// (bitaxeorg). Frame, job and response layouts follow Mujina's BM13xx
// reference and are shared with BM1362. Multi-chip Antminer chains may need
// additional writes – confirm with a UART capture of stock firmware before
// trusting this on 3 kW hardware.

#include "minerd/asic/bm13xx/variants.hpp"

#include <cstring>

namespace minerd::asic::bm13xx {

Bytes encode_job_bm1366_style(const work::WorkItem& w, std::uint8_t chip_job_id) {
    // Full job format (BM1362 .. BM1370), 82 bytes (Mujina BM13xx reference):
    //   job_header | num_midstates=1 | starting_nonce[4]=0 | nbits[4] LE | ntime[4] LE
    //   | merkle_root[32] | prev_hash[32] | version[4] LE
    // Both hashes: the header's eight 4-byte words in reversed word order,
    // bytes inside each word unchanged.
    Bytes p(82, 0);
    p[0] = chip_job_id;
    p[1] = 0x01;
    std::memcpy(p.data() + 6, w.header.data() + work::header::kNbits, 4);
    std::memcpy(p.data() + 10, w.header.data() + work::header::kNtime, 4);
    auto put_words_reversed = [&](std::size_t dst, std::size_t src) {
        for (int i = 0; i < 8; ++i)
            std::memcpy(p.data() + dst + 4 * std::size_t(i), w.header.data() + src + 4 * std::size_t(7 - i), 4);
    };
    put_words_reversed(14, work::header::kMerkleRoot);
    put_words_reversed(46, work::header::kPrevHash);
    std::memcpy(p.data() + 78, w.header.data() + work::header::kVersion, 4);
    return p;
}

std::uint8_t chip_address_from_nonce(std::uint32_t header_nonce) {
    return std::uint8_t(((bswap32(header_nonce) >> 18) & 0x7F) << 1);
}

std::optional<DecodedResponse> decode_response_full(std::span<const std::uint8_t> f,
                                                    std::uint8_t (*job_byte_to_id)(std::uint8_t),
                                                    std::uint8_t subcore_mask) {
    // AA 55 | b2 b3 b4 b5 | b6 | b7 | b8 b9 | type(3)/crc5(5)
    if (f.size() < 11 || f[0] != 0xAA || f[1] != 0x55) return std::nullopt;
    DecodedResponse r;
    switch (response_type(f)) {
    case resp::TYPE_REGISTER:
        r.kind = DecodedResponse::Kind::Register;
        r.value = read_be32(f.data() + 2);
        r.chip_address = f[6];
        r.reg = f[7];
        return r;
    case resp::TYPE_NONCE:
        r.kind = DecodedResponse::Kind::Nonce;
        r.nonce = read_le32(f.data() + 2);
        r.excess_difficulty = f[6];
        r.chip_job_id = job_byte_to_id(f[7]);
        r.subcore = std::uint8_t(f[7] & subcore_mask);
        r.version_bits = std::uint32_t((std::uint32_t(f[8]) << 8) | f[9]) << 13;
        r.chip_address = chip_address_from_nonce(r.nonce);
        return r;
    default:
        return std::nullopt;
    }
}

std::optional<DecodedResponse> decode_response_bm1366_style(std::span<const std::uint8_t> f) {
    // BM1366 (ESP-Miner): job ids are multiples of 8 in the job frame; the
    // response carries them in bits 7-4 shifted left by one.
    return decode_response_full(f, [](std::uint8_t b) { return std::uint8_t((b & 0xF0) >> 1); }, 0x0F);
}

std::uint32_t version_mask_value_bm1366_style(std::uint32_t mask) {
    // MIDSTATE_CONFIG: [31] auto [29:28] gen (1 = 8 midstates) [15:0] mask >> 13
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
    v.hash_counting_value = [](unsigned, double) -> std::uint32_t { return 0x0000151C; };  // ESP-Miner single-chip value
    v.fast_uart = [](unsigned /*baud*/) -> std::optional<RegisterWrite> { return std::nullopt; };  // TODO
    v.work_interval_factor = 4096;  // chip rolls 2^16 versions per job (8192 sweeps); refresh at ~half coverage
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

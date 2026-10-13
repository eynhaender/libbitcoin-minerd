#include "check.hpp"
#include "minerd/asic/bm13xx/protocol.hpp"
#include "minerd/asic/bm13xx/variants.hpp"
#include "minerd/hal/sim_hal.hpp"

#include <cmath>
#include <cstring>

using namespace minerd;
using namespace minerd::asic::bm13xx;

TEST(crc5_known_frames) {
    // Classic BM13xx frames published in open-source drivers.
    CHECK_EQ(to_hex(cmd_read_register_all(0x00)), std::string("55aa520500000a"));
    CHECK_EQ(to_hex(cmd_chain_inactive()), std::string("55aa5305000003"));
}

TEST(crc16_ccitt_false_check_value) {
    const std::string s = "123456789";
    CHECK_EQ(crc16_false(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(s.data()), s.size())),
             std::uint16_t(0x29B1));
}

TEST(write_register_frame_layout) {
    const auto f = cmd_write_register(0x04, 0x14, 0x000000FF);
    CHECK_EQ(f.size(), std::size_t(11));
    CHECK_EQ(f[2], std::uint8_t(0x41));  // TYPE_CMD | GROUP_SINGLE | CMD_WRITE
    CHECK_EQ(f[3], std::uint8_t(9));
    CHECK_EQ(f[4], std::uint8_t(0x04));
    CHECK_EQ(f[5], std::uint8_t(0x14));
    CHECK_EQ(f[10], crc5(std::span<const std::uint8_t>(f.data() + 2, 8)));
}

TEST(job_frame_crc16) {
    Bytes payload(82, 0xAB);
    const auto f = frame_job(payload);
    CHECK_EQ(f.size(), std::size_t(88));
    CHECK_EQ(f[3], std::uint8_t(86));
    const std::uint16_t c = crc16_false(std::span<const std::uint8_t>(f.data() + 2, f.size() - 4));
    CHECK_EQ(f[f.size() - 2], std::uint8_t(c >> 8));
    CHECK_EQ(f[f.size() - 1], std::uint8_t(c));
}

TEST(pll_solver_never_overshoots) {
    for (double target : {50.0, 200.0, 485.0, 525.0, 600.0, 650.0}) {
        auto p = solve_pll(target);
        CHECK(p.has_value());
        if (!p) continue;
        CHECK(p->freq_mhz <= target + 1e-9);
        CHECK(target - p->freq_mhz < 6.25);
        CHECK(p->post_div1 >= p->post_div2);
    }
    PllSetting def{160, 2, 5, 2, 200};
    CHECK_EQ(pll_register_value(def), 0x40A00241u);
}

TEST(ticket_mask_bit_reversal) {
    CHECK_EQ(ticket_mask_value(256), 0x000000FFu);
    CHECK_EQ(ticket_mask_value(512), 0x000080FFu);
    CHECK_EQ(ticket_mask_value(300), 0x000000FFu);  // rounded down to 256
    CHECK_EQ(effective_ticket_difficulty(300), 256.0);
}

TEST(response_framer_resyncs_on_garbage) {
    ResponseFramer fr(11);
    const std::uint8_t junk[] = {0x00, 0x13, 0xAA};
    const std::uint8_t frame[] = {0xAA, 0x55, 1, 2, 3, 4, 5, 6, 7, 8, 0x80};
    fr.feed(junk);
    fr.feed(std::span<const std::uint8_t>(frame, 5));
    CHECK(!fr.next().has_value());
    fr.feed(std::span<const std::uint8_t>(frame + 5, 6));
    auto f = fr.next();
    CHECK(f.has_value() && f->size() == 11 && (*f)[2] == 1);
    CHECK_EQ(fr.discarded_bytes(), std::size_t(3));
}

// --- generic chain driver against a scripted chip model ----------------------

namespace {
Bytes register_reply(std::uint32_t value, std::uint8_t addr, std::uint8_t reg) {
    Bytes payload{0, 0, 0, 0, addr, reg, 0, 0};
    write_be32(payload.data(), value);
    return make_response(payload, resp::TYPE_REGISTER);
}
/// BM1366-style nonce reply: job byte carries (job_id_byte << 1) in bits 7-4.
Bytes nonce_reply_bm1366(std::uint32_t nonce, std::uint8_t chip_job_id, std::uint16_t version_bits_shifted) {
    Bytes payload{0, 0, 0, 0, 0, std::uint8_t(chip_job_id << 1), std::uint8_t(version_bits_shifted >> 8),
                  std::uint8_t(version_bits_shifted)};
    write_le32(payload.data(), nonce);
    return make_response(payload, resp::TYPE_NONCE);
}
} // namespace

TEST(bm13xx_chain_enumerates_and_maps_results) {
    constexpr unsigned kChips = 3;
    auto serial = std::make_unique<hal::sim::ScriptedSerial>([](std::span<const std::uint8_t> w, hal::sim::ScriptedSerial& port) {
        // On "read chip-id from all chips", every chip answers.
        if (w.size() == 7 && w[2] == 0x52 && w[5] == 0x00) {
            for (unsigned i = 0; i < kChips; ++i) port.push_rx(register_reply(0x13660000u, 0, 0x00));
        }
    });
    auto* port = serial.get();

    asic::ChainContext ctx;
    ctx.chain_index = 0;
    ctx.serial = std::move(serial);
    auto chain = make_chain(bm1366(), std::move(ctx));

    asic::ChainConfig cfg;
    cfg.expected_chips = kChips;
    cfg.chip_difficulty = 512;
    Status s = chain->init(cfg);
    CHECK(s.is_ok());
    if (!s) { std::printf("  %s\n", s.message().c_str()); return; }
    CHECK_EQ(chain->chip_count(), kChips);
    CHECK_EQ(chain->effective_chip_difficulty(), 512.0);

    // Address assignment frames: 55 AA 40 05 <addr> 00 crc
    unsigned addr_frames = 0;
    for (const auto& f : port->written_frames())
        if (f.size() == 7 && f[2] == 0x40) ++addr_frames;
    CHECK_EQ(addr_frames, kChips);

    work::WorkItem w;
    w.id = 777;
    w.version_mask = 0x1FFFE000;
    work::header::set_version(w.header, 0x20000000);
    CHECK(chain->send_work(w).is_ok());
    const auto last = port->written_frames().back();
    CHECK_EQ(last[2], std::uint8_t(0x21));  // job frame
    CHECK_EQ(last.size(), std::size_t(88));
    const std::uint8_t chip_job_id = last[4];

    port->push_rx(nonce_reply_bm1366(0xCAFEBABE, chip_job_id, 0x0005));
    auto r = chain->poll_result(std::chrono::milliseconds(200));
    CHECK(r.has_value());
    if (r) {
        CHECK_EQ(r->work_id, std::uint64_t(777));
        CHECK_EQ(r->nonce, 0xCAFEBABEu);
        CHECK_EQ(r->version, 0x2000A000u);  // 5 << 13
    }
    chain->shutdown();
    CHECK(!chain->send_work(w).is_ok());
}

TEST(bm13xx_chain_fails_on_wrong_chip_count) {
    auto serial = std::make_unique<hal::sim::ScriptedSerial>([](std::span<const std::uint8_t> w, hal::sim::ScriptedSerial& port) {
        if (w.size() == 7 && w[2] == 0x52) port.push_rx(register_reply(0x13660000u, 0, 0));
    });
    asic::ChainContext ctx;
    ctx.serial = std::move(serial);
    auto chain = make_chain(bm1366(), std::move(ctx));
    asic::ChainConfig cfg;
    cfg.expected_chips = 126;
    CHECK(!chain->init(cfg).is_ok());
}

// --- values from Mujina's BM13xx reference -----------------------------------

TEST(response_crc5_matches_published_frames) {
    const auto nonce = *from_hex("aa556db88ee10104035494");      // BM1362 nonce (S19j Pro capture)
    const auto reg = *from_hex("aa551370000000000000" "10");     // BM1370 CHIP_ID reply
    CHECK(response_crc_valid(nonce));
    CHECK(response_crc_valid(reg));
    CHECK_EQ(response_type(nonce), resp::TYPE_NONCE);
    CHECK_EQ(response_type(reg), resp::TYPE_REGISTER);
    auto bad = nonce;
    bad[5] ^= 0x01;
    CHECK(!response_crc_valid(bad));
    // make_response reproduces the published frame byte for byte
    CHECK(make_response(std::span<const std::uint8_t>(nonce.data() + 2, 8), resp::TYPE_NONCE) == nonce);
}

TEST(bm1362_decodes_published_nonce) {
    const auto f = *from_hex("aa556db88ee10104035494");
    auto r = bm1362().decode_response(f);
    CHECK(r.has_value());
    if (!r) return;
    CHECK(r->kind == DecodedResponse::Kind::Nonce);
    CHECK_EQ(r->nonce, 0xE18EB86Du);              // wire bytes 6D B8 8E E1 into the header as-is
    CHECK_EQ(r->chip_address, std::uint8_t(0xDC)); // chip 110 with address step 2
    CHECK_EQ(r->chip_job_id, std::uint8_t(0x00));
    CHECK_EQ(r->subcore, std::uint8_t(4));
    CHECK_EQ(r->excess_difficulty, std::uint8_t(1));
    CHECK_EQ(r->version_bits, 0x006A8000u);       // 0x0354 << 13
}

TEST(bm1362_register_values) {
    const auto& v = bm1362();
    CHECK_EQ(v.version_mask_value(0x1FFFE000), 0x9000FFFFu);   // S19j Pro capture
    auto fu = v.fast_uart(3125000);
    CHECK(fu.has_value() && fu->reg == 0x28 && fu->value == 0x11300000u);
    CHECK(!v.fast_uart(3000000).has_value());                  // not reachable within 2 %
    CHECK_EQ(uart_divider_for(115740).value_or(99), 26u);      // reset baud
    CHECK_EQ(v.traits.expected_ghs(525) * 378 / 1000 > 100, true);
    CHECK_EQ(v.job_id_count, 32u);
    CHECK_EQ(v.hash_counting_value(126, 525), 6242u);
    auto p = solve_pll(525, v.pll);
    CHECK(p && p->fb_div >= 0x10 && p->fb_div <= 0xFA);
    if (p) {
        const double vco = 25.0 * p->fb_div / p->ref_div;
        CHECK(vco >= 2000 && vco <= 3200);
        CHECK((pll_register_value(*p, v.pll) & 0x40000000u) != 0);   // PLLEN
    }
}

TEST(job_encoding_reverses_hash_word_order) {
    work::WorkItem w;
    for (std::size_t i = 0; i < 80; ++i) w.header[i] = std::uint8_t(i);
    const auto p = encode_job_bm1366_style(w, 0x18);
    CHECK_EQ(p.size(), std::size_t(82));
    CHECK_EQ(p[0], std::uint8_t(0x18));
    CHECK_EQ(p[1], std::uint8_t(0x01));
    // merkle root = header bytes 36..67; first transmitted word = header word 7 (bytes 64..67)
    CHECK_EQ(p[14], std::uint8_t(64));
    CHECK_EQ(p[17], std::uint8_t(67));
    CHECK_EQ(p[42], std::uint8_t(36));
    // prev hash = header bytes 4..35; first transmitted word = bytes 32..35
    CHECK_EQ(p[46], std::uint8_t(32));
    CHECK_EQ(p[77], std::uint8_t(7));
    // nbits/ntime/version little-endian as in the header
    CHECK_EQ(p[6], std::uint8_t(72));
    CHECK_EQ(p[10], std::uint8_t(68));
    CHECK_EQ(p[78], std::uint8_t(0));
}

TEST(bm1362_chain_against_scripted_chips) {
    constexpr unsigned kChips = 4;
    auto serial = std::make_unique<hal::sim::ScriptedSerial>([](std::span<const std::uint8_t> w, hal::sim::ScriptedSerial& port) {
        if (w.size() == 7 && w[2] == 0x52 && w[5] == 0x00)
            for (unsigned i = 0; i < kChips; ++i) port.push_rx(register_reply(0x13620300u, 0, 0x00));
    });
    auto* port = serial.get();
    asic::ChainContext ctx;
    ctx.serial = std::move(serial);
    auto chain = make_chain(bm1362(), std::move(ctx));
    asic::ChainConfig cfg;
    cfg.expected_chips = kChips;
    cfg.fast_baud = 3125000;
    Status s = chain->init(cfg);
    CHECK(s.is_ok());
    if (!s) { std::printf("  %s\n", s.message().c_str()); return; }
    CHECK_EQ(port->baud(), 3125000u);

    // Version mask: 1 write before the chip-id read, 2 after (capture order).
    const auto frames = port->written_frames();
    int before = 0, after = 0;
    bool seen_read = false;
    for (const auto& f : frames) {
        if (f.size() == 7 && f[2] == 0x52) seen_read = true;
        if (f.size() == 11 && f[2] == 0x51 && f[5] == 0xA4) (seen_read ? after : before)++;
    }
    CHECK_EQ(before, 1);
    CHECK_EQ(after, 2);

    work::WorkItem w;
    w.id = 4242;
    w.version_mask = 0x1FFFE000;
    work::header::set_version(w.header, 0x20000000);
    for (int i = 0; i < 20; ++i) CHECK(chain->send_work(w).is_ok());  // walk into job ids > 15
    const std::uint8_t job_byte = port->written_frames().back()[4];
    CHECK((job_byte & 0x07) == 0);
    CHECK(job_byte >= 0x80);  // 5-bit job id space in use

    // Nonce from chip address 0x02 (chip 1 with step 64 for 4 chips -> clamps), subcore 3
    Bytes payload{0, 0, 0, 0, 0x02, std::uint8_t(job_byte | 0x03), 0x00, 0x01};
    write_le32(payload.data(), 0x12345678);
    auto good = make_response(payload, resp::TYPE_NONCE);
    auto corrupt = good;
    corrupt[3] ^= 0xFF;
    port->push_rx(corrupt);  // must be dropped by the CRC check
    port->push_rx(good);
    auto r = chain->poll_result(std::chrono::milliseconds(200));
    CHECK(r.has_value());
    if (r) {
        CHECK_EQ(r->work_id, std::uint64_t(4242));
        CHECK_EQ(r->nonce, 0x12345678u);
        CHECK_EQ(r->version, 0x20002000u);
    }
    CHECK(!chain->poll_result(std::chrono::milliseconds(50)).has_value());
}

TEST_MAIN()

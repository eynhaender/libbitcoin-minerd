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
    Bytes r{0xAA, 0x55, 0, 0, 0, 0, addr, reg, 0, 0, 0x00};
    write_be32(r.data() + 2, value);
    return r;
}
Bytes nonce_reply(std::uint32_t nonce, std::uint8_t chip_job_id, std::uint16_t version_bits_shifted) {
    Bytes r{0xAA, 0x55, 0, 0, 0, 0, 0, std::uint8_t(chip_job_id << 1), std::uint8_t(version_bits_shifted >> 8),
            std::uint8_t(version_bits_shifted), 0x80};
    write_le32(r.data() + 2, nonce);
    return r;
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

    port->push_rx(nonce_reply(0xCAFEBABE, chip_job_id, 0x0005));
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

TEST_MAIN()

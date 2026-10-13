// End-to-end: local pool -> work generator -> simulated chains -> host
// verification -> share submission, plus HW-error detection and autotuning.

#include "check.hpp"
#include "minerd/asic/chip_registry.hpp"
#include "minerd/board/platform.hpp"
#include "minerd/control/miner.hpp"
#include "minerd/util/log.hpp"

#include <thread>

using namespace minerd;
using namespace std::chrono_literals;

static board::BoardProfile sim_profile(double hw_error_rate) {
    IniConfig ini;
    const std::string text =
        "[board]\nplatform=sim\n[psu]\ntype=sim\n"
        "[chain.0]\nchip=SIM\nchips=2\nregulator=sim\nchip_difficulty=0.00003\nsim_max_mhs=2\nsim_hw_error_rate=" +
        std::to_string(hw_error_rate) +
        "\n"
        "[chain.1]\nchip=SIM\nchips=2\nregulator=sim\nchip_difficulty=0.00003\nsim_max_mhs=2\n"
        "[fan.0]\ntype=sim\n"
        "[safety]\nmin_fan_rpm=100\n"
        "[pool]\nurl=local-test\nsuggest_difficulty=0.00006\n"
        "[api]\nport=0\n";
    (void)IniConfig::parse(text, ini);
    board::BoardProfile p;
    (void)board::load_profile(ini, p);
    return p;
}

TEST(end_to_end_shares_are_found_and_accepted) {
    log::set_level(log::Level::warn);
    auto p = sim_profile(0.2);
    asic::ChipRegistry reg;
    asic::register_builtin_chips(reg);
    board::Hardware hw;
    CHECK(board::build_hardware(p, reg, hw).is_ok());
    Status ps;
    auto pool = pool::make_pool_client(p.pool, ps);
    CHECK(ps.is_ok());

    control::Miner miner(p, std::move(hw), std::move(pool));
    CHECK(miner.start().is_ok());
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    control::MinerStats st;
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(500ms);
        miner.heartbeat();
        st = miner.stats();
        if (st.accepted >= 5 && st.hw_errors >= 2 && st.chains.size() == 2) break;
    }
    miner.stop();
    std::printf("  accepted=%llu rejected=%llu hw=%llu nonces=%llu ghs=%.6f\n", (unsigned long long)st.accepted,
                (unsigned long long)st.rejected, (unsigned long long)st.hw_errors, (unsigned long long)st.nonces,
                st.hashrate_ghs_1m);
    CHECK(st.accepted >= 5);
    CHECK_EQ(st.rejected, std::uint64_t(0));  // host verification must filter every corrupted nonce
    CHECK(st.hw_errors >= 2);
    CHECK(!st.tripped);
    CHECK_EQ(st.chains.size(), std::size_t(2));
}

TEST(tuner_backs_off_noisy_chips_and_raises_clean_ones) {
    board::TuningProfile t;
    t.step_mhz = 10;
    t.max_hw_error_rate = 0.01;
    t.min_temp_margin_c = 5;
    board::SafetyLimits l;
    l.max_chip_temp_c = 85;
    control::HwErrorStepTuner tuner(t, l, 10);
    std::vector<control::ChipSample> s = {
        {0, 0, 500, 50, 700, 1000, 50},  // 4.8 % errors -> down
        {0, 1, 500, 50, 700, 1000, 0},   // clean -> up
        {0, 2, 500, 50, 700, 3, 0},      // too few samples -> unchanged
        {0, 3, 500, 50, 700, 0, 0},      // silent chip -> down
    };
    auto ch = tuner.step(s, 60.0);
    CHECK_EQ(ch.size(), std::size_t(3));
    for (const auto& c : ch) {
        if (c.chip == 0) CHECK_EQ(c.new_mhz, 490.0);
        if (c.chip == 1) CHECK_EQ(c.new_mhz, 510.0);
        if (c.chip == 3) CHECK_EQ(c.new_mhz, 490.0);
        CHECK(c.chip != 2);
    }
    // No thermal headroom -> clean chip is not raised.
    auto hot = tuner.step(s, 82.0);
    for (const auto& c : hot) CHECK(c.chip != 1);
}

TEST_MAIN()

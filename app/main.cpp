// minerd – open ASIC miner control daemon.
//
//   minerd --profile profiles/sim.ini                  # offline simulation
//   minerd --profile profiles/s19jpro_beaglebone.ini
//          --pool stratum+tcp://192.168.1.10:3333 --user bc1q...worker1

#include "minerd/api/cgminer_api.hpp"
#include "minerd/asic/chip_registry.hpp"
#include "minerd/board/platform.hpp"
#include "minerd/control/miner.hpp"
#include "minerd/util/log.hpp"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>

using namespace minerd;

namespace {
volatile std::sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }

void usage() {
    std::puts(
        "usage: minerd --profile FILE [options]\n"
        "  --profile FILE     board profile (.ini)\n"
        "  --pool URL         override [pool] url (stratum+tcp://host:port | local-test)\n"
        "  --user NAME        override [pool] user\n"
        "  --pass PASS        override [pool] password\n"
        "  --log LEVEL        trace|debug|info|warn|error (default info)\n"
        "  --list-chips       print compiled-in chip drivers and exit\n"
        "  --dry-run          load profile, build hardware objects, exit (no power)\n"
        "  --run-seconds N    stop after N seconds (testing)");
}
} // namespace

int main(int argc, char** argv) {
    std::string profile_path, pool_url, user, pass;
    bool list_chips = false, dry_run = false;
    long run_seconds = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { usage(); std::exit(2); }
            return argv[++i];
        };
        if (a == "--profile") profile_path = next();
        else if (a == "--pool") pool_url = next();
        else if (a == "--user") user = next();
        else if (a == "--pass") pass = next();
        else if (a == "--log") log::set_level(log::parse_level(next()));
        else if (a == "--list-chips") list_chips = true;
        else if (a == "--dry-run") dry_run = true;
        else if (a == "--run-seconds") run_seconds = std::stol(next());
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 2; }
    }

    asic::ChipRegistry chips;
    asic::register_builtin_chips(chips);

    if (list_chips) {
        for (const auto& n : chips.names()) {
            const auto* t = chips.traits(n);
            std::printf("%-8s id=0x%04x  %.0f-%.0f MHz  ~%.1f GH/s/chip @ %.0f MHz  %s\n", n.c_str(), t->chip_id,
                        t->freq_min_mhz, t->freq_max_mhz, t->expected_ghs(t->freq_default_mhz), t->freq_default_mhz,
                        t->verified ? "verified" : "UNVERIFIED");
        }
        return 0;
    }
    if (profile_path.empty()) { usage(); return 2; }

    board::BoardProfile profile;
    if (Status s = board::load_profile_file(profile_path, profile); !s) {
        log::fatal("main", s.message());
        return 1;
    }
    if (!pool_url.empty()) profile.pool.url = pool_url;
    if (!user.empty()) profile.pool.user = user;
    if (!pass.empty()) profile.pool.password = pass;
    log::info("main", "profile '", profile.name, "' platform=", profile.platform, " chains=", profile.chains.size());

    board::Hardware hw;
    if (Status s = board::build_hardware(profile, chips, hw); !s) {
        log::fatal("main", "hardware: ", s.message());
        return 1;
    }
    if (dry_run) {
        log::info("main", "dry run OK: ", hw.boards.size(), " chains, ", hw.fans.size(), " fans, psu=", hw.psu->name());
        return 0;
    }

    Status ps;
    auto pool = pool::make_pool_client(profile.pool, ps);
    if (!ps) {
        log::fatal("main", ps.message());
        return 1;
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGPIPE, SIG_IGN);

    const std::string api_bind = profile.api_bind;
    const unsigned api_port = profile.api_port;
    control::Miner miner(std::move(profile), std::move(hw), std::move(pool));
    if (Status s = miner.start(); !s) {
        log::fatal("main", s.message());
        return 1;
    }

    api::CgminerApiServer api(api_bind, api_port, [&miner] { return miner.stats(); });
    if (api_port != 0) {
        if (Status s = api.start(); !s) log::warn("main", s.message());
    }

    const auto t0 = std::chrono::steady_clock::now();
    auto last_report = t0;
    while (!g_stop && !miner.tripped()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        miner.heartbeat();
        const auto now = std::chrono::steady_clock::now();
        if (now - last_report >= std::chrono::seconds(10)) {
            last_report = now;
            const auto st = miner.stats();
            log::info("main", "hashrate ", st.hashrate_ghs_1m, " GH/s (expected ", st.expected_ghs, ") A/R/S ",
                      st.accepted, "/", st.rejected, "/", st.stale, " HW ", st.hw_errors, " maxT ",
                      st.max_temp_c ? std::to_string(*st.max_temp_c) : std::string("n/a"), " fan ", st.fan_duty, "%");
        }
        if (run_seconds > 0 && now - t0 >= std::chrono::seconds(run_seconds)) break;
    }

    const bool tripped = miner.tripped();
    const auto final_stats = miner.stats();
    api.stop();
    miner.stop();
    if (tripped) {
        log::fatal("main", "stopped by safety trip: ", final_stats.trip_reason);
        return 3;
    }
    log::info("main", "final: accepted ", final_stats.accepted, " rejected ", final_stats.rejected, " hw errors ",
              final_stats.hw_errors);
    return 0;
}

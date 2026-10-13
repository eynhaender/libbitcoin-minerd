#include "check.hpp"
#include "minerd/api/cgminer_api.hpp"
#include "minerd/asic/chip_registry.hpp"
#include "minerd/board/platform.hpp"
#include "minerd/board/profile.hpp"
#include "minerd/util/json.hpp"

using namespace minerd;

TEST(json_roundtrip) {
    auto v = json::parse(R"({"id":1,"method":"mining.notify","params":["a",[1,2.5,true,null],"é\n"]})");
    CHECK(v.has_value());
    if (!v) return;
    CHECK_EQ((*v)["method"].as_string(), std::string("mining.notify"));
    CHECK_EQ((*v)["params"].at(1).at(1).as_number(), 2.5);
    CHECK((*v)["params"].at(1).at(3).is_null());
    CHECK_EQ((*v)["params"].at(2).as_string(), std::string("\xc3\xa9\n"));
    auto again = json::parse(v->dump());
    CHECK(again && again->dump() == v->dump());
}

TEST(json_rejects_garbage) {
    CHECK(!json::parse("{\"a\":}"));
    CHECK(!json::parse("[1,2"));
    CHECK(!json::parse("{} trailing"));
    std::string deep(200, '[');
    CHECK(!json::parse(deep));
}

TEST(profile_sim_loads_and_builds) {
    board::BoardProfile p;
    Status s = board::load_profile_file("profiles/sim.ini", p);
    CHECK(s.is_ok());
    if (!s) { std::printf("  %s\n", s.message().c_str()); return; }
    CHECK_EQ(p.chains.size(), std::size_t(3));
    CHECK_EQ(p.chains[0].chip, std::string("SIM"));
    CHECK_EQ(p.chains[0].options.at("sim_max_mhs"), std::string("1.5"));

    asic::ChipRegistry reg;
    asic::register_builtin_chips(reg);
    board::Hardware hw;
    s = board::build_hardware(p, reg, hw);
    CHECK(s.is_ok());
    CHECK_EQ(hw.boards.size(), std::size_t(3));
    CHECK_EQ(hw.fans.size(), std::size_t(2));
}

TEST(all_hardware_profiles_parse) {
    for (const char* f : {"profiles/s19jpro_beaglebone.ini", "profiles/s19jpro_zynq.ini", "profiles/s19jpro_amlogic.ini",
                          "profiles/example_bm1366_s19kpro.ini"}) {
        board::BoardProfile p;
        Status s = board::load_profile_file(f, p);
        CHECK(s.is_ok());
        if (!s) std::printf("  %s: %s\n", f, s.message().c_str());
        CHECK_EQ(p.chains.size(), std::size_t(3));
    }
}

TEST(unverified_chip_is_refused_without_opt_in) {
    IniConfig ini;
    CHECK(IniConfig::parse("[board]\nplatform=linux-generic\n[chain.0]\nchip=BM1362\n", ini).is_ok());
    board::BoardProfile p;
    CHECK(board::load_profile(ini, p).is_ok());
    asic::ChipRegistry reg;
    asic::register_builtin_chips(reg);
    board::Hardware hw;
    Status s = board::build_hardware(p, reg, hw);
    CHECK(!s.is_ok());
    CHECK(s.message().find("not verified") != std::string::npos);
}

TEST(profile_rejects_inconsistent_thermal_limits) {
    IniConfig ini;
    CHECK(IniConfig::parse("[chain.0]\nchip=SIM\n[thermal]\ntarget_temp_c=99\n", ini).is_ok());
    board::BoardProfile p;
    CHECK(!board::load_profile(ini, p).is_ok());
}

TEST(registry_contains_builtin_drivers) {
    asic::ChipRegistry reg;
    asic::register_builtin_chips(reg);
    CHECK(reg.contains("BM1362"));
    CHECK(reg.contains("BM1366"));
    CHECK(reg.contains("SIM"));
    CHECK(!reg.traits("BM1362")->verified);
}

TEST(cgminer_api_summary) {
    control::MinerStats st;
    st.accepted = 5;
    st.hashrate_ghs_1m = 100;
    auto v = json::parse(api::handle_request("{\"command\":\"summary\"}", st));
    CHECK(v.has_value());
    if (!v) return;
    CHECK_EQ((*v)["STATUS"].at(0)["STATUS"].as_string(), std::string("S"));
    CHECK_EQ((*v)["SUMMARY"].at(0)["Accepted"].as_number(), 5.0);
    CHECK_EQ((*v)["SUMMARY"].at(0)["MHS 1m"].as_number(), 100000.0);
    auto bad = json::parse(api::handle_request("ascset|0,reboot", st));
    CHECK(bad && (*bad)["STATUS"].at(0)["STATUS"].as_string() == "E");
}

TEST_MAIN()

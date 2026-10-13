#include "check.hpp"
#include "minerd/pool/stratum_v1.hpp"
#include "minerd/work/share.hpp"
#include "minerd/work/work_generator.hpp"

#include <cmath>
#include <cstring>

using namespace minerd;

TEST(genesis_block_header_hash) {
    work::header::Header h{};
    work::header::set_version(h, 1);
    auto merkle = reversed(*hash_from_hex("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));
    std::memcpy(h.data() + work::header::kMerkleRoot, merkle.data(), 32);
    work::header::set_ntime(h, 1231006505);
    write_le32(h.data() + work::header::kNbits, 0x1d00ffff);
    work::header::set_nonce(h, 2083236893);
    const auto hash = crypto::sha256d(h);
    CHECK_EQ(to_hex(reversed(hash)), std::string("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"));
    CHECK(work::hash_meets_nbits(hash, 0x1d00ffff));
    CHECK(!work::hash_meets_nbits(hash, 0x1b00ffff));
    CHECK(work::hash_difficulty(hash) > 2000 && work::hash_difficulty(hash) < 3000);
}

TEST(nbits_difficulty_is_one_for_diff1) {
    CHECK(std::fabs(work::nbits_difficulty(0x1d00ffff) - 1.0) < 1e-9);
}

TEST(rolled_version_deposits_bits_into_mask) {
    CHECK_EQ(work::rolled_version(0x20000000, 0x1FFFE000, 0), 0x20000000u);
    CHECK_EQ(work::rolled_version(0x20000000, 0x1FFFE000, 1), 0x20002000u);
    CHECK_EQ(work::rolled_version(0x20000000, 0x1FFFE000, 5), 0x2000A000u);
    CHECK_EQ(work::rolled_version(0x20000000, 0x00000000, 7), 0x20000000u);
}

static json::Value notify_params() {
    using json::Array;
    using json::Value;
    return Value(Array{
        Value("job42"),
        Value("03020100070605040b0a09080f0e0d0c13121110171615141b1a19181f1e1d1c"),
        Value("01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff20"),
        Value("ffffffff0100f2052a01000000"),
        Value(Array{Value("0a325ca303eb3014c43ae004970f343634db176fa1697bcc8c9efac94626488d"),
                    Value("f17821c6417e73b24157c2ec80e527b32a9e3d7036940cf28ba5307dba67e28c")}),
        Value("20000000"), Value("1705ae3a"), Value("64f0c1a2"), Value(true)});
}

TEST(stratum_notify_to_header_matches_reference) {
    pool::StratumSession sess;
    sess.extranonce1 = *from_hex("f8002c90");
    sess.extranonce2_size = 4;
    sess.version_mask = 0x1FFFE000;
    sess.difficulty = 8;
    std::string err;
    auto job = pool::parse_notify(notify_params(), sess, &err);
    CHECK(job.has_value());
    if (!job) { std::printf("  %s\n", err.c_str()); return; }
    CHECK_EQ(job->job_id, std::string("job42"));
    CHECK(job->clean_jobs);
    CHECK_EQ(job->pool_difficulty, 8.0);

    const Bytes en2 = *from_hex("00000002");
    CHECK_EQ(to_hex(work::merkle_root(*job, en2)),
             std::string("a3ab0125e71a93f7d0681840c79299443e46937fefbfea4a43a54be504d442e2"));
    const auto hdr = work::build_header(*job, en2, job->ntime, work::rolled_version(job->version, job->version_mask, 5), 0x12345678);
    CHECK_EQ(to_hex(hdr), std::string("00a00020000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1fa3ab0125e71a93"
                                      "f7d0681840c79299443e46937fefbfea4a43a54be504d442e2a2c1f0643aae051778563412"));
    CHECK_EQ(to_hex(crypto::sha256d(hdr)), std::string("1b9f425a6599faf6d449d8b250397bb71ecc22ce857eddb08b3c9ca784ddc618"));
}

TEST(work_generator_rolls_extranonce2_and_midstates) {
    pool::StratumSession sess;
    sess.extranonce1 = *from_hex("f8002c90");
    sess.version_mask = 0x1FFFE000;
    auto job = std::make_shared<work::Job>(*pool::parse_notify(notify_params(), sess));
    work::WorkGenerator gen(job);
    auto a = gen.next(1);
    auto b = gen.next(2, 4);
    CHECK(a.extranonce2 != b.extranonce2);
    CHECK(a.header != b.header);  // different merkle roots
    CHECK_EQ(b.midstates.size(), std::size_t(4));
    CHECK_EQ(b.midstate_versions[1], 0x20002000u);
    // midstate[1] must equal the midstate of the header with version 1 rolled in
    auto h = b.header;
    work::header::set_version(h, b.midstate_versions[1]);
    CHECK(crypto::header_midstate(h.data()).h == b.midstates[1].h);
}

TEST(submit_params_format) {
    pool::Share sh;
    sh.job_id = "job42";
    sh.extranonce2 = {0, 0, 0, 2};
    sh.ntime = 0x64f0c1a2;
    sh.nonce = 0x12345678;
    sh.version = 0x2000A000;
    sh.version_mask = 0x1FFFE000;
    CHECK_EQ(pool::submit_params("w1", sh).dump(),
             std::string(R"(["w1","job42","00000002","64f0c1a2","12345678","0000a000"])"));
}

TEST(stratum_url_parsing) {
    auto u = pool::parse_stratum_url("stratum+tcp://pool.example.org:3333");
    CHECK(u && u->host == "pool.example.org" && u->port == 3333);
    auto v6 = pool::parse_stratum_url("stratum+tcp://[::1]:3333");
    CHECK(v6 && v6->host == "::1");
    CHECK(!pool::parse_stratum_url("stratum+ssl://x:1"));
    CHECK(!pool::parse_stratum_url("stratum+tcp://nohost"));
}

TEST_MAIN()

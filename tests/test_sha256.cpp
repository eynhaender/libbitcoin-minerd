#include "check.hpp"
#include "minerd/crypto/sha256.hpp"

#include <string>

using namespace minerd;

static std::span<const std::uint8_t> s(const std::string& str) {
    return {reinterpret_cast<const std::uint8_t*>(str.data()), str.size()};
}

TEST(sha256_empty) {
    CHECK_EQ(to_hex(crypto::sha256(s(""))), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

TEST(sha256_abc) {
    CHECK_EQ(to_hex(crypto::sha256(s("abc"))), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

TEST(sha256_two_blocks) {
    CHECK_EQ(to_hex(crypto::sha256(s("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

TEST(sha256_lengths_around_padding_boundary) {
    // 55/56/63/64/65 bytes exercise one- vs two-block padding.
    const std::string expected[] = {
        "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318",  // 55 x 'a'
        "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a",  // 56
        "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34",  // 63
        "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb",  // 64
        "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0",  // 65
    };
    const int lens[] = {55, 56, 63, 64, 65};
    for (int i = 0; i < 5; ++i) CHECK_EQ(to_hex(crypto::sha256(s(std::string(std::size_t(lens[i]), 'a')))), expected[i]);
}

TEST(midstate_path_matches_full_hash) {
    std::array<std::uint8_t, 80> hdr{};
    for (std::size_t i = 0; i < hdr.size(); ++i) hdr[i] = std::uint8_t(i * 7 + 3);
    const auto mid = crypto::header_midstate(hdr.data());
    CHECK(crypto::sha256d_header_from_midstate(mid, hdr.data()) == crypto::sha256d(hdr));
}

#ifdef MINERD_HAVE_LIBBITCOIN
TEST(libbitcoin_cross_check) {
    std::array<std::uint8_t, 80> hdr{};
    for (std::size_t i = 0; i < hdr.size(); ++i) hdr[i] = std::uint8_t(i);
    CHECK(crypto::sha256d(hdr) == crypto::sha256d_libbitcoin(hdr));
}
#endif

TEST_MAIN()

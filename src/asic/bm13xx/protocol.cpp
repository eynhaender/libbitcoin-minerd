#include "minerd/asic/bm13xx/protocol.hpp"

#include <algorithm>
#include <cmath>

namespace minerd::asic::bm13xx {

std::uint8_t crc5(std::span<const std::uint8_t> data) {
    std::uint8_t crc = 0x1F;
    for (std::uint8_t byte : data) {
        for (int bit = 7; bit >= 0; --bit) {
            const bool din = (byte >> bit) & 1;
            const bool msb = (crc >> 4) & 1;
            crc = std::uint8_t((crc << 1) & 0x1F);
            if (din ^ msb) crc ^= 0x05;
        }
    }
    return crc;
}

std::uint16_t crc16_false(std::span<const std::uint8_t> data) {
    std::uint16_t crc = 0xFFFF;
    for (std::uint8_t byte : data) {
        crc ^= std::uint16_t(byte) << 8;
        for (int i = 0; i < 8; ++i) crc = (crc & 0x8000) ? std::uint16_t((crc << 1) ^ 0x1021) : std::uint16_t(crc << 1);
    }
    return crc;
}

Bytes frame_command(std::uint8_t header, std::span<const std::uint8_t> data) {
    Bytes f{0x55, 0xAA, header, std::uint8_t(data.size() + 3)};
    f.insert(f.end(), data.begin(), data.end());
    f.push_back(crc5(std::span<const std::uint8_t>(f.data() + 2, f.size() - 2)));
    return f;
}

Bytes frame_job(std::span<const std::uint8_t> data) {
    Bytes f{0x55, 0xAA, std::uint8_t(hdr::TYPE_JOB | hdr::GROUP_SINGLE | hdr::CMD_WRITE), std::uint8_t(data.size() + 4)};
    f.insert(f.end(), data.begin(), data.end());
    const std::uint16_t c = crc16_false(std::span<const std::uint8_t>(f.data() + 2, f.size() - 2));
    f.push_back(std::uint8_t(c >> 8));
    f.push_back(std::uint8_t(c));
    return f;
}

Bytes cmd_chain_inactive() {
    const std::uint8_t d[2] = {0x00, 0x00};
    return frame_command(hdr::TYPE_CMD | hdr::GROUP_ALL | hdr::CMD_INACTIVE, d);
}

Bytes cmd_set_address(std::uint8_t address) {
    const std::uint8_t d[2] = {address, 0x00};
    return frame_command(hdr::TYPE_CMD | hdr::GROUP_SINGLE | hdr::CMD_SETADDRESS, d);
}

Bytes cmd_read_register_all(std::uint8_t reg) {
    const std::uint8_t d[2] = {0x00, reg};
    return frame_command(hdr::TYPE_CMD | hdr::GROUP_ALL | hdr::CMD_READ, d);
}

Bytes cmd_read_register(std::uint8_t address, std::uint8_t reg) {
    const std::uint8_t d[2] = {address, reg};
    return frame_command(hdr::TYPE_CMD | hdr::GROUP_SINGLE | hdr::CMD_READ, d);
}

Bytes cmd_write_register_all(std::uint8_t reg, std::uint32_t v) {
    const std::uint8_t d[6] = {0x00, reg, std::uint8_t(v >> 24), std::uint8_t(v >> 16), std::uint8_t(v >> 8), std::uint8_t(v)};
    return frame_command(hdr::TYPE_CMD | hdr::GROUP_ALL | hdr::CMD_WRITE, d);
}

Bytes cmd_write_register(std::uint8_t address, std::uint8_t reg, std::uint32_t v) {
    const std::uint8_t d[6] = {address, reg, std::uint8_t(v >> 24), std::uint8_t(v >> 16), std::uint8_t(v >> 8), std::uint8_t(v)};
    return frame_command(hdr::TYPE_CMD | hdr::GROUP_SINGLE | hdr::CMD_WRITE, d);
}

std::optional<PllSetting> solve_pll(double target, const PllLimits& L) {
    std::optional<PllSetting> best;
    for (unsigned ref = L.ref_min; ref <= L.ref_max; ++ref) {
        for (unsigned p1 = L.post_min; p1 <= L.post_max; ++p1) {
            for (unsigned p2 = L.post_min; p2 <= p1; ++p2) {  // post1 >= post2 required
                // fb_div that gets closest from below for this divider combination
                const double fb_exact = target * ref * p1 * p2 / L.ref_clock_mhz;
                const unsigned fb = unsigned(std::floor(fb_exact + 1e-9));
                if (fb < L.fb_min || fb > L.fb_max) continue;
                const double vco = L.ref_clock_mhz * fb / ref;
                if (vco < L.vco_min_mhz || vco > L.vco_max_mhz) continue;
                const double f = vco / (p1 * p2);
                if (f > target + 1e-9) continue;
                // prefer closest frequency, then lower VCO (less power)
                if (!best || f > best->freq_mhz + 1e-9 ||
                    (std::fabs(f - best->freq_mhz) < 1e-9 && vco < L.ref_clock_mhz * best->fb_div / best->ref_div)) {
                    best = PllSetting{fb, ref, p1, p2, f};
                }
            }
        }
    }
    return best;
}

std::uint32_t pll_register_value(const PllSetting& s, const PllLimits& L) {
    const double vco = L.ref_clock_mhz * s.fb_div / s.ref_div;
    const std::uint32_t top = (vco >= 2400.0) ? 0x50u : 0x40u;
    return (top << 24) | ((s.fb_div & 0xFFu) << 16) | ((s.ref_div & 0xFFu) << 8) |
           (((s.post_div1 - 1) & 0x0Fu) << 4) | ((s.post_div2 - 1) & 0x0Fu);
}

namespace {
std::uint8_t reverse_bits(std::uint8_t b) {
    b = std::uint8_t((b & 0xF0) >> 4 | (b & 0x0F) << 4);
    b = std::uint8_t((b & 0xCC) >> 2 | (b & 0x33) << 2);
    b = std::uint8_t((b & 0xAA) >> 1 | (b & 0x55) << 1);
    return b;
}
std::uint32_t pow2_floor(double d) {
    if (d < 1) return 1;
    std::uint32_t p = 1;
    while (p <= 0x7FFFFFFFu && double(p) * 2 <= d) p *= 2;
    return p;
}
} // namespace

std::uint32_t ticket_mask_value(double difficulty) {
    const std::uint32_t m = pow2_floor(difficulty) - 1;
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= std::uint32_t(reverse_bits(std::uint8_t(m >> (8 * i)))) << (8 * i);
    return v;
}

double effective_ticket_difficulty(double difficulty) { return double(pow2_floor(difficulty)); }

void ResponseFramer::feed(std::span<const std::uint8_t> bytes) { buf_.insert(buf_.end(), bytes.begin(), bytes.end()); }

std::optional<Bytes> ResponseFramer::next() {
    while (buf_.size() >= 2) {
        if (buf_[0] == 0xAA && buf_[1] == 0x55) {
            if (buf_.size() < frame_len_) return std::nullopt;
            Bytes frame(buf_.begin(), buf_.begin() + std::ptrdiff_t(frame_len_));
            buf_.erase(buf_.begin(), buf_.begin() + std::ptrdiff_t(frame_len_));
            return frame;
        }
        buf_.erase(buf_.begin());
        ++discarded_;
    }
    return std::nullopt;
}

} // namespace minerd::asic::bm13xx

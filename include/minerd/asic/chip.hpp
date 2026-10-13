#pragma once
// ===========================================================================
//  CHIP ABSTRACTION  —  the boundary that lets the daemon move between miners
// ===========================================================================
//
// Everything chip-specific lives behind IAsicChain. The control loop, work
// engine, pool code, autotuner and safety supervisor only see:
//
//   * ChipTraits    – static facts (cores, frequency range, work format, ...)
//   * IAsicChain    – a daisy chain of identical chips on one hashboard
//
// A new miner model is supported by
//   1) writing (or reusing) an IAsicChain implementation for its chip, and
//   2) registering it under a name in the ChipRegistry, and
//   3) writing a board profile (.ini) that references that name.
//
// BM13xx chips (BM1362, BM1366, BM1368, BM1370, BM1397, ...) share one
// transport/enumeration implementation (Bm13xxChain) and differ only in a
// variant table. Non-Bitmain chips implement IAsicChain directly.

#include "minerd/util/status.hpp"
#include "minerd/work/job.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace minerd::asic {

/// How a chip wants to receive work.
enum class WorkFormat {
    FullHeader,   ///< chip gets merkle root/prev hash/etc. and computes midstates itself (BM1366+)
    Midstates,    ///< host computes N midstates over rolled versions (BM1387/BM1397 style)
};

struct ChipTraits {
    std::string name;               ///< "BM1362"
    std::uint16_t chip_id = 0;      ///< value reported in the chip-id register
    /// Hash engines per clock cycle per chip ("small cores"). Expected chip
    /// hashrate [H/s] = hashes_per_clock * f[Hz].
    double hashes_per_clock = 0;
    WorkFormat work_format = WorkFormat::FullHeader;
    unsigned midstates = 0;         ///< for WorkFormat::Midstates
    bool version_rolling = true;    ///< supports BIP310/AsicBoost

    double freq_min_mhz = 0;
    double freq_max_mhz = 0;
    double freq_step_mhz = 6.25;
    double freq_default_mhz = 0;

    /// Typical safe chain operating window (domain voltage x domains). Used as a
    /// sanity clamp; actual targets come from the board profile.
    double chain_voltage_min_v = 0;
    double chain_voltage_max_v = 0;

    /// True if values above were confirmed on real hardware. Unverified chips
    /// log a warning at start-up.
    bool verified = false;

    double expected_ghs(double freq_mhz) const { return hashes_per_clock * freq_mhz / 1000.0; }
};

struct ChainConfig {
    unsigned expected_chips = 0;      ///< 0 = accept whatever is enumerated
    double frequency_mhz = 0;         ///< target frequency; drivers ramp towards it
    std::uint32_t version_mask = 0x1FFFE000;
    double chip_difficulty = 256;     ///< ticket mask difficulty (nonces reported by chips)
    unsigned baud = 115200;           ///< initial baud; drivers may switch to a faster rate
    unsigned fast_baud = 0;           ///< 0 = keep initial baud
};

struct ChipCounters {
    std::uint64_t nonces = 0;         ///< valid nonces (verified on host)
    std::uint64_t hw_errors = 0;      ///< nonces that failed host verification
    double frequency_mhz = 0;
};

struct TemperatureReading {
    std::string source;               ///< "chain0/chip12", "chain0/pcb-left", ...
    double celsius = 0;
};

class IAsicChain {
public:
    virtual ~IAsicChain() = default;

    virtual const ChipTraits& traits() const = 0;

    // --- life cycle ---
    // init() and ramp_frequency() are called before work I/O starts. The
    // setters below may be called from the control/tuning thread while work
    // I/O runs on other threads, so drivers must serialize register access.
    /// Reset, enumerate, address and configure chips; ends at a low safe
    /// frequency. Power must already be applied by the board.
    virtual Status init(const ChainConfig& cfg) = 0;
    /// Ramp all chips gradually to `mhz` (large jumps can brown out the rail).
    virtual Status ramp_frequency(double mhz) = 0;
    virtual Status set_chip_frequency(unsigned chip, double mhz) = 0;
    virtual Status set_chip_difficulty(double difficulty) = 0;
    virtual Status set_version_mask(std::uint32_t mask) = 0;
    /// Stop hashing and put the chips in a quiet state. Must be safe to call
    /// at any time, from any thread, more than once.
    virtual void shutdown() = 0;

    virtual unsigned chip_count() const = 0;
    virtual double chip_frequency(unsigned chip) const = 0;
    /// Difficulty actually programmed into the chips (BM13xx rounds down to a
    /// power of two). Used for hashrate estimation and HW-error detection.
    virtual double effective_chip_difficulty() const = 0;

    // --- work I/O (send_work and poll_result may run on different threads) ---
    virtual Status send_work(const work::WorkItem& item) = 0;
    /// Next decoded nonce, or nullopt on timeout. `work_id` is mapped back to
    /// the host WorkItem id by the driver.
    virtual std::optional<work::NonceResult> poll_result(std::chrono::milliseconds timeout) = 0;
    /// How often new work must be fed so chips never idle (depends on frequency,
    /// cores and nonce range). The dispatcher sleeps this long between jobs.
    virtual std::chrono::microseconds work_interval() const = 0;
    /// Number of host WorkItems that may be outstanding (chip job-id space).
    virtual unsigned max_outstanding_work() const = 0;

    // --- optional sensors routed through the chain (e.g. BM13xx I2C passthrough) ---
    virtual std::vector<TemperatureReading> read_temperatures() { return {}; }
};

} // namespace minerd::asic

#ifndef BUPT_VOLUMETRIC_TRANSPORT_SIMULATED_ACCESS_UNIT_H_
#define BUPT_VOLUMETRIC_TRANSPORT_SIMULATED_ACCESS_UNIT_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace bupt::volumetric {

inline constexpr std::size_t kSimulatedAccessUnitHeaderBytes = 36;
inline constexpr std::size_t kMaxSimulatedAccessUnitPayloadBytes = 2'000'000;
inline constexpr std::uint32_t kOctreeSimMagic = 0x4f435453;
inline constexpr std::uint8_t kOctreeSimVersion = 1;

/** One admitted simulated volumetric frame handed to the WebRTC codec. */
struct SimulatedAccessUnit final {
  std::uint64_t frame_id = 0;
  std::uint32_t rtp_timestamp = 0;
  // Sender capture time in microseconds (serialized). The receiver subtracts it
  // from its own monotonic clock to derive the end-to-end block latency. Both
  // ends are the same host for the localhost loopback, so a process-local
  // monotonic clock is NOT shared across processes; QPC/steady time is embedded
  // here so the receiver can measure it.
  std::uint64_t capture_time_us = 0;
  // Send-side only (not serialized): the WebRTC ULPFEC protection factor (Q8)
  // and the semantic class of this block. The receiver reads block structure
  // from the 48-byte OCTREE-SIM wire header (Task 13), not from here.
  std::uint16_t fec_rate = 0;
  std::uint8_t semantic_class = 0;
  std::vector<std::uint8_t> payload;
};

/** Stable reasons why an OCTREE-SIM access unit was rejected. */
enum class SimulatedAccessUnitError : std::uint8_t {
  kNone = 0,
  kTruncatedHeader,
  kBadMagic,
  kUnsupportedVersion,
  kPayloadTooLarge,
  kPayloadLengthMismatch,
  kChecksumMismatch,
};

struct SimulatedAccessUnitParseResult final {
  SimulatedAccessUnitError error = SimulatedAccessUnitError::kNone;
  std::optional<SimulatedAccessUnit> unit;

  [[nodiscard]] bool ok() const noexcept {
    return error == SimulatedAccessUnitError::kNone && unit.has_value();
  }
};

/** Serializes one validated access unit in network byte order. */
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
SerializeSimulatedAccessUnit(const SimulatedAccessUnit& unit);

/** Parses and checksum-validates one complete access unit. */
[[nodiscard]] SimulatedAccessUnitParseResult ParseSimulatedAccessUnit(
    std::span<const std::uint8_t> wire);

/** Small latest-frame-first handoff between the source and encoder threads. */
class SimulatedAccessUnitQueue final {
 public:
  explicit SimulatedAccessUnitQueue(std::size_t capacity);

  void Push(SimulatedAccessUnit unit);
  [[nodiscard]] std::optional<SimulatedAccessUnit> Pop(
      std::uint32_t rtp_timestamp);
  // Pops the oldest queued unit (FIFO). WebRTC reassigns RTP timestamps, so the
  // encoder cannot match on the application timestamp in the real media path.
  [[nodiscard]] std::optional<SimulatedAccessUnit> PopOldest();
  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] std::uint64_t evicted_count() const;

 private:
  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::deque<SimulatedAccessUnit> units_;
  std::uint64_t evicted_count_ = 0;
};

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_TRANSPORT_SIMULATED_ACCESS_UNIT_H_

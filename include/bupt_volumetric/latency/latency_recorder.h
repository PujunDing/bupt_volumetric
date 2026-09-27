#ifndef BUPT_VOLUMETRIC_LATENCY_LATENCY_RECORDER_H_
#define BUPT_VOLUMETRIC_LATENCY_LATENCY_RECORDER_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace bupt::volumetric {

// Quantile summary for a set of latency samples (all in milliseconds).
struct LatencyStats final {
  std::uint64_t count = 0;
  double min_ms = 0;
  double p50_ms = 0;
  double p90_ms = 0;
  double p99_ms = 0;
  double max_ms = 0;
};

// Bounded accumulator of latency samples (milliseconds). Samples are non-
// negative; NaN/negative values are rejected. When full, the oldest sample is
// dropped so the summary always reflects the most recent window. Not thread-
// safe by itself; the caller serializes access.
class LatencyRecorder final {
 public:
  explicit LatencyRecorder(std::size_t max_samples = 4096);

  void Record(double latency_ms) noexcept;

  [[nodiscard]] LatencyStats stats() const noexcept;
  [[nodiscard]] std::uint64_t dropped_count() const noexcept;

  // Compact human-readable overlay line: "latency p50=12.3 p90=15.2 p99=18.0 ms
  // (n=30)", or "latency n/a" when no samples exist.
  [[nodiscard]] std::string FormatOverlay() const;

 private:
  std::size_t max_samples_;
  std::deque<double> samples_;
  std::uint64_t dropped_count_ = 0;
};

// Thread-safe FIFO of raw per-block latency samples (microseconds). The decoder
// pushes one sample per completed block; the receive-side loop drains it. This
// captures every block's latency instead of a single "latest" value that gets
// overwritten when blocks complete in bursts.
class LatencySampleQueue final {
 public:
  void Push(std::uint64_t latency_us);
  [[nodiscard]] std::vector<std::uint64_t> Drain();
  [[nodiscard]] std::size_t size() const;

 private:
  mutable std::mutex mutex_;
  std::deque<std::uint64_t> samples_;
};

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_LATENCY_LATENCY_RECORDER_H_

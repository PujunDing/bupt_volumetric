#ifndef BUPT_VOLUMETRIC_LATENCY_LATENCY_CLOCK_H_
#define BUPT_VOLUMETRIC_LATENCY_LATENCY_CLOCK_H_

#include <cstdint>

namespace bupt::volumetric {

// Monotonic clock for local duration measurement. It is single-process
// monotonic (steady_clock); it must never be used to compare timestamps
// produced by another process — cross-host capture timestamps need the
// clock-sync estimator, which is out of scope for the same-host loopback.
class LatencyClock final {
 public:
  LatencyClock() = default;

  // Microseconds since an arbitrary, fixed epoch within this process.
  [[nodiscard]] std::uint64_t NowUs() const noexcept;
};

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_LATENCY_LATENCY_CLOCK_H_

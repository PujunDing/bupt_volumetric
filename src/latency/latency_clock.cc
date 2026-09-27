#include "bupt_volumetric/latency/latency_clock.h"

#include <chrono>
#include <cstdint>

namespace bupt::volumetric {

std::uint64_t LatencyClock::NowUs() const noexcept {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

}  // namespace bupt::volumetric

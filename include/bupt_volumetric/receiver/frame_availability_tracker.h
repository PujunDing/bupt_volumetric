#ifndef BUPT_VOLUMETRIC_RECEIVER_FRAME_AVAILABILITY_TRACKER_H_
#define BUPT_VOLUMETRIC_RECEIVER_FRAME_AVAILABILITY_TRACKER_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>

#include "bupt_volumetric/model/semantic_class.h"

namespace bupt::volumetric {

struct FrameBlockTargets final {
  std::size_t p0_blocks = 0;
  std::size_t p1_blocks = 0;
  std::size_t p2_blocks = 0;
};

enum class FrameAvailability : std::uint8_t {
  kUnavailable = 0,
  kP0Usable,
  kP0P1Usable,
  kComplete,
};

/** Tracks monotonic partial quality independently for every frame. */
class FrameAvailabilityTracker final {
public:
  explicit FrameAvailabilityTracker(FrameBlockTargets targets) noexcept;

  /** Records a completed block and returns the frame's monotonic state. */
  [[nodiscard]] FrameAvailability Observe(std::uint64_t frame_id,
                                          SemanticClass semantic_class,
                                          std::uint32_t block_id);

private:
  struct FrameState final {
    std::set<std::uint32_t> p0;
    std::set<std::uint32_t> p1;
    std::set<std::uint32_t> p2;
  };

  FrameBlockTargets targets_;
  std::map<std::uint64_t, FrameState> frames_;
};

} // namespace bupt::volumetric

#endif // BUPT_VOLUMETRIC_RECEIVER_FRAME_AVAILABILITY_TRACKER_H_

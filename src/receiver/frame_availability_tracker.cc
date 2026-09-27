#include "bupt_volumetric/receiver/frame_availability_tracker.h"

#include <cstdint>

#include "bupt_volumetric/model/semantic_class.h"

namespace bupt::volumetric {

FrameAvailabilityTracker::FrameAvailabilityTracker(
    FrameBlockTargets targets) noexcept
    : targets_(targets) {}

FrameAvailability FrameAvailabilityTracker::Observe(
    std::uint64_t frame_id, SemanticClass semantic_class,
    std::uint32_t block_id) {
  FrameState &state = frames_[frame_id];
  switch (semantic_class) {
  case SemanticClass::kP0:
    state.p0.insert(block_id);
    break;
  case SemanticClass::kP1:
    state.p1.insert(block_id);
    break;
  case SemanticClass::kP2:
    state.p2.insert(block_id);
    break;
  }

  if (state.p0.size() < targets_.p0_blocks) {
    return FrameAvailability::kUnavailable;
  }
  if (state.p1.size() < targets_.p1_blocks) {
    return FrameAvailability::kP0Usable;
  }
  if (state.p2.size() < targets_.p2_blocks) {
    return FrameAvailability::kP0P1Usable;
  }
  return FrameAvailability::kComplete;
}

} // namespace bupt::volumetric

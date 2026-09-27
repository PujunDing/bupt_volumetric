#include "bupt_volumetric/model/simulated_octree_frame.h"

#include <utility>

namespace bupt::volumetric {

SimulatedOctreeFrame::SimulatedOctreeFrame(std::uint64_t frame_id,
                                           Payload payload)
    : frame_id_(frame_id), payload_(std::move(payload)) {}

std::uint64_t SimulatedOctreeFrame::frame_id() const noexcept {
  return frame_id_;
}

const SimulatedOctreeFrame::Payload &
SimulatedOctreeFrame::payload() const noexcept {
  return payload_;
}

} // namespace bupt::volumetric

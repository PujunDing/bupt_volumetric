#ifndef BUPT_VOLUMETRIC_TRANSPORT_ACCESS_UNIT_PLANNER_H_
#define BUPT_VOLUMETRIC_TRANSPORT_ACCESS_UNIT_PLANNER_H_

#include <cstdint>
#include <vector>

#include "bupt_volumetric/model/simulated_octree_frame.h"
#include "bupt_volumetric/transport/simulated_access_unit.h"

namespace bupt::volumetric {

// Partitions, plans, and admits one simulated frame into per-block access units.
// Each returned unit carries the admitted block's source bytes, its semantic
// class, and its WebRTC ULPFEC protection factor (Q8). Blocks that exceed the
// byte budget are dropped by the semantic admission controller.
[[nodiscard]] std::vector<SimulatedAccessUnit> PlanAdmittedAccessUnits(
    const SimulatedOctreeFrame& frame,
    std::uint32_t rtp_timestamp,
    std::uint64_t available_media_bytes);

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_TRANSPORT_ACCESS_UNIT_PLANNER_H_

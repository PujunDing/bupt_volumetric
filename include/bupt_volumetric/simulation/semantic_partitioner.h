#ifndef BUPT_VOLUMETRIC_SIMULATION_SEMANTIC_PARTITIONER_H_
#define BUPT_VOLUMETRIC_SIMULATION_SEMANTIC_PARTITIONER_H_

#include <vector>

#include "bupt_volumetric/model/semantic_region.h"
#include "bupt_volumetric/model/simulated_octree_frame.h"

namespace bupt::volumetric {

// Applies the first prototype's fixed 1/8, 3/8, 4/8 semantic layout.
class SemanticPartitioner final {
public:
  /**
   * Splits one fixed-profile frame into ordered P0, P1, and P2 ranges.
   *
   * @param frame Simulated frame whose payload is described by the ranges.
   * @return Three contiguous descriptors covering the payload exactly once.
   * @throws std::invalid_argument if the payload is not exactly 2,000,000
   *         bytes, because the fixed prototype profile would reference bytes
   *         outside or leave bytes outside the declared semantic ranges.
   *
   * The returned regions are descriptors only and never copy source bytes.
   */
  [[nodiscard]] std::vector<SemanticRegion>
  Partition(const SimulatedOctreeFrame &frame) const;
};

} // namespace bupt::volumetric

#endif // BUPT_VOLUMETRIC_SIMULATION_SEMANTIC_PARTITIONER_H_

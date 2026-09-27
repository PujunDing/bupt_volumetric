#ifndef BUPT_VOLUMETRIC_SIMULATION_FRAME_GENERATOR_H_
#define BUPT_VOLUMETRIC_SIMULATION_FRAME_GENERATOR_H_

#include <cstddef>
#include <cstdint>

#include "bupt_volumetric/model/simulated_octree_frame.h"

namespace bupt::volumetric {

// Generates repeatable source data for transport and recovery experiments.
// The generator has no mutable state, wall-clock dependency, or global RNG, so
// a failing network scenario can be reproduced from its frame id and seed.
class SimulatedFrameGenerator final {
public:
  static constexpr std::size_t kPayloadBytes = 2'000'000;

  /**
   * Generates one deterministic simulated encoded frame.
   *
   * @param frame_id Application frame identifier mixed into the byte stream.
   * @param seed Experiment seed recorded in the run manifest.
   * @return A frame containing exactly kPayloadBytes source bytes.
   *
   * This operation performs no real point-cloud compression and is not
   * cryptographically random. Identical arguments always return identical
   * bytes, while changing either argument changes the generated stream.
   */
  [[nodiscard]] SimulatedOctreeFrame Generate(std::uint64_t frame_id,
                                              std::uint64_t seed) const;
};

} // namespace bupt::volumetric

#endif // BUPT_VOLUMETRIC_SIMULATION_FRAME_GENERATOR_H_

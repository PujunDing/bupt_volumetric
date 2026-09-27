#include "bupt_volumetric/transport/access_unit_planner.h"

#include <cstdint>
#include <vector>

#include "bupt_volumetric/model/semantic_block.h"
#include "bupt_volumetric/model/semantic_region.h"
#include "bupt_volumetric/protection/protection_policy.h"
#include "bupt_volumetric/simulation/semantic_partitioner.h"
#include "bupt_volumetric/transport/block_planner.h"
#include "bupt_volumetric/transport/semantic_admission_controller.h"
#include "bupt_volumetric/transport/transport_budget.h"

namespace bupt::volumetric {

std::vector<SimulatedAccessUnit> PlanAdmittedAccessUnits(
    const SimulatedOctreeFrame& frame,
    std::uint32_t rtp_timestamp,
    std::uint64_t available_media_bytes) {
  const std::vector<SemanticRegion> regions =
      SemanticPartitioner().Partition(frame);
  const std::vector<SemanticBlock> blocks = BlockPlanner().Plan(regions);
  const TransportOverheadProfile overhead{};

  std::vector<AdmissionCandidate> candidates;
  candidates.reserve(blocks.size());
  for (const SemanticBlock& block : blocks) {
    candidates.push_back({.frame_id = frame.frame_id(),
                          .block = block,
                          .created_at_ms = 0,
                          .deadline_ms = 10'000});
  }
  const AdmissionResult admission = SemanticAdmissionController().Decide(
      candidates, /*now_ms=*/0, available_media_bytes, AdmissionLimits{},
      overhead);

  std::vector<SimulatedAccessUnit> units;
  for (const AdmissionDecision& decision : admission.decisions) {
    if (decision.outcome != AdmissionOutcome::kAdmitted) {
      continue;
    }
    const SemanticBlock& block = blocks[decision.candidate_index];
    units.push_back(SimulatedAccessUnit{
        .frame_id = frame.frame_id(),
        .rtp_timestamp = rtp_timestamp,
        .fec_rate = FecRateQ8(block.semantic_class()),
        .semantic_class = static_cast<std::uint8_t>(block.semantic_class()),
        .payload = std::vector<std::uint8_t>(
            frame.payload().begin() + block.offset_bytes(),
            frame.payload().begin() + block.offset_bytes() + block.size_bytes()),
    });
  }
  return units;
}

}  // namespace bupt::volumetric

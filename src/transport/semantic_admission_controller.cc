#include "bupt_volumetric/transport/semantic_admission_controller.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

#include "bupt_volumetric/model/semantic_block.h"
#include "bupt_volumetric/protection/protection_budget.h"
#include "bupt_volumetric/transport/transport_budget.h"

namespace bupt::volumetric {
namespace {

std::uint64_t EstimateBlockBytes(
    const SemanticBlock &block,
    const TransportOverheadProfile &overhead) noexcept {
  const ProtectionBudget protection =
      ProtectionBudgetCalculator().Calculate({block});
  return TransportBudgetCalculator()
      .EstimateFrameCost(protection, overhead)
      .total_wire_bytes;
}

AdmissionDecision Drop(std::size_t candidate_index,
                       const AdmissionCandidate &candidate,
                       AdmissionReason reason,
                       std::uint64_t estimated_wire_bytes) noexcept {
  return AdmissionDecision{
      .candidate_index = candidate_index,
      .semantic_class = candidate.block.semantic_class(),
      .outcome = AdmissionOutcome::kDropped,
      .reason = reason,
      .estimated_wire_bytes = estimated_wire_bytes,
  };
}

} // namespace

AdmissionResult SemanticAdmissionController::Decide(
    const std::vector<AdmissionCandidate> &candidates, std::uint64_t now_ms,
    std::uint64_t available_media_bytes, const AdmissionLimits &limits,
    const TransportOverheadProfile &overhead) const {
  std::vector<std::size_t> priority_order(candidates.size());
  std::iota(priority_order.begin(), priority_order.end(), 0);
  std::stable_sort(priority_order.begin(), priority_order.end(),
                   [&candidates](std::size_t left, std::size_t right) {
                     const AdmissionCandidate &lhs = candidates[left];
                     const AdmissionCandidate &rhs = candidates[right];
                     if (lhs.block.semantic_class() !=
                         rhs.block.semantic_class()) {
                       return static_cast<std::uint8_t>(
                                  lhs.block.semantic_class()) <
                              static_cast<std::uint8_t>(
                                  rhs.block.semantic_class());
                     }
                     if (lhs.frame_id != rhs.frame_id) {
                       return lhs.frame_id > rhs.frame_id;
                     }
                     return lhs.block.block_id() < rhs.block.block_id();
                   });

  AdmissionResult result;
  result.decisions.reserve(candidates.size());
  for (const std::size_t candidate_index : priority_order) {
    const AdmissionCandidate &candidate = candidates[candidate_index];
    const std::uint64_t block_bytes =
        EstimateBlockBytes(candidate.block, overhead);
    if (candidate.deadline_ms <= now_ms) {
      result.decisions.push_back(Drop(candidate_index, candidate,
                                      AdmissionReason::kExpired, block_bytes));
      continue;
    }
    if (now_ms >= candidate.created_at_ms &&
        now_ms - candidate.created_at_ms > limits.max_frame_age_ms) {
      result.decisions.push_back(
          Drop(candidate_index, candidate, AdmissionReason::kFrameTooOld,
               block_bytes));
      continue;
    }
    if (result.admitted_block_count >= limits.max_admitted_blocks) {
      result.decisions.push_back(Drop(candidate_index, candidate,
                                      AdmissionReason::kBlockLimit,
                                      block_bytes));
      continue;
    }
    if (block_bytes > limits.max_admitted_bytes -
                          std::min(result.admitted_wire_bytes,
                                   limits.max_admitted_bytes)) {
      result.decisions.push_back(Drop(candidate_index, candidate,
                                      AdmissionReason::kByteLimit, block_bytes));
      continue;
    }
    if (block_bytes > available_media_bytes -
                          std::min(result.admitted_wire_bytes,
                                   available_media_bytes)) {
      result.decisions.push_back(
          Drop(candidate_index, candidate,
               AdmissionReason::kInsufficientBudget, block_bytes));
      continue;
    }

    result.decisions.push_back(AdmissionDecision{
        .candidate_index = candidate_index,
        .semantic_class = candidate.block.semantic_class(),
        .outcome = AdmissionOutcome::kAdmitted,
        .reason = AdmissionReason::kAdmitted,
        .estimated_wire_bytes = block_bytes,
    });
    ++result.admitted_block_count;
    result.admitted_wire_bytes += block_bytes;
  }
  return result;
}

} // namespace bupt::volumetric

#ifndef BUPT_VOLUMETRIC_TRANSPORT_SEMANTIC_ADMISSION_CONTROLLER_H_
#define BUPT_VOLUMETRIC_TRANSPORT_SEMANTIC_ADMISSION_CONTROLLER_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "bupt_volumetric/model/semantic_block.h"
#include "bupt_volumetric/model/semantic_class.h"
#include "bupt_volumetric/transport/transport_budget.h"

namespace bupt::volumetric {

/** A complete semantic block waiting for transport admission. */
struct AdmissionCandidate final {
  std::uint64_t frame_id;
  SemanticBlock block;
  std::uint64_t created_at_ms;
  std::uint64_t deadline_ms;
};

struct AdmissionLimits final {
  std::size_t max_admitted_blocks = std::numeric_limits<std::size_t>::max();
  std::uint64_t max_admitted_bytes =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t max_frame_age_ms =
      std::numeric_limits<std::uint64_t>::max();
};

enum class AdmissionOutcome : std::uint8_t {
  kAdmitted = 0,
  kDropped,
};

enum class AdmissionReason : std::uint8_t {
  kAdmitted = 0,
  kExpired,
  kFrameTooOld,
  kBlockLimit,
  kByteLimit,
  kInsufficientBudget,
};

/** Auditable outcome for one candidate after priority ordering. */
struct AdmissionDecision final {
  std::size_t candidate_index = 0;
  SemanticClass semantic_class = SemanticClass::kP0;
  AdmissionOutcome outcome = AdmissionOutcome::kDropped;
  AdmissionReason reason = AdmissionReason::kInsufficientBudget;
  std::uint64_t estimated_wire_bytes = 0;
};

struct AdmissionResult final {
  std::vector<AdmissionDecision> decisions;
  std::size_t admitted_block_count = 0;
  std::uint64_t admitted_wire_bytes = 0;
};

/** Performs whole-block, deadline-aware semantic admission. */
class SemanticAdmissionController final {
public:
  /**
   * Selects blocks without exceeding the post-protocol media budget.
   *
   * Candidates are evaluated P0, P1, P2; newer frames win within a class.
   * The result contains one stable decision and reason for every candidate.
   */
  [[nodiscard]] AdmissionResult
  Decide(const std::vector<AdmissionCandidate> &candidates,
         std::uint64_t now_ms, std::uint64_t available_media_bytes,
         const AdmissionLimits &limits,
         const TransportOverheadProfile &overhead) const;
};

} // namespace bupt::volumetric

#endif // BUPT_VOLUMETRIC_TRANSPORT_SEMANTIC_ADMISSION_CONTROLLER_H_

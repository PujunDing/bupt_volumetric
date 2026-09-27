#ifndef BUPT_VOLUMETRIC_PROTECTION_PROTECTION_BUDGET_H_
#define BUPT_VOLUMETRIC_PROTECTION_PROTECTION_BUDGET_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "bupt_volumetric/model/semantic_block.h"

namespace bupt::volumetric {

/** Payload-only protection accounting before transport protocol overhead. */
struct ProtectionBudget final {
  std::uint64_t source_payload_bytes = 0;
  std::uint64_t source_symbol_count = 0;
  std::uint64_t theoretical_repair_payload_bytes = 0;
  std::uint64_t repair_symbol_count = 0;
  std::uint64_t packetized_repair_payload_bytes = 0;
};

/** Computes auditable source and repair totals for semantic blocks. */
class ProtectionBudgetCalculator final {
public:
  /**
   * Calculates logical and whole-symbol repair costs.
   *
   * The theoretical value applies the class ratio to source bytes. The
   * packetized value counts whole repair symbols requested by the policy.
   */
  [[nodiscard]] ProtectionBudget
  Calculate(const std::vector<SemanticBlock> &blocks,
            std::size_t repair_symbol_bytes = 1'150) const noexcept;
};

} // namespace bupt::volumetric

#endif // BUPT_VOLUMETRIC_PROTECTION_PROTECTION_BUDGET_H_

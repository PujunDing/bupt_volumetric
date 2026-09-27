#include "bupt_volumetric/protection/protection_budget.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "bupt_volumetric/model/semantic_block.h"
#include "bupt_volumetric/model/semantic_class.h"
#include "bupt_volumetric/protection/protection_policy.h"

namespace bupt::volumetric {
namespace {

std::uint64_t TheoreticalRepairBytes(const SemanticBlock &block) noexcept {
  switch (block.semantic_class()) {
  case SemanticClass::kP0:
    return block.size_bytes() / 2;
  case SemanticClass::kP1:
    return block.size_bytes() / 8;
  case SemanticClass::kP2:
    return 0;
  }
  return 0;
}

} // namespace

ProtectionBudget ProtectionBudgetCalculator::Calculate(
    const std::vector<SemanticBlock> &blocks,
    std::size_t repair_symbol_bytes) const noexcept {
  ProtectionBudget budget;
  const ProtectionPolicy policy;
  for (const SemanticBlock &block : blocks) {
    const FecProfile profile = policy.ForBlock(block);
    budget.source_payload_bytes += block.size_bytes();
    budget.source_symbol_count += block.source_symbol_count();
    budget.theoretical_repair_payload_bytes +=
        TheoreticalRepairBytes(block);
    budget.repair_symbol_count += profile.repair_symbol_count();
  }
  budget.packetized_repair_payload_bytes =
      budget.repair_symbol_count * repair_symbol_bytes;
  return budget;
}

} // namespace bupt::volumetric

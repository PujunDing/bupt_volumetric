#include "bupt_volumetric/protection/protection_policy.h"

#include <cstddef>

#include "bupt_volumetric/model/semantic_class.h"

namespace bupt::volumetric {
namespace {

constexpr std::size_t DivideRoundUp(std::size_t numerator,
                                    std::size_t denominator) noexcept {
  return (numerator + denominator - 1) / denominator;
}

} // namespace

FecProfile
ProtectionPolicy::ForBlock(const SemanticBlock &block) const noexcept {
  const std::size_t source_symbols = block.source_symbol_count();
  switch (block.semantic_class()) {
  case SemanticClass::kP0:
    return FecProfile(source_symbols, DivideRoundUp(source_symbols, 2));
  case SemanticClass::kP1:
    return FecProfile(source_symbols, DivideRoundUp(source_symbols, 8));
  case SemanticClass::kP2:
    return FecProfile(source_symbols, 0);
  }
  return FecProfile(source_symbols, 0);
}

} // namespace bupt::volumetric

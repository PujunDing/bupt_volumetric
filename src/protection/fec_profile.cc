#include "bupt_volumetric/protection/fec_profile.h"

namespace bupt::volumetric {

FecProfile::FecProfile(std::size_t source_symbol_count,
                       std::size_t repair_symbol_count) noexcept
    : source_symbol_count_(source_symbol_count),
      repair_symbol_count_(repair_symbol_count) {}

std::size_t FecProfile::source_symbol_count() const noexcept {
  return source_symbol_count_;
}

std::size_t FecProfile::repair_symbol_count() const noexcept {
  return repair_symbol_count_;
}

} // namespace bupt::volumetric

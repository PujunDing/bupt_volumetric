#ifndef BUPT_VOLUMETRIC_PROTECTION_FEC_PROFILE_H_
#define BUPT_VOLUMETRIC_PROTECTION_FEC_PROFILE_H_

#include <cstddef>

namespace bupt::volumetric {

// Immutable source/repair allocation for one semantic FEC block.
class FecProfile final {
public:
  /**
   * Creates a per-block FEC allocation.
   *
   * @param source_symbol_count Number of source symbols in the block.
   * @param repair_symbol_count Number of repair symbols requested from WebRTC.
   */
  FecProfile(std::size_t source_symbol_count,
             std::size_t repair_symbol_count) noexcept;

  /** Returns the number of protected source symbols. */
  [[nodiscard]] std::size_t source_symbol_count() const noexcept;
  /** Returns the number of repair symbols to generate. */
  [[nodiscard]] std::size_t repair_symbol_count() const noexcept;

private:
  std::size_t source_symbol_count_;
  std::size_t repair_symbol_count_;
};

} // namespace bupt::volumetric

#endif // BUPT_VOLUMETRIC_PROTECTION_FEC_PROFILE_H_

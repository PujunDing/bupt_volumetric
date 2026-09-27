#ifndef BUPT_VOLUMETRIC_PROTECTION_PROTECTION_POLICY_H_
#define BUPT_VOLUMETRIC_PROTECTION_PROTECTION_POLICY_H_

#include <cstdint>

#include "bupt_volumetric/model/semantic_block.h"
#include "bupt_volumetric/model/semantic_class.h"
#include "bupt_volumetric/protection/fec_profile.h"

namespace bupt::volumetric {

// Returns the WebRTC ULPFEC protection factor (Q8, 0..255) for a semantic class:
// P0=128 (50%), P1=32 (12.5%), P2=0 (source only). These match the repair counts
// produced by ProtectionPolicy::ForBlock via ForwardErrorCorrection::NumFecPackets.
[[nodiscard]] constexpr std::uint16_t FecRateQ8(
    SemanticClass semantic_class) noexcept {
  switch (semantic_class) {
  case SemanticClass::kP0:
    return 128;
  case SemanticClass::kP1:
    return 32;
  case SemanticClass::kP2:
    return 0;
  }
  return 0;
}

// Maps semantic value to the prototype's fixed initial FEC ratios.
class ProtectionPolicy final {
public:
  /**
   * Selects repair allocation for one admitted block.
   *
   * @param block Source block whose class and symbol count drive protection.
   * @return P0 uses 50%, P1 uses 12.5%, and P2 uses no repair symbols.
   *
   * Fractional repair counts round upward so tail blocks are never protected
   * less than their configured ratio. This policy does not control pacing or
   * invent additional bitrate beyond the admission budget.
   */
  [[nodiscard]] FecProfile ForBlock(const SemanticBlock &block) const noexcept;
};

} // namespace bupt::volumetric

#endif // BUPT_VOLUMETRIC_PROTECTION_PROTECTION_POLICY_H_

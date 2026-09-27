#ifndef BUPT_VOLUMETRIC_TRANSPORT_TRANSPORT_BUDGET_H_
#define BUPT_VOLUMETRIC_TRANSPORT_TRANSPORT_BUDGET_H_

#include <cstddef>
#include <cstdint>

#include "bupt_volumetric/protection/protection_budget.h"

namespace bupt::volumetric {

/** Conservative per-packet overhead assumptions used before live calibration. */
struct TransportOverheadProfile final {
  std::size_t application_header_bytes = 0;
  std::size_t rtp_header_bytes = 0;
  std::size_t rtp_extension_reserve_bytes = 0;
  std::size_t srtp_overhead_bytes = 0;
  std::size_t udp_ip_overhead_bytes = 0;
  std::size_t red_ulpfec_overhead_bytes = 0;
  std::size_t rtcp_reserve_bytes = 0;
};

/** Frame cost split into mutually exclusive accounting categories. */
struct TransportCost final {
  std::uint64_t source_payload_bytes = 0;
  std::uint64_t application_header_bytes = 0;
  std::uint64_t repair_payload_bytes = 0;
  std::uint64_t transport_overhead_bytes = 0;
  std::uint64_t rtcp_reserve_bytes = 0;
  std::uint64_t total_wire_bytes = 0;
};

enum class WindowBudgetError : std::uint8_t {
  kNone = 0,
  kInvalidWindow,
  kOverflow,
  kReserveExceedsTarget,
};

/** Byte budget available during one scheduler window. */
struct WindowBudget final {
  WindowBudgetError error = WindowBudgetError::kNone;
  std::uint64_t target_wire_bytes = 0;
  std::uint64_t protocol_reserve_bytes = 0;
  std::uint64_t available_media_bytes = 0;

  [[nodiscard]] bool ok() const noexcept {
    return error == WindowBudgetError::kNone;
  }
};

/** Converts BWE targets and FEC allocation into auditable byte budgets. */
class TransportBudgetCalculator final {
public:
  /** Estimates one frame without merging any accounting categories. */
  [[nodiscard]] TransportCost
  EstimateFrameCost(const ProtectionBudget &protection,
                    const TransportOverheadProfile &overhead) const noexcept;

  /** Converts bits/second and milliseconds into a post-reserve media budget. */
  [[nodiscard]] WindowBudget
  BuildWindowBudget(std::uint64_t target_bitrate_bps,
                    std::uint64_t window_ms,
                    std::uint64_t protocol_reserve_bytes) const noexcept;
};

} // namespace bupt::volumetric

#endif // BUPT_VOLUMETRIC_TRANSPORT_TRANSPORT_BUDGET_H_

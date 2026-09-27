#include "bupt_volumetric/transport/transport_budget.h"

#include <cstdint>
#include <limits>

#include "bupt_volumetric/protection/protection_budget.h"

namespace bupt::volumetric {

TransportCost TransportBudgetCalculator::EstimateFrameCost(
    const ProtectionBudget &protection,
    const TransportOverheadProfile &overhead) const noexcept {
  const std::uint64_t packet_count =
      protection.source_symbol_count + protection.repair_symbol_count;
  const std::uint64_t shared_transport_overhead =
      overhead.rtp_header_bytes + overhead.rtp_extension_reserve_bytes +
      overhead.srtp_overhead_bytes + overhead.udp_ip_overhead_bytes;

  TransportCost cost{
      .source_payload_bytes = protection.source_payload_bytes,
      .application_header_bytes =
          protection.source_symbol_count * overhead.application_header_bytes,
      .repair_payload_bytes = protection.packetized_repair_payload_bytes,
      .transport_overhead_bytes =
          packet_count * shared_transport_overhead +
          protection.repair_symbol_count *
              overhead.red_ulpfec_overhead_bytes,
      .rtcp_reserve_bytes = overhead.rtcp_reserve_bytes,
  };
  cost.total_wire_bytes =
      cost.source_payload_bytes + cost.application_header_bytes +
      cost.repair_payload_bytes + cost.transport_overhead_bytes +
      cost.rtcp_reserve_bytes;
  return cost;
}

WindowBudget TransportBudgetCalculator::BuildWindowBudget(
    std::uint64_t target_bitrate_bps, std::uint64_t window_ms,
    std::uint64_t protocol_reserve_bytes) const noexcept {
  if (window_ms == 0) {
    return WindowBudget{
        .error = WindowBudgetError::kInvalidWindow,
        .target_wire_bytes = 0,
        .protocol_reserve_bytes = protocol_reserve_bytes,
        .available_media_bytes = 0,
    };
  }
  if (target_bitrate_bps >
      std::numeric_limits<std::uint64_t>::max() / window_ms) {
    return WindowBudget{
        .error = WindowBudgetError::kOverflow,
        .target_wire_bytes = 0,
        .protocol_reserve_bytes = protocol_reserve_bytes,
        .available_media_bytes = 0,
    };
  }

  const std::uint64_t target_wire_bytes =
      target_bitrate_bps * window_ms / 8'000;
  if (protocol_reserve_bytes > target_wire_bytes) {
    return WindowBudget{
        .error = WindowBudgetError::kReserveExceedsTarget,
        .target_wire_bytes = target_wire_bytes,
        .protocol_reserve_bytes = protocol_reserve_bytes,
        .available_media_bytes = 0,
    };
  }
  return WindowBudget{
      .error = WindowBudgetError::kNone,
      .target_wire_bytes = target_wire_bytes,
      .protocol_reserve_bytes = protocol_reserve_bytes,
      .available_media_bytes = target_wire_bytes - protocol_reserve_bytes,
  };
}

} // namespace bupt::volumetric

#ifndef BUPT_VOLUMETRIC_PROTECTION_FEC_WIRE_ACCOUNTING_H_
#define BUPT_VOLUMETRIC_PROTECTION_FEC_WIRE_ACCOUNTING_H_

#include <cstddef>
#include <cstdint>

#include "bupt_volumetric/model/semantic_block.h"
#include "bupt_volumetric/transport/block_planner.h"

namespace bupt::volumetric {

// Byte-level ledger for one semantic FEC block, derived from the protection
// policy and WebRTC's NumFecPackets formula. Padding and RTCP are intentionally
// excluded; they are the only allowed discrepancy against egress counters.
struct FecWireAccounting final {
  std::size_t source_packets = 0;
  std::size_t source_bytes = 0;
  std::size_t repair_packets = 0;
  std::size_t repair_bytes = 0;
  std::size_t header_bytes = 0;        // 48-byte OCTREE-SIM header per symbol
  std::size_t red_overhead_bytes = 0;  // 1-byte RED header per repair packet
};

// Computes the ledger for a block given its Q8 ULPFEC protection factor.
[[nodiscard]] FecWireAccounting ComputeFecWireAccounting(
    const SemanticBlock& block,
    std::uint16_t fec_rate,
    std::size_t symbol_bytes = BlockPlanner::kPayloadBytesPerSymbol);

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_PROTECTION_FEC_WIRE_ACCOUNTING_H_

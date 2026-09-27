#include "bupt_volumetric/protection/fec_wire_accounting.h"

#include <cstddef>
#include <cstdint>

#include "bupt_volumetric/model/semantic_block.h"
#include "bupt_volumetric/transport/wire_header.h"

namespace bupt::volumetric {

FecWireAccounting ComputeFecWireAccounting(const SemanticBlock& block,
                                           std::uint16_t fec_rate,
                                           std::size_t symbol_bytes) {
  const std::size_t source = block.source_symbol_count();
  // Same formula as ForwardErrorCorrection::NumFecPackets.
  const std::size_t repair = (source * fec_rate + 128) >> 8;
  return FecWireAccounting{
      .source_packets = source,
      .source_bytes = block.size_bytes(),
      .repair_packets = repair,
      .repair_bytes = repair * symbol_bytes,
      .header_bytes = source * kWireHeaderBytes,
      .red_overhead_bytes = repair,
  };
}

}  // namespace bupt::volumetric

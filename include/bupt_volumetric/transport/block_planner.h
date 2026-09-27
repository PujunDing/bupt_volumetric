#ifndef BUPT_VOLUMETRIC_TRANSPORT_BLOCK_PLANNER_H_
#define BUPT_VOLUMETRIC_TRANSPORT_BLOCK_PLANNER_H_

#include <cstddef>
#include <vector>

#include "bupt_volumetric/model/semantic_block.h"
#include "bupt_volumetric/model/semantic_region.h"

namespace bupt::volumetric {

// Converts semantic byte ranges into bounded atomic transport/FEC blocks.
class BlockPlanner final {
public:
  static constexpr std::size_t kPayloadBytesPerSymbol = 1'150;
  static constexpr std::size_t kMaxSourceSymbolsPerBlock = 32;

  /**
   * Plans transport blocks for ordered semantic regions.
   *
   * @param regions Non-overlapping ranges in frame payload order.
   * @return Blocks in the same order, each covering at most 32 source symbols.
   *
   * A short final symbol is represented through block size rather than hidden
   * padding, so byte accounting remains exact.
   */
  [[nodiscard]] std::vector<SemanticBlock>
  Plan(const std::vector<SemanticRegion> &regions,
       std::size_t payload_bytes_per_symbol = kPayloadBytesPerSymbol) const;
};

} // namespace bupt::volumetric

#endif // BUPT_VOLUMETRIC_TRANSPORT_BLOCK_PLANNER_H_

#include "bupt_volumetric/transport/block_planner.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bupt::volumetric {
namespace {

constexpr std::size_t DivideRoundUp(std::size_t numerator,
                                    std::size_t denominator) noexcept {
  return (numerator + denominator - 1) / denominator;
}

} // namespace

std::vector<SemanticBlock>
BlockPlanner::Plan(const std::vector<SemanticRegion> &regions,
                   std::size_t payload_bytes_per_symbol) const {
  std::vector<SemanticBlock> blocks;
  if (payload_bytes_per_symbol == 0 ||
      payload_bytes_per_symbol > kPayloadBytesPerSymbol) {
    return blocks;
  }
  const std::size_t max_block_bytes =
      payload_bytes_per_symbol * kMaxSourceSymbolsPerBlock;

  for (const SemanticRegion &region : regions) {
    std::size_t region_offset = 0;
    std::uint32_t block_id = 0;
    while (region_offset < region.size_bytes()) {
      const std::size_t block_bytes =
          std::min(max_block_bytes, region.size_bytes() - region_offset);
      const std::size_t symbol_count =
          DivideRoundUp(block_bytes, payload_bytes_per_symbol);
      blocks.emplace_back(region.semantic_class(), block_id,
                          region.offset_bytes() + region_offset, block_bytes,
                          symbol_count);
      region_offset += block_bytes;
      ++block_id;
    }
  }
  return blocks;
}

} // namespace bupt::volumetric

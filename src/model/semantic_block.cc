#include "bupt_volumetric/model/semantic_block.h"

namespace bupt::volumetric {

SemanticBlock::SemanticBlock(SemanticClass semantic_class,
                             std::uint32_t block_id, std::size_t offset_bytes,
                             std::size_t size_bytes,
                             std::size_t source_symbol_count) noexcept
    : semantic_class_(semantic_class), block_id_(block_id),
      offset_bytes_(offset_bytes), size_bytes_(size_bytes),
      source_symbol_count_(source_symbol_count) {}

SemanticClass SemanticBlock::semantic_class() const noexcept {
  return semantic_class_;
}
std::uint32_t SemanticBlock::block_id() const noexcept { return block_id_; }
std::size_t SemanticBlock::offset_bytes() const noexcept {
  return offset_bytes_;
}
std::size_t SemanticBlock::size_bytes() const noexcept { return size_bytes_; }
std::size_t SemanticBlock::source_symbol_count() const noexcept {
  return source_symbol_count_;
}

} // namespace bupt::volumetric

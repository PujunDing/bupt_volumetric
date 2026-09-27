#include "bupt_volumetric/model/semantic_region.h"

namespace bupt::volumetric {

SemanticRegion::SemanticRegion(SemanticClass semantic_class,
                               std::size_t offset_bytes,
                               std::size_t size_bytes) noexcept
    : semantic_class_(semantic_class), offset_bytes_(offset_bytes),
      size_bytes_(size_bytes) {}

SemanticClass SemanticRegion::semantic_class() const noexcept {
  return semantic_class_;
}

std::size_t SemanticRegion::offset_bytes() const noexcept {
  return offset_bytes_;
}

std::size_t SemanticRegion::size_bytes() const noexcept { return size_bytes_; }

std::size_t SemanticRegion::end_offset_bytes() const noexcept {
  return offset_bytes_ + size_bytes_;
}

} // namespace bupt::volumetric

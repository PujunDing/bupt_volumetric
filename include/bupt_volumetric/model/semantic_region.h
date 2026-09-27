#ifndef BUPT_VOLUMETRIC_MODEL_SEMANTIC_REGION_H_
#define BUPT_VOLUMETRIC_MODEL_SEMANTIC_REGION_H_

#include <cstddef>

#include "bupt_volumetric/model/semantic_class.h"

namespace bupt::volumetric {

// Describes a non-owning contiguous range within a simulated frame payload.
class SemanticRegion final {
public:
  /**
   * Creates a semantic range descriptor.
   *
   * @param semantic_class Protection/admission priority assigned to the range.
   * @param offset_bytes Zero-based byte offset in the frame source payload.
   * @param size_bytes Number of source bytes in the range.
   */
  SemanticRegion(SemanticClass semantic_class, std::size_t offset_bytes,
                 std::size_t size_bytes) noexcept;

  /** Returns the semantic priority assigned to this range. */
  [[nodiscard]] SemanticClass semantic_class() const noexcept;

  /** Returns the zero-based source-payload offset in bytes. */
  [[nodiscard]] std::size_t offset_bytes() const noexcept;

  /** Returns the number of source-payload bytes in this region. */
  [[nodiscard]] std::size_t size_bytes() const noexcept;

  /** Returns the exclusive source-payload end offset in bytes. */
  [[nodiscard]] std::size_t end_offset_bytes() const noexcept;

private:
  SemanticClass semantic_class_;
  std::size_t offset_bytes_;
  std::size_t size_bytes_;
};

} // namespace bupt::volumetric

#endif // BUPT_VOLUMETRIC_MODEL_SEMANTIC_REGION_H_

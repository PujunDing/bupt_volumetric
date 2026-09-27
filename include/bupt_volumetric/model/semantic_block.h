#ifndef BUPT_VOLUMETRIC_MODEL_SEMANTIC_BLOCK_H_
#define BUPT_VOLUMETRIC_MODEL_SEMANTIC_BLOCK_H_

#include <cstddef>
#include <cstdint>

#include "bupt_volumetric/model/semantic_class.h"

namespace bupt::volumetric {

// Describes one atomic admission and FEC unit within a semantic region.
class SemanticBlock final {
public:
  /**
   * Creates a block descriptor without copying source payload bytes.
   *
   * @param semantic_class Semantic protection/admission priority.
   * @param block_id Zero-based block identifier within its semantic class.
   * @param offset_bytes Zero-based offset in the frame source payload.
   * @param size_bytes Number of source bytes covered by this block.
   * @param source_symbol_count Number of RTP source symbols, in [1, 32].
   */
  SemanticBlock(SemanticClass semantic_class, std::uint32_t block_id,
                std::size_t offset_bytes, std::size_t size_bytes,
                std::size_t source_symbol_count) noexcept;

  /** Returns the semantic priority of this block. */
  [[nodiscard]] SemanticClass semantic_class() const noexcept;
  /** Returns the zero-based block identifier within its semantic class. */
  [[nodiscard]] std::uint32_t block_id() const noexcept;
  /** Returns the zero-based source-payload offset in bytes. */
  [[nodiscard]] std::size_t offset_bytes() const noexcept;
  /** Returns the number of source bytes covered by this block. */
  [[nodiscard]] std::size_t size_bytes() const noexcept;
  /** Returns the number of source RTP symbols required by this block. */
  [[nodiscard]] std::size_t source_symbol_count() const noexcept;

private:
  SemanticClass semantic_class_;
  std::uint32_t block_id_;
  std::size_t offset_bytes_;
  std::size_t size_bytes_;
  std::size_t source_symbol_count_;
};

} // namespace bupt::volumetric

#endif // BUPT_VOLUMETRIC_MODEL_SEMANTIC_BLOCK_H_

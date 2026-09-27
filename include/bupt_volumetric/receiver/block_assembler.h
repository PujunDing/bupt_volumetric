#ifndef BUPT_VOLUMETRIC_RECEIVER_BLOCK_ASSEMBLER_H_
#define BUPT_VOLUMETRIC_RECEIVER_BLOCK_ASSEMBLER_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <tuple>
#include <vector>

#include "bupt_volumetric/model/semantic_class.h"
#include "bupt_volumetric/transport/wire_header.h"

namespace bupt::volumetric {

enum class BlockAssemblyStatus : std::uint8_t {
  kAccepted = 0,
  kDuplicate,
  kCompleted,
  kAlreadyCompleted,
  kExpired,
  kMalformed,
  kMetadataMismatch,
  kResourceLimit,
};

struct CompletedBlock final {
  std::uint64_t frame_id = 0;
  SemanticClass semantic_class = SemanticClass::kP0;
  std::uint32_t region_id = 0;
  std::uint32_t block_id = 0;
  std::vector<std::uint8_t> bytes;
  std::size_t recovered_symbol_count = 0;
};

struct BlockAssemblyResult final {
  BlockAssemblyStatus status = BlockAssemblyStatus::kMalformed;
  std::optional<CompletedBlock> completed_block;
};

/** Reorders and deduplicates bounded source symbols until a block is usable. */
class BlockAssembler final {
public:
  BlockAssembler(std::size_t max_inflight_blocks,
                 std::size_t max_buffered_bytes) noexcept;

  /** Accepts one validated or WebRTC-recovered OCTREE-SIM source packet. */
  [[nodiscard]] BlockAssemblyResult
  Accept(std::span<const std::uint8_t> packet, bool recovered,
         std::uint64_t now_ms);

  [[nodiscard]] std::size_t inflight_block_count() const noexcept;
  [[nodiscard]] std::size_t buffered_bytes() const noexcept;

private:
  using BlockKey =
      std::tuple<std::uint64_t, SemanticClass, std::uint32_t, std::uint32_t>;
  struct BlockState final {
    WireHeader identity;
    std::vector<std::optional<std::vector<std::uint8_t>>> symbols;
    std::size_t received_symbols = 0;
    std::size_t recovered_symbols = 0;
  };

  std::size_t max_inflight_blocks_;
  std::size_t max_buffered_bytes_;
  std::size_t buffered_bytes_ = 0;
  std::map<BlockKey, BlockState> inflight_;
  std::set<BlockKey> completed_;
};

} // namespace bupt::volumetric

#endif // BUPT_VOLUMETRIC_RECEIVER_BLOCK_ASSEMBLER_H_

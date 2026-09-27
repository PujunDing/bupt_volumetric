#include "bupt_volumetric/receiver/block_assembler.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "bupt_volumetric/transport/wire_header.h"

namespace bupt::volumetric {

BlockAssembler::BlockAssembler(std::size_t max_inflight_blocks,
                               std::size_t max_buffered_bytes) noexcept
    : max_inflight_blocks_(max_inflight_blocks),
      max_buffered_bytes_(max_buffered_bytes) {}

BlockAssemblyResult BlockAssembler::Accept(
    std::span<const std::uint8_t> packet, bool recovered,
    std::uint64_t now_ms) {
  const WireDecodeResult decoded = ParseWirePacket(packet);
  if (!decoded.ok()) {
    return {.status = BlockAssemblyStatus::kMalformed,
            .completed_block = std::nullopt};
  }
  const WireHeader &header = *decoded.header;
  const BlockKey key{header.frame_id, header.semantic_class, header.region_id,
                     header.block_id};
  if (now_ms >= header.latency_token &&
      now_ms - header.latency_token > header.deadline_offset_ms) {
    const auto expired = inflight_.find(key);
    if (expired != inflight_.end()) {
      for (const auto &symbol : expired->second.symbols) {
        if (symbol.has_value()) {
          buffered_bytes_ -= symbol->size();
        }
      }
      inflight_.erase(expired);
    }
    return {.status = BlockAssemblyStatus::kExpired,
            .completed_block = std::nullopt};
  }

  if (completed_.contains(key)) {
    return {.status = BlockAssemblyStatus::kAlreadyCompleted,
            .completed_block = std::nullopt};
  }

  auto state_it = inflight_.find(key);
  if (state_it == inflight_.end()) {
    if (inflight_.size() >= max_inflight_blocks_ ||
        decoded.payload.size() > max_buffered_bytes_ -
                                     std::min(buffered_bytes_,
                                              max_buffered_bytes_)) {
      return {.status = BlockAssemblyStatus::kResourceLimit,
              .completed_block = std::nullopt};
    }
    BlockState state{
        .identity = header,
        .symbols = std::vector<std::optional<std::vector<std::uint8_t>>>(
            header.source_symbol_count),
    };
    state_it = inflight_.emplace(key, std::move(state)).first;
  }
  BlockState &state = state_it->second;
  if (state.identity.source_symbol_count != header.source_symbol_count ||
      state.identity.repair_symbol_count != header.repair_symbol_count) {
    return {.status = BlockAssemblyStatus::kMetadataMismatch,
            .completed_block = std::nullopt};
  }
  if (state.symbols[header.symbol_index].has_value()) {
    return {.status = BlockAssemblyStatus::kDuplicate,
            .completed_block = std::nullopt};
  }
  if (decoded.payload.size() > max_buffered_bytes_ -
                                   std::min(buffered_bytes_,
                                            max_buffered_bytes_)) {
    return {.status = BlockAssemblyStatus::kResourceLimit,
            .completed_block = std::nullopt};
  }

  state.symbols[header.symbol_index] =
      std::vector<std::uint8_t>(decoded.payload.begin(), decoded.payload.end());
  ++state.received_symbols;
  state.recovered_symbols += recovered ? 1 : 0;
  buffered_bytes_ += decoded.payload.size();
  if (state.received_symbols != state.symbols.size()) {
    return {.status = BlockAssemblyStatus::kAccepted,
            .completed_block = std::nullopt};
  }

  CompletedBlock completed{
      .frame_id = header.frame_id,
      .semantic_class = header.semantic_class,
      .region_id = header.region_id,
      .block_id = header.block_id,
      .bytes = {},
      .recovered_symbol_count = state.recovered_symbols,
  };
  for (const auto &symbol : state.symbols) {
    completed.bytes.insert(completed.bytes.end(), symbol->begin(),
                           symbol->end());
  }
  buffered_bytes_ -= completed.bytes.size();
  inflight_.erase(state_it);
  completed_.insert(key);
  return {.status = BlockAssemblyStatus::kCompleted,
          .completed_block = std::move(completed)};
}

std::size_t BlockAssembler::inflight_block_count() const noexcept {
  return inflight_.size();
}

std::size_t BlockAssembler::buffered_bytes() const noexcept {
  return buffered_bytes_;
}

} // namespace bupt::volumetric

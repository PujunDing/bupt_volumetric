#include "bupt_volumetric/transport/semantic_packetizer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "bupt_volumetric/transport/block_planner.h"
#include "bupt_volumetric/transport/wire_header.h"

namespace bupt::volumetric {
namespace {

std::size_t DivideRoundUp(std::size_t numerator,
                          std::size_t denominator) noexcept {
  return numerator / denominator + (numerator % denominator != 0 ? 1 : 0);
}

WireFlags FlagsForSymbol(std::size_t symbol_index,
                         std::size_t symbol_count) noexcept {
  WireFlags flags = WireFlags::kNone;
  if (symbol_index == 0) {
    flags = flags | WireFlags::kBlockBegin;
  }
  if (symbol_index + 1 == symbol_count) {
    flags = flags | WireFlags::kBlockEnd;
  }
  return flags;
}

PacketizationResult Failure(PacketizationError error) {
  return PacketizationResult{.error = error, .packets = {}};
}

} // namespace

PacketizationResult SemanticPacketizer::Packetize(
    const SimulatedOctreeFrame &frame, const SemanticBlock &block,
    const FecProfile &profile, const PacketizationContext &context,
    std::size_t max_rtp_payload_bytes) const {
  if (max_rtp_payload_bytes <= kWireHeaderBytes) {
    return Failure(PacketizationError::kPayloadLimitTooSmall);
  }
  if (block.offset_bytes() > frame.payload().size() ||
      block.size_bytes() > frame.payload().size() - block.offset_bytes()) {
    return Failure(PacketizationError::kBlockOutsideFrame);
  }
  if (profile.source_symbol_count() != block.source_symbol_count()) {
    return Failure(PacketizationError::kFecProfileMismatch);
  }

  const std::size_t source_bytes_per_symbol =
      max_rtp_payload_bytes - kWireHeaderBytes;
  const std::size_t expected_symbol_count =
      DivideRoundUp(block.size_bytes(), source_bytes_per_symbol);
  if (expected_symbol_count != block.source_symbol_count()) {
    return Failure(PacketizationError::kBlockPlanMismatch);
  }
  if (expected_symbol_count == 0 ||
      expected_symbol_count > BlockPlanner::kMaxSourceSymbolsPerBlock ||
      profile.repair_symbol_count() > expected_symbol_count ||
      source_bytes_per_symbol > std::numeric_limits<std::uint16_t>::max()) {
    return Failure(PacketizationError::kFieldOutOfRange);
  }

  std::vector<std::vector<std::uint8_t>> packets;
  packets.reserve(expected_symbol_count);
  std::size_t source_offset = block.offset_bytes();
  std::size_t remaining_bytes = block.size_bytes();
  for (std::size_t symbol_index = 0; symbol_index < expected_symbol_count;
       ++symbol_index) {
    const std::size_t payload_bytes =
        std::min(source_bytes_per_symbol, remaining_bytes);
    const WireHeader header{
        .flags = FlagsForSymbol(symbol_index, expected_symbol_count),
        .semantic_class = block.semantic_class(),
        .frame_id = frame.frame_id(),
        .rtp_timestamp = context.rtp_timestamp,
        .region_id = context.region_id,
        .block_id = block.block_id(),
        .source_symbol_count =
            static_cast<std::uint16_t>(expected_symbol_count),
        .repair_symbol_count =
            static_cast<std::uint16_t>(profile.repair_symbol_count()),
        .symbol_index = static_cast<std::uint16_t>(symbol_index),
        .payload_bytes = static_cast<std::uint16_t>(payload_bytes),
        .latency_token = context.latency_token,
        .deadline_offset_ms = context.deadline_offset_ms,
    };
    const auto encoded_header = SerializeWireHeader(header);
    if (!encoded_header.has_value()) {
      return Failure(PacketizationError::kFieldOutOfRange);
    }

    std::vector<std::uint8_t> packet(encoded_header->begin(),
                                     encoded_header->end());
    const auto payload_begin = frame.payload().begin() + source_offset;
    packet.insert(packet.end(), payload_begin, payload_begin + payload_bytes);
    packets.push_back(std::move(packet));
    source_offset += payload_bytes;
    remaining_bytes -= payload_bytes;
  }

  return PacketizationResult{
      .error = PacketizationError::kNone,
      .packets = std::move(packets),
  };
}

} // namespace bupt::volumetric

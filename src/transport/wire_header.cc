#include "bupt_volumetric/transport/wire_header.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace bupt::volumetric {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {0x42, 0x56, 0x4f, 0x4c};
constexpr std::uint8_t kKnownFlagMask = 0x03;
constexpr std::uint16_t kMaxSourceSymbols = 32;

void WriteU16(std::span<std::uint8_t> output, std::size_t offset,
              std::uint16_t value) noexcept {
  output[offset] = static_cast<std::uint8_t>(value >> 8U);
  output[offset + 1] = static_cast<std::uint8_t>(value);
}

void WriteU32(std::span<std::uint8_t> output, std::size_t offset,
              std::uint32_t value) noexcept {
  for (std::size_t index = 0; index < 4; ++index) {
    output[offset + index] =
        static_cast<std::uint8_t>(value >> ((3U - index) * 8U));
  }
}

void WriteU64(std::span<std::uint8_t> output, std::size_t offset,
              std::uint64_t value) noexcept {
  for (std::size_t index = 0; index < 8; ++index) {
    output[offset + index] =
        static_cast<std::uint8_t>(value >> ((7U - index) * 8U));
  }
}

std::uint16_t ReadU16(std::span<const std::uint8_t> input,
                      std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(input[offset]) << 8U) |
      static_cast<std::uint16_t>(input[offset + 1]));
}

std::uint32_t ReadU32(std::span<const std::uint8_t> input,
                      std::size_t offset) noexcept {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    value = (value << 8U) | input[offset + index];
  }
  return value;
}

std::uint64_t ReadU64(std::span<const std::uint8_t> input,
                      std::size_t offset) noexcept {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < 8; ++index) {
    value = (value << 8U) | input[offset + index];
  }
  return value;
}

WireDecodeError ValidateHeader(const WireHeader &header) noexcept {
  const auto flags = static_cast<std::uint8_t>(header.flags);
  if ((flags & static_cast<std::uint8_t>(~kKnownFlagMask)) != 0) {
    return WireDecodeError::kInvalidFlags;
  }
  if (static_cast<std::uint8_t>(header.semantic_class) >
      static_cast<std::uint8_t>(SemanticClass::kP2)) {
    return WireDecodeError::kInvalidSemanticClass;
  }
  if (header.source_symbol_count == 0 ||
      header.source_symbol_count > kMaxSourceSymbols) {
    return WireDecodeError::kInvalidSourceSymbolCount;
  }
  if (header.repair_symbol_count > header.source_symbol_count) {
    return WireDecodeError::kInvalidRepairSymbolCount;
  }
  if (header.symbol_index >= header.source_symbol_count) {
    return WireDecodeError::kInvalidSymbolIndex;
  }
  if (header.payload_bytes == 0) {
    return WireDecodeError::kInvalidPayloadLength;
  }
  return WireDecodeError::kNone;
}

WireDecodeResult Failure(WireDecodeError error) noexcept {
  return WireDecodeResult{
      .error = error,
      .header = std::nullopt,
      .payload = {},
  };
}

} // namespace

std::optional<std::array<std::uint8_t, kWireHeaderBytes>>
SerializeWireHeader(const WireHeader &header) noexcept {
  if (ValidateHeader(header) != WireDecodeError::kNone) {
    return std::nullopt;
  }

  std::array<std::uint8_t, kWireHeaderBytes> output{};
  std::copy(kMagic.begin(), kMagic.end(), output.begin());
  output[kWireVersionOffset] = kWireVersion;
  output[kWireFlagsOffset] = static_cast<std::uint8_t>(header.flags);
  output[kWireSemanticClassOffset] =
      static_cast<std::uint8_t>(header.semantic_class);
  output[7] = static_cast<std::uint8_t>(kWireHeaderBytes);
  WriteU64(output, 8, header.frame_id);
  WriteU32(output, 16, header.rtp_timestamp);
  WriteU32(output, 20, header.region_id);
  WriteU32(output, 24, header.block_id);
  WriteU16(output, kWireSourceSymbolCountOffset, header.source_symbol_count);
  WriteU16(output, kWireRepairSymbolCountOffset, header.repair_symbol_count);
  WriteU16(output, 32, header.symbol_index);
  WriteU16(output, 34, header.payload_bytes);
  WriteU64(output, 36, header.latency_token);
  WriteU32(output, 44, header.deadline_offset_ms);
  return output;
}

WireDecodeResult
ParseWirePacket(std::span<const std::uint8_t> packet) noexcept {
  if (packet.size() < kWireHeaderBytes) {
    return Failure(WireDecodeError::kTruncatedHeader);
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), packet.begin())) {
    return Failure(WireDecodeError::kBadMagic);
  }
  if (packet[kWireVersionOffset] != kWireVersion) {
    return Failure(WireDecodeError::kUnsupportedVersion);
  }
  if (packet[7] != kWireHeaderBytes) {
    return Failure(WireDecodeError::kInvalidHeaderLength);
  }

  WireHeader header{
      .flags = static_cast<WireFlags>(packet[kWireFlagsOffset]),
      .semantic_class =
          static_cast<SemanticClass>(packet[kWireSemanticClassOffset]),
      .frame_id = ReadU64(packet, 8),
      .rtp_timestamp = ReadU32(packet, 16),
      .region_id = ReadU32(packet, 20),
      .block_id = ReadU32(packet, 24),
      .source_symbol_count =
          ReadU16(packet, kWireSourceSymbolCountOffset),
      .repair_symbol_count =
          ReadU16(packet, kWireRepairSymbolCountOffset),
      .symbol_index = ReadU16(packet, 32),
      .payload_bytes = ReadU16(packet, 34),
      .latency_token = ReadU64(packet, 36),
      .deadline_offset_ms = ReadU32(packet, 44),
  };

  const WireDecodeError validation_error = ValidateHeader(header);
  if (validation_error != WireDecodeError::kNone) {
    return Failure(validation_error);
  }
  if (packet.size() - kWireHeaderBytes != header.payload_bytes) {
    return Failure(WireDecodeError::kPayloadLengthMismatch);
  }

  return WireDecodeResult{
      .error = WireDecodeError::kNone,
      .header = header,
      .payload = packet.subspan(kWireHeaderBytes),
  };
}

} // namespace bupt::volumetric

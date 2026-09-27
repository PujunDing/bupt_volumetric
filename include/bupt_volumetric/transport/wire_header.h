#ifndef BUPT_VOLUMETRIC_TRANSPORT_WIRE_HEADER_H_
#define BUPT_VOLUMETRIC_TRANSPORT_WIRE_HEADER_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "bupt_volumetric/model/semantic_class.h"

namespace bupt::volumetric {

inline constexpr std::size_t kWireHeaderBytes = 48;
inline constexpr std::size_t kWireVersionOffset = 4;
inline constexpr std::size_t kWireFlagsOffset = 5;
inline constexpr std::size_t kWireSemanticClassOffset = 6;
inline constexpr std::size_t kWireSourceSymbolCountOffset = 28;
inline constexpr std::size_t kWireRepairSymbolCountOffset = 30;
inline constexpr std::uint8_t kWireVersion = 1;

/** Identifies a source symbol's position within its semantic FEC block. */
enum class WireFlags : std::uint8_t {
  kNone = 0,
  kBlockBegin = 1U << 0U,
  kBlockEnd = 1U << 1U,
};

/** Combines independently meaningful wire flags. */
[[nodiscard]] constexpr WireFlags operator|(WireFlags left,
                                            WireFlags right) noexcept {
  return static_cast<WireFlags>(static_cast<std::uint8_t>(left) |
                                static_cast<std::uint8_t>(right));
}

/** Metadata carried by every OCTREE-SIM source RTP payload. */
struct WireHeader final {
  WireFlags flags = WireFlags::kNone;
  SemanticClass semantic_class = SemanticClass::kP0;
  std::uint64_t frame_id = 0;
  std::uint32_t rtp_timestamp = 0;
  std::uint32_t region_id = 0;
  std::uint32_t block_id = 0;
  std::uint16_t source_symbol_count = 0;
  std::uint16_t repair_symbol_count = 0;
  std::uint16_t symbol_index = 0;
  std::uint16_t payload_bytes = 0;
  std::uint64_t latency_token = 0;
  std::uint32_t deadline_offset_ms = 0;

  bool operator==(const WireHeader &) const = default;
};

/** Stable reasons why an untrusted OCTREE-SIM packet was rejected. */
enum class WireDecodeError : std::uint8_t {
  kNone = 0,
  kTruncatedHeader,
  kBadMagic,
  kUnsupportedVersion,
  kInvalidHeaderLength,
  kInvalidFlags,
  kInvalidSemanticClass,
  kInvalidSourceSymbolCount,
  kInvalidRepairSymbolCount,
  kInvalidSymbolIndex,
  kInvalidPayloadLength,
  kPayloadLengthMismatch,
};

/** Owns decoded metadata while exposing payload bytes from the caller's input. */
struct WireDecodeResult final {
  WireDecodeError error = WireDecodeError::kNone;
  std::optional<WireHeader> header;
  std::span<const std::uint8_t> payload;

  /** Returns true only when both metadata and payload passed validation. */
  [[nodiscard]] bool ok() const noexcept {
    return error == WireDecodeError::kNone && header.has_value();
  }
};

/**
 * Serializes validated metadata into the version-1 48-byte network header.
 *
 * Returns no value when a field violates the protocol bounds. The function
 * serializes metadata only; the caller appends exactly `payload_bytes` bytes.
 */
[[nodiscard]] std::optional<std::array<std::uint8_t, kWireHeaderBytes>>
SerializeWireHeader(const WireHeader &header) noexcept;

/**
 * Parses and validates one complete OCTREE-SIM source packet.
 *
 * The returned payload span aliases `packet`; callers must keep the packet
 * storage alive while using it.
 */
[[nodiscard]] WireDecodeResult
ParseWirePacket(std::span<const std::uint8_t> packet) noexcept;

} // namespace bupt::volumetric

#endif // BUPT_VOLUMETRIC_TRANSPORT_WIRE_HEADER_H_

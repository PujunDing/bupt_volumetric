#ifndef BUPT_VOLUMETRIC_TRANSPORT_SEMANTIC_PACKETIZER_H_
#define BUPT_VOLUMETRIC_TRANSPORT_SEMANTIC_PACKETIZER_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "bupt_volumetric/model/semantic_block.h"
#include "bupt_volumetric/model/simulated_octree_frame.h"
#include "bupt_volumetric/protection/fec_profile.h"

namespace bupt::volumetric {

/** Per-frame transport metadata copied into each source symbol. */
struct PacketizationContext final {
  std::uint32_t rtp_timestamp = 0;
  std::uint32_t region_id = 0;
  std::uint64_t latency_token = 0;
  std::uint32_t deadline_offset_ms = 0;
};

/** Stable reason why a semantic block could not be packetized. */
enum class PacketizationError : std::uint8_t {
  kNone = 0,
  kPayloadLimitTooSmall,
  kBlockOutsideFrame,
  kBlockPlanMismatch,
  kFecProfileMismatch,
  kFieldOutOfRange,
};

/** Complete source RTP payload records for one semantic block. */
struct PacketizationResult final {
  PacketizationError error = PacketizationError::kNone;
  std::vector<std::vector<std::uint8_t>> packets;

  /** Returns true only when packetization completed without rejection. */
  [[nodiscard]] bool ok() const noexcept {
    return error == PacketizationError::kNone;
  }
};

/** Converts one admitted semantic block into bounded OCTREE-SIM payloads. */
class SemanticPacketizer final {
public:
  /**
   * Packetizes one complete FEC block without assigning RTP sequence numbers.
   *
   * @param frame Owner of source bytes referenced by `block`.
   * @param block Atomic admission and FEC unit.
   * @param profile Source/repair allocation for this exact block.
   * @param context Metadata shared by the block's source symbols.
   * @param max_rtp_payload_bytes Limit supplied by the WebRTC packetizer.
   */
  [[nodiscard]] PacketizationResult
  Packetize(const SimulatedOctreeFrame &frame, const SemanticBlock &block,
            const FecProfile &profile, const PacketizationContext &context,
            std::size_t max_rtp_payload_bytes) const;
};

} // namespace bupt::volumetric

#endif // BUPT_VOLUMETRIC_TRANSPORT_SEMANTIC_PACKETIZER_H_

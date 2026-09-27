#ifndef BUPT_VOLUMETRIC_RTC_SEMANTIC_PAYLOAD_OBSERVER_ADAPTER_H_
#define BUPT_VOLUMETRIC_RTC_SEMANTIC_PAYLOAD_OBSERVER_ADAPTER_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>

#include "bupt_volumetric/receiver/block_assembler.h"
#include "video/rtp_video_stream_receiver2.h"

namespace bupt::volumetric {

// Bridges WebRTC's post-FEC, pre-assembly source-payload observer into the
// WebRTC-free BlockAssembler. Every source RTP payload delivered here has
// already passed the ULPFEC receiver, so `recovered` distinguishes packets that
// were reconstructed from repair from packets received directly. FEC repair
// packets never reach this observer (WebRTC only reports source payloads).
class SemanticPayloadObserverAdapter final
    : public webrtc::RtpVideoStreamReceiver2::PostFecSourcePayloadObserver {
 public:
  // `assembler` receives every validated source payload; `completed_blocks` is
  // incremented once per block that reaches kCompleted.
  SemanticPayloadObserverAdapter(
      std::shared_ptr<BlockAssembler> assembler,
      std::shared_ptr<std::atomic<std::uint64_t>> completed_blocks);

  void OnSourcePayload(std::span<const uint8_t> payload,
                       std::uint16_t sequence_number,
                       std::uint32_t rtp_timestamp,
                       bool recovered,
                       webrtc::Timestamp receive_time) override;

  [[nodiscard]] std::uint64_t total_packets() const noexcept;
  [[nodiscard]] std::uint64_t recovered_packets() const noexcept;
  [[nodiscard]] std::uint64_t malformed_packets() const noexcept;

 private:
  const std::shared_ptr<BlockAssembler> assembler_;
  const std::shared_ptr<std::atomic<std::uint64_t>> completed_blocks_;
  std::atomic<std::uint64_t> total_packets_{0};
  std::atomic<std::uint64_t> recovered_packets_{0};
  std::atomic<std::uint64_t> malformed_packets_{0};
};

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_RTC_SEMANTIC_PAYLOAD_OBSERVER_ADAPTER_H_

#include "bupt_volumetric/rtc/semantic_payload_observer_adapter.h"

#include <cstdint>
#include <memory>
#include <span>

#include "bupt_volumetric/receiver/block_assembler.h"

namespace bupt::volumetric {

SemanticPayloadObserverAdapter::SemanticPayloadObserverAdapter(
    std::shared_ptr<BlockAssembler> assembler,
    std::shared_ptr<std::atomic<std::uint64_t>> completed_blocks)
    : assembler_(std::move(assembler)),
      completed_blocks_(std::move(completed_blocks)) {}

void SemanticPayloadObserverAdapter::OnSourcePayload(
    std::span<const uint8_t> payload,
    std::uint16_t /*sequence_number*/,
    std::uint32_t /*rtp_timestamp*/,
    bool recovered,
    webrtc::Timestamp receive_time) {
  total_packets_.fetch_add(1, std::memory_order_relaxed);
  if (recovered) {
    recovered_packets_.fetch_add(1, std::memory_order_relaxed);
  }

  const BlockAssemblyResult result =
      assembler_->Accept(payload, recovered, receive_time.ms());
  switch (result.status) {
    case BlockAssemblyStatus::kCompleted:
      if (completed_blocks_) {
        completed_blocks_->fetch_add(1, std::memory_order_relaxed);
      }
      break;
    case BlockAssemblyStatus::kMalformed:
      malformed_packets_.fetch_add(1, std::memory_order_relaxed);
      break;
    default:
      break;
  }
}

std::uint64_t SemanticPayloadObserverAdapter::total_packets() const noexcept {
  return total_packets_.load(std::memory_order_relaxed);
}

std::uint64_t SemanticPayloadObserverAdapter::recovered_packets() const noexcept {
  return recovered_packets_.load(std::memory_order_relaxed);
}

std::uint64_t SemanticPayloadObserverAdapter::malformed_packets() const noexcept {
  return malformed_packets_.load(std::memory_order_relaxed);
}

}  // namespace bupt::volumetric

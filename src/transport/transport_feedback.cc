#include "bupt_volumetric/transport/transport_feedback.h"

#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>

namespace bupt::volumetric {

std::optional<std::uint64_t> ActualBitrateSampler::Sample(
    std::uint64_t outbound_bytes, std::uint64_t observed_at_ms) noexcept {
  if (!previous_bytes_.has_value() || !previous_time_ms_.has_value()) {
    previous_bytes_ = outbound_bytes;
    previous_time_ms_ = observed_at_ms;
    return std::nullopt;
  }

  const std::uint64_t previous_bytes = *previous_bytes_;
  const std::uint64_t previous_time_ms = *previous_time_ms_;
  previous_bytes_ = outbound_bytes;
  previous_time_ms_ = observed_at_ms;

  if (outbound_bytes < previous_bytes || observed_at_ms <= previous_time_ms) {
    return std::nullopt;
  }

  const std::uint64_t byte_delta = outbound_bytes - previous_bytes;
  const std::uint64_t time_delta_ms = observed_at_ms - previous_time_ms;
  if (byte_delta > std::numeric_limits<std::uint64_t>::max() / 8'000) {
    return std::nullopt;
  }
  return byte_delta * 8'000 / time_delta_ms;
}

void TransportFeedbackStore::OnRateUpdate(
    const EncoderRateUpdate& update) {
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.target_bitrate_bps = update.target_bitrate_bps;
  snapshot_.target_framerate_fps = update.target_framerate_fps;
}

void TransportFeedbackStore::OnStats(
    const RtcTransportObservation& observation) {
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.selected_pair_rtt_ms = observation.selected_pair_rtt_ms;
  snapshot_.available_outgoing_bitrate_bps =
      observation.available_outgoing_bitrate_bps;
  snapshot_.outbound_bytes = observation.outbound_bytes;
  snapshot_.outbound_packets = observation.outbound_packets;
  snapshot_.retransmitted_bytes = observation.retransmitted_bytes;
  snapshot_.retransmitted_packets = observation.retransmitted_packets;
  snapshot_.total_packet_send_delay_ms =
      observation.total_packet_send_delay_ms;
  snapshot_.loss_fraction = observation.loss_fraction;
  snapshot_.jitter_ms = observation.jitter_ms;
  snapshot_.remote_round_trip_time_ms =
      observation.remote_round_trip_time_ms;
  snapshot_.pacer_delay_ms = observation.pacer_delay_ms;
  snapshot_.fec_packets_sent = observation.fec_packets_sent;
  snapshot_.fec_bytes_sent = observation.fec_bytes_sent;
  snapshot_.fec_packets_received = observation.fec_packets_received;
  snapshot_.fec_bytes_received = observation.fec_bytes_received;
  snapshot_.actual_sent_bitrate_bps =
      observation.outbound_bytes.has_value()
          ? bitrate_sampler_.Sample(*observation.outbound_bytes,
                                    observation.observed_at_ms)
          : std::nullopt;
}

TransportFeedbackSnapshot TransportFeedbackStore::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_;
}

}  // namespace bupt::volumetric

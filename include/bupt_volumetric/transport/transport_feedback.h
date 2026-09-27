#ifndef BUPT_VOLUMETRIC_TRANSPORT_TRANSPORT_FEEDBACK_H_
#define BUPT_VOLUMETRIC_TRANSPORT_TRANSPORT_FEEDBACK_H_

#include <cstdint>
#include <mutex>
#include <optional>

namespace bupt::volumetric {

/** One target-rate update delivered by WebRTC's encoder controller. */
struct EncoderRateUpdate final {
  std::uint64_t target_bitrate_bps = 0;
  double target_framerate_fps = 0.0;
  std::uint64_t observed_at_ms = 0;
};

/** Nullable transport measurements extracted from one RTCStats report. */
struct RtcTransportObservation final {
  std::uint64_t observed_at_ms = 0;
  std::optional<double> selected_pair_rtt_ms;
  std::optional<std::uint64_t> available_outgoing_bitrate_bps;
  std::optional<std::uint64_t> outbound_bytes;
  std::optional<std::uint64_t> outbound_packets;
  std::optional<std::uint64_t> retransmitted_bytes;
  std::optional<std::uint64_t> retransmitted_packets;
  std::optional<double> total_packet_send_delay_ms;
  std::optional<double> loss_fraction;
  std::optional<double> jitter_ms;
  std::optional<double> remote_round_trip_time_ms;
  std::optional<double> pacer_delay_ms;
  // Send-side native FEC observation (Task 12 non-standard provisional stats):
  // ULPFEC repair packets/bytes actually sent on the outbound stream.
  std::optional<std::uint64_t> fec_packets_sent;
  std::optional<std::uint64_t> fec_bytes_sent;
  // Receive-side FEC observation from RTCInboundRtpStreamStats. NOTE: WebRTC
  // populates this only for FlexFEC, not ULPFEC, so it stays nullopt for the
  // Task 12/13 ULPFEC path; wiring UlpfecReceiver::GetPacketCounter() into
  // receive stats is deferred to Task 13.
  std::optional<std::uint64_t> fec_packets_received;
  std::optional<std::uint64_t> fec_bytes_received;
};

/** Immutable-by-value view of the latest control and observation state. */
struct TransportFeedbackSnapshot final {
  std::uint64_t target_bitrate_bps = 0;
  double target_framerate_fps = 0.0;
  std::optional<double> selected_pair_rtt_ms;
  std::optional<std::uint64_t> available_outgoing_bitrate_bps;
  std::optional<std::uint64_t> outbound_bytes;
  std::optional<std::uint64_t> outbound_packets;
  std::optional<std::uint64_t> retransmitted_bytes;
  std::optional<std::uint64_t> retransmitted_packets;
  std::optional<double> total_packet_send_delay_ms;
  std::optional<double> loss_fraction;
  std::optional<double> jitter_ms;
  std::optional<double> remote_round_trip_time_ms;
  std::optional<double> pacer_delay_ms;
  std::optional<std::uint64_t> actual_sent_bitrate_bps;
  std::optional<std::uint64_t> fec_packets_sent;
  std::optional<std::uint64_t> fec_bytes_sent;
  std::optional<std::uint64_t> fec_packets_received;
  std::optional<std::uint64_t> fec_bytes_received;
};

/** Consumer of WebRTC encoder target-rate changes. */
class RateUpdateSink {
 public:
  virtual ~RateUpdateSink() = default;
  virtual void OnRateUpdate(const EncoderRateUpdate& update) = 0;
};

/** Derives bits/second from monotonic time and cumulative byte counters. */
class ActualBitrateSampler final {
 public:
  [[nodiscard]] std::optional<std::uint64_t> Sample(
      std::uint64_t outbound_bytes, std::uint64_t observed_at_ms) noexcept;

 private:
  std::optional<std::uint64_t> previous_bytes_;
  std::optional<std::uint64_t> previous_time_ms_;
};

/** Thread-safe merger for high-frequency rate and low-frequency stats paths. */
class TransportFeedbackStore final : public RateUpdateSink {
 public:
  void OnRateUpdate(const EncoderRateUpdate& update) override;
  void OnStats(const RtcTransportObservation& observation);
  [[nodiscard]] TransportFeedbackSnapshot Snapshot() const;

 private:
  mutable std::mutex mutex_;
  TransportFeedbackSnapshot snapshot_;
  ActualBitrateSampler bitrate_sampler_;
};

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_TRANSPORT_TRANSPORT_FEEDBACK_H_

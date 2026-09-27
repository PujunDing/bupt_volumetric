#include "bupt_volumetric/rtc/rtc_stats_mapper.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

#include "api/stats/rtcstats_objects.h"

namespace bupt::volumetric {
namespace {

constexpr double kMillisecondsPerSecond = 1000.0;

std::optional<double> NonNegativeMilliseconds(double seconds) noexcept {
  if (!std::isfinite(seconds) || seconds < 0.0) {
    return std::nullopt;
  }
  return seconds * kMillisecondsPerSecond;
}

std::optional<std::uint64_t> NonNegativeBitrate(double bitrate) noexcept {
  constexpr double kMaximum =
      static_cast<double>(std::numeric_limits<std::uint64_t>::max());
  if (!std::isfinite(bitrate) || bitrate < 0.0 || bitrate > kMaximum) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(bitrate);
}

template <typename Value>
void AddCounter(const Value& member,
                std::optional<std::uint64_t>& total) noexcept {
  if (!member.has_value()) {
    return;
  }

  const std::uint64_t value = static_cast<std::uint64_t>(*member);
  if (!total.has_value()) {
    total = value;
    return;
  }
  if (value > std::numeric_limits<std::uint64_t>::max() - *total) {
    total.reset();
    return;
  }
  *total += value;
}

template <typename Value>
void AddSeconds(const Value& member, std::optional<double>& total_ms) noexcept {
  if (!member.has_value()) {
    return;
  }
  const std::optional<double> milliseconds =
      NonNegativeMilliseconds(static_cast<double>(*member));
  if (!milliseconds.has_value()) {
    return;
  }
  total_ms = total_ms.value_or(0.0) + *milliseconds;
}

struct Mean final {
  void Add(double value) noexcept {
    if (!std::isfinite(value)) {
      return;
    }
    sum += value;
    ++count;
  }

  [[nodiscard]] std::optional<double> Value() const noexcept {
    if (count == 0) {
      return std::nullopt;
    }
    return sum / static_cast<double>(count);
  }

  double sum = 0.0;
  std::uint64_t count = 0;
};

}  // namespace

RtcTransportObservation MapRtcStats(
    const webrtc::RTCStatsReport& report) noexcept {
  RtcTransportObservation observation;
  const std::int64_t report_time_ms = report.timestamp().ms();
  if (report_time_ms >= 0) {
    observation.observed_at_ms = static_cast<std::uint64_t>(report_time_ms);
  }

  for (const webrtc::RTCTransportStats* transport :
       report.GetStatsOfType<webrtc::RTCTransportStats>()) {
    if (!transport->selected_candidate_pair_id.has_value()) {
      continue;
    }
    const auto* pair = report.GetAs<webrtc::RTCIceCandidatePairStats>(
        *transport->selected_candidate_pair_id);
    if (pair == nullptr) {
      continue;
    }
    if (pair->current_round_trip_time.has_value()) {
      observation.selected_pair_rtt_ms = NonNegativeMilliseconds(
          static_cast<double>(*pair->current_round_trip_time));
    }
    if (pair->available_outgoing_bitrate.has_value()) {
      observation.available_outgoing_bitrate_bps = NonNegativeBitrate(
          static_cast<double>(*pair->available_outgoing_bitrate));
    }
    if (pair->pacer_delay.has_value()) {
      observation.pacer_delay_ms = NonNegativeMilliseconds(
          static_cast<double>(*pair->pacer_delay));
    }
    break;
  }

  for (const webrtc::RTCOutboundRtpStreamStats* outbound :
       report.GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>()) {
    if (!outbound->kind.has_value() || *outbound->kind != "video") {
      continue;
    }
    AddCounter(outbound->bytes_sent, observation.outbound_bytes);
    AddCounter(outbound->packets_sent, observation.outbound_packets);
    AddCounter(outbound->retransmitted_bytes_sent,
               observation.retransmitted_bytes);
    AddCounter(outbound->retransmitted_packets_sent,
               observation.retransmitted_packets);
    AddCounter(outbound->fec_packets_sent, observation.fec_packets_sent);
    AddCounter(outbound->fec_bytes_sent, observation.fec_bytes_sent);
    AddSeconds(outbound->total_packet_send_delay,
               observation.total_packet_send_delay_ms);
  }

  for (const webrtc::RTCInboundRtpStreamStats* inbound :
       report.GetStatsOfType<webrtc::RTCInboundRtpStreamStats>()) {
    if (!inbound->kind.has_value() || *inbound->kind != "video") {
      continue;
    }
    AddCounter(inbound->fec_packets_received, observation.fec_packets_received);
    AddCounter(inbound->fec_bytes_received, observation.fec_bytes_received);
  }

  Mean loss;
  Mean jitter_ms;
  Mean round_trip_time_ms;
  for (const webrtc::RTCRemoteInboundRtpStreamStats* remote :
       report.GetStatsOfType<webrtc::RTCRemoteInboundRtpStreamStats>()) {
    if (!remote->kind.has_value() || *remote->kind != "video") {
      continue;
    }
    if (remote->fraction_lost.has_value()) {
      const double value = static_cast<double>(*remote->fraction_lost);
      if (value >= 0.0 && value <= 1.0) {
        loss.Add(value);
      }
    }
    if (remote->jitter.has_value()) {
      const std::optional<double> value =
          NonNegativeMilliseconds(static_cast<double>(*remote->jitter));
      if (value.has_value()) {
        jitter_ms.Add(*value);
      }
    }
    if (remote->round_trip_time.has_value()) {
      const std::optional<double> value = NonNegativeMilliseconds(
          static_cast<double>(*remote->round_trip_time));
      if (value.has_value()) {
        round_trip_time_ms.Add(*value);
      }
    }
  }
  observation.loss_fraction = loss.Value();
  observation.jitter_ms = jitter_ms.Value();
  observation.remote_round_trip_time_ms = round_trip_time_ms.Value();

  return observation;
}

}  // namespace bupt::volumetric

#ifndef BUPT_VOLUMETRIC_METRICS_METRIC_EVENT_H_
#define BUPT_VOLUMETRIC_METRICS_METRIC_EVENT_H_

#include <cstdint>
#include <optional>
#include <string>

namespace bupt::volumetric {

// Provenance is fixed per field definition so consumers never mislabel an
// estimate as a measurement.
enum class MetricProvenance : std::uint8_t {
  kObserved = 0,
  kDerived = 1,
  kEstimated = 2,
};

// One congestion-control observation of the semantic admission loop. The
// pacer delay is observed, the actual sent bitrate is derived from byte-counter
// deltas, and the transport overhead is estimated.
struct CongestionMetricEvent final {
  std::string run_id;
  std::uint64_t monotonic_time_ms = 0;
  std::uint64_t target_bitrate_bps = 0;
  std::uint64_t admitted_p0_bytes = 0;
  std::uint64_t admitted_p1_bytes = 0;
  std::uint64_t admitted_p2_bytes = 0;
  std::uint64_t dropped_p0_bytes = 0;
  std::uint64_t dropped_p1_bytes = 0;
  std::uint64_t dropped_p2_bytes = 0;
  std::optional<double> pacer_delay_ms;
  std::optional<std::uint64_t> actual_sent_bitrate_bps;
  std::uint64_t estimated_transport_overhead_bytes = 0;
};

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_METRICS_METRIC_EVENT_H_

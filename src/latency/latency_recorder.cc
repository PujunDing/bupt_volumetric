#include "bupt_volumetric/latency/latency_recorder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace bupt::volumetric {
namespace {

double Quantile(const std::vector<double>& sorted, double percentile) {
  if (sorted.empty()) {
    return 0.0;
  }
  if (sorted.size() == 1) {
    return sorted[0];
  }
  const double position = percentile * static_cast<double>(sorted.size() - 1);
  const std::size_t lower = static_cast<std::size_t>(position);
  const std::size_t upper = lower + 1;
  const double fraction = position - static_cast<double>(lower);
  return sorted[lower] * (1.0 - fraction) + sorted[upper] * fraction;
}

std::string FormatMs(double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.1f", value);
  return buffer;
}

}  // namespace

LatencyRecorder::LatencyRecorder(std::size_t max_samples)
    : max_samples_(max_samples == 0 ? 1 : max_samples) {}

void LatencyRecorder::Record(double latency_ms) noexcept {
  if (!std::isfinite(latency_ms) || latency_ms < 0.0) {
    return;
  }
  if (samples_.size() == max_samples_) {
    samples_.pop_front();
    ++dropped_count_;
  }
  samples_.push_back(latency_ms);
}

LatencyStats LatencyRecorder::stats() const noexcept {
  LatencyStats result;
  result.count = static_cast<std::uint64_t>(samples_.size());
  if (samples_.empty()) {
    return result;
  }
  std::vector<double> sorted(samples_.begin(), samples_.end());
  std::sort(sorted.begin(), sorted.end());
  result.min_ms = sorted.front();
  result.max_ms = sorted.back();
  result.p50_ms = Quantile(sorted, 0.50);
  result.p90_ms = Quantile(sorted, 0.90);
  result.p99_ms = Quantile(sorted, 0.99);
  return result;
}

std::uint64_t LatencyRecorder::dropped_count() const noexcept {
  return dropped_count_;
}

std::string LatencyRecorder::FormatOverlay() const {
  const LatencyStats s = stats();
  if (s.count == 0) {
    return "latency n/a";
  }
  return "latency p50=" + FormatMs(s.p50_ms) + " p90=" + FormatMs(s.p90_ms) +
         " p99=" + FormatMs(s.p99_ms) + " ms (n=" +
         std::to_string(s.count) + ")";
}

void LatencySampleQueue::Push(std::uint64_t latency_us) {
  std::lock_guard<std::mutex> lock(mutex_);
  samples_.push_back(latency_us);
}

std::vector<std::uint64_t> LatencySampleQueue::Drain() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::uint64_t> drained(samples_.begin(), samples_.end());
  samples_.clear();
  return drained;
}

std::size_t LatencySampleQueue::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return samples_.size();
}

}  // namespace bupt::volumetric

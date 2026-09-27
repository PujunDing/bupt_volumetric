#include "bupt_volumetric/transport/simulated_access_unit.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace bupt::volumetric {
namespace {

constexpr std::uint32_t kFnvOffsetBasis = 2'166'136'261u;
constexpr std::uint32_t kFnvPrime = 16'777'619u;

void WriteU32(std::span<std::uint8_t> output, std::size_t offset,
              std::uint32_t value) {
  for (std::size_t index = 0; index < 4; ++index) {
    output[offset + index] =
        static_cast<std::uint8_t>(value >> ((3U - index) * 8U));
  }
}

void WriteU64(std::span<std::uint8_t> output, std::size_t offset,
              std::uint64_t value) {
  for (std::size_t index = 0; index < 8; ++index) {
    output[offset + index] =
        static_cast<std::uint8_t>(value >> ((7U - index) * 8U));
  }
}

std::uint32_t ReadU32(std::span<const std::uint8_t> input,
                      std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    value = (value << 8U) | input[offset + index];
  }
  return value;
}

std::uint64_t ReadU64(std::span<const std::uint8_t> input,
                      std::size_t offset) {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < 8; ++index) {
    value = (value << 8U) | input[offset + index];
  }
  return value;
}

std::uint32_t Checksum(std::span<const std::uint8_t> payload) {
  std::uint32_t hash = kFnvOffsetBasis;
  for (const std::uint8_t byte : payload) {
    hash ^= byte;
    hash *= kFnvPrime;
  }
  return hash;
}

SimulatedAccessUnitParseResult Failure(SimulatedAccessUnitError error) {
  return SimulatedAccessUnitParseResult{.error = error, .unit = std::nullopt};
}

}  // namespace

std::optional<std::vector<std::uint8_t>> SerializeSimulatedAccessUnit(
    const SimulatedAccessUnit& unit) {
  if (unit.payload.size() > kMaxSimulatedAccessUnitPayloadBytes) {
    return std::nullopt;
  }

  std::vector<std::uint8_t> wire(kSimulatedAccessUnitHeaderBytes +
                                 unit.payload.size());
  WriteU32(wire, 0, kOctreeSimMagic);
  wire[4] = kOctreeSimVersion;
  WriteU64(wire, 8, unit.frame_id);
  WriteU64(wire, 16, unit.capture_time_us);
  WriteU32(wire, 24, unit.rtp_timestamp);
  WriteU32(wire, 28, static_cast<std::uint32_t>(unit.payload.size()));
  WriteU32(wire, 32, Checksum(unit.payload));
  std::copy(unit.payload.begin(), unit.payload.end(),
            wire.begin() + kSimulatedAccessUnitHeaderBytes);
  return wire;
}

SimulatedAccessUnitParseResult ParseSimulatedAccessUnit(
    std::span<const std::uint8_t> wire) {
  if (wire.size() < kSimulatedAccessUnitHeaderBytes) {
    return Failure(SimulatedAccessUnitError::kTruncatedHeader);
  }
  if (ReadU32(wire, 0) != kOctreeSimMagic) {
    return Failure(SimulatedAccessUnitError::kBadMagic);
  }
  if (wire[4] != kOctreeSimVersion) {
    return Failure(SimulatedAccessUnitError::kUnsupportedVersion);
  }

  const std::uint32_t payload_size = ReadU32(wire, 28);
  if (payload_size > kMaxSimulatedAccessUnitPayloadBytes) {
    return Failure(SimulatedAccessUnitError::kPayloadTooLarge);
  }
  if (wire.size() - kSimulatedAccessUnitHeaderBytes != payload_size) {
    return Failure(SimulatedAccessUnitError::kPayloadLengthMismatch);
  }

  const std::span<const std::uint8_t> payload =
      wire.subspan(kSimulatedAccessUnitHeaderBytes);
  if (Checksum(payload) != ReadU32(wire, 32)) {
    return Failure(SimulatedAccessUnitError::kChecksumMismatch);
  }

  return SimulatedAccessUnitParseResult{
      .error = SimulatedAccessUnitError::kNone,
      .unit = SimulatedAccessUnit{
          .frame_id = ReadU64(wire, 8),
          .rtp_timestamp = ReadU32(wire, 24),
          .capture_time_us = ReadU64(wire, 16),
          .payload = std::vector<std::uint8_t>(payload.begin(), payload.end()),
      },
  };
}

SimulatedAccessUnitQueue::SimulatedAccessUnitQueue(std::size_t capacity)
    : capacity_(capacity) {
  if (capacity == 0) {
    throw std::invalid_argument("access-unit queue capacity must be positive");
  }
}

void SimulatedAccessUnitQueue::Push(SimulatedAccessUnit unit) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (units_.size() == capacity_) {
    units_.pop_front();
    ++evicted_count_;
  }
  units_.push_back(std::move(unit));
}

std::optional<SimulatedAccessUnit> SimulatedAccessUnitQueue::Pop(
    std::uint32_t rtp_timestamp) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto match =
      std::find_if(units_.begin(), units_.end(),
                   [rtp_timestamp](const SimulatedAccessUnit& unit) {
                     return unit.rtp_timestamp == rtp_timestamp;
                   });
  if (match == units_.end()) {
    return std::nullopt;
  }
  SimulatedAccessUnit unit = std::move(*match);
  units_.erase(match);
  return unit;
}

std::optional<SimulatedAccessUnit> SimulatedAccessUnitQueue::PopOldest() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (units_.empty()) {
    return std::nullopt;
  }
  SimulatedAccessUnit unit = std::move(units_.front());
  units_.pop_front();
  return unit;
}

std::size_t SimulatedAccessUnitQueue::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return units_.size();
}

std::uint64_t SimulatedAccessUnitQueue::evicted_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return evicted_count_;
}

}  // namespace bupt::volumetric

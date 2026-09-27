#include "bupt_volumetric/simulation/frame_generator.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace bupt::volumetric {
namespace {

// SplitMix64 is used only as a stable byte-pattern generator. Its compact,
// fully specified integer operations make experiment fixtures reproducible
// across machines; no security property is required here.
std::uint64_t NextPatternWord(std::uint64_t &state) noexcept {
  state += 0x9E3779B97F4A7C15ULL;
  std::uint64_t value = state;
  value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31);
}

} // namespace

SimulatedOctreeFrame
SimulatedFrameGenerator::Generate(std::uint64_t frame_id,
                                  std::uint64_t seed) const {
  SimulatedOctreeFrame::Payload payload(kPayloadBytes);
  std::uint64_t state = seed ^ (frame_id * 0xD6E8FEB86659FD93ULL);

  std::size_t offset = 0;
  while (offset < payload.size()) {
    const std::uint64_t word = NextPatternWord(state);
    for (unsigned int byte_index = 0;
         byte_index < sizeof(word) && offset < payload.size(); ++byte_index) {
      payload[offset++] = static_cast<std::uint8_t>(word >> (byte_index * 8U));
    }
  }

  return SimulatedOctreeFrame(frame_id, std::move(payload));
}

} // namespace bupt::volumetric

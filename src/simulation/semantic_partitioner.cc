#include "bupt_volumetric/simulation/semantic_partitioner.h"

#include <cstddef>
#include <stdexcept>
#include <vector>

#include "bupt_volumetric/model/semantic_class.h"

namespace bupt::volumetric {
namespace {

constexpr std::size_t kP0Bytes = 250'000;
constexpr std::size_t kP1Bytes = 750'000;
constexpr std::size_t kP2Bytes = 1'000'000;
constexpr std::size_t kExpectedFrameBytes = kP0Bytes + kP1Bytes + kP2Bytes;

} // namespace

std::vector<SemanticRegion>
SemanticPartitioner::Partition(const SimulatedOctreeFrame &frame) const {
  if (frame.payload().size() != kExpectedFrameBytes) {
    throw std::invalid_argument(
        "SemanticPartitioner requires exactly 2,000,000 payload bytes");
  }
  return {
      SemanticRegion(SemanticClass::kP0, 0, kP0Bytes),
      SemanticRegion(SemanticClass::kP1, kP0Bytes, kP1Bytes),
      SemanticRegion(SemanticClass::kP2, kP0Bytes + kP1Bytes, kP2Bytes),
  };
}

} // namespace bupt::volumetric

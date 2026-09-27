#ifndef BUPT_VOLUMETRIC_MODEL_SIMULATED_OCTREE_FRAME_H_
#define BUPT_VOLUMETRIC_MODEL_SIMULATED_OCTREE_FRAME_H_

#include <cstdint>
#include <vector>

namespace bupt::volumetric {

// Owns the deterministic source bytes for one simulated encoded point-cloud
// frame. This type is deliberately codec-neutral: its payload is test data,
// not a G-PCC, V-PCC, or other standardized bitstream.
class SimulatedOctreeFrame final {
public:
  using Payload = std::vector<std::uint8_t>;

  /**
   * Creates an immutable-identity simulated frame.
   *
   * @param frame_id Monotonically increasing application frame identifier.
   * @param payload Source payload bytes; metadata and transport headers are
   *        intentionally excluded from this byte count.
   */
  SimulatedOctreeFrame(std::uint64_t frame_id, Payload payload);

  /** Returns the application frame identifier. */
  [[nodiscard]] std::uint64_t frame_id() const noexcept;

  /**
   * Returns the owned source payload without copying it.
   *
   * @return A reference valid for the lifetime of this frame.
   */
  [[nodiscard]] const Payload &payload() const noexcept;

private:
  std::uint64_t frame_id_;
  Payload payload_;
};

} // namespace bupt::volumetric

#endif // BUPT_VOLUMETRIC_MODEL_SIMULATED_OCTREE_FRAME_H_

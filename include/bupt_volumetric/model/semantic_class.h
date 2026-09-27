#ifndef BUPT_VOLUMETRIC_MODEL_SEMANTIC_CLASS_H_
#define BUPT_VOLUMETRIC_MODEL_SEMANTIC_CLASS_H_

#include <cstdint>

namespace bupt::volumetric {

// Expresses spatial-semantic value, from indispensable structure to optional
// detail. It must not be translated into video keyframe or temporal-layer
// flags.
enum class SemanticClass : std::uint8_t {
  kP0 = 0,
  kP1 = 1,
  kP2 = 2,
};

} // namespace bupt::volumetric

#endif // BUPT_VOLUMETRIC_MODEL_SEMANTIC_CLASS_H_

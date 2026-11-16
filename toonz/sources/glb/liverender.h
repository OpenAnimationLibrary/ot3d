#pragma once

#include "glbrenderer.h"
#include <atomic>

namespace otglb {
// GPU-ready, immutable camera-view batches. The initial live path retains the
// existing per-source camera/depth conventions; it is not a shared world scene.
struct LiveVertex {
  float position[4];  // Projected x/y, normalized depth, clip w.
  float normal[3], view[3], color[4], reflection[3], uv[2];
};
struct LiveBatch {
  std::vector<LiveVertex> vertices;
  std::vector<EnvironmentLayer> reflections;
  std::shared_ptr<const std::vector<ColorPixel>> texture;
  int textureWidth = 0, textureHeight = 0;
  std::array<float, 4> colorScale{{0, 0, 0, 1}};
  std::array<double, 6> affine{{1, 0, 0, 0, 1, 0}};
  std::size_t opaqueVertices = 0;
  bool wireframe             = false;
};
struct LiveFrame {
  static constexpr std::size_t MaxVertices    = 1500000;
  static constexpr std::size_t MaxReflections = 4;
  std::vector<LiveBatch> batches;
  std::vector<std::string> warnings;
};
// No GL or Qt calls. Shade local lights/IBL at vertices, leave reflected
// environment lookup for the fragment shader. Throws rather than truncating.
LiveFrame prepareLiveFrame(const RenderScene &scene,
                           const int *canceled               = nullptr,
                           const std::atomic<bool> *obsolete = nullptr);
}  // namespace otglb

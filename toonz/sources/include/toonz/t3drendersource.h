#pragma once

#include "glbrenderer.h"
#include <memory>
class TRenderSettings;

// Internal contract for FX nodes that can provide an unevaluated 3D render
// result to another 3D-aware FX. It is deliberately separate from raster
// compute so lighting can be applied before the model is flattened to pixels.
class T3DRenderSource {
public:
  virtual ~T3DRenderSource() = default;

  virtual std::shared_ptr<const otglb::RenderScene> get3DRenderScene(
      double frame, const int *canceled,
      const otglb::LightingRig *lighting                   = nullptr,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *renderSettings                = nullptr) const = 0;

  // Bounds queries must not evaluate a raster input outside a render session.
  virtual std::shared_ptr<const otglb::RenderScene> get3DRenderGeometry(
      double frame, const int *canceled,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *renderSettings                = nullptr) const {
    return get3DRenderScene(frame, canceled, nullptr, transforms,
                            renderSettings);
  }
};

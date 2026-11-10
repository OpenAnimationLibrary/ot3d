#pragma once

#include "glbrenderer.h"
#include "ammaterial.h"
#include "amink.h"
#include <memory>
class TRenderSettings;
class TAffine;

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
      const TRenderSettings *renderSettings                = nullptr,
      const otam::MaterialBindings *materials              = nullptr,
      const otam::InkBindings *ink                         = nullptr) const = 0;

  // Bounds queries must not evaluate a raster input outside a render session.
  virtual std::shared_ptr<const otglb::RenderScene> get3DRenderGeometry(
      double frame, const int *canceled,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *renderSettings                = nullptr,
      const otam::InkBindings *ink                         = nullptr) const {
    return get3DRenderScene(frame, canceled, nullptr, transforms,
                            renderSettings, nullptr, ink);
  }
};

// Optional render-tree contract for multi-input 3D groups. Column placement
// must stay metadata, rather than becoming a raster-only affine input node.
class T3DRenderGroup {
public:
  virtual ~T3DRenderGroup()                                       = default;
  virtual void set3DInputAffine(int input, const TAffine &affine) = 0;
};

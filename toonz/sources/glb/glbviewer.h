#pragma once

#include "glbrenderer.h"

namespace otglb {

// Unprojected geometry for an interactive Viewer. The Viewer owns its camera,
// depth buffer and GL state. No raster render or graphics context is used here.
struct ViewerTriangle {
  std::array<std::array<double, 3>, 3> vertices;
  std::array<float, 3> color;
};

struct ViewerGeometry {
  std::vector<ViewerTriangle> triangles;
  std::vector<std::string> warnings;
  double stageUnitsPerModelUnit = 100.0;
  bool wireframe                = false;
};

// Opaque triangle lists, strips, fans and node animation are supported
// initially. Skinning, morphs, textures and transparent primitives are reported
// explicitly. Capacity is checked before emitting triangles; partial results
// never escape.
ViewerGeometry prepareViewerGeometry(const Asset &asset,
                                     const RenderOptions &options,
                                     std::size_t maxTriangles = 250000);

}  // namespace otglb

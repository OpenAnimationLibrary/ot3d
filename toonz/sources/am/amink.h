#pragma once

#include "ammodel.h"

namespace otam {
struct InkStyle {
  std::array<double, 3> widths{{3, 3, 3}};
  std::array<std::array<float, 4>, 3> colors{
      {{{0, 0, 0, 1}}, {{0, 0, 0, 1}}, {{0, 0, 0, 1}}}};
  double middle    = 0.5;
  double roughness = 0;
  double fadeIn = 0, fadeOut = 0;
  bool unfilledOnly = false;
  std::string group;
};
using InkBindings = std::vector<InkStyle>;

struct InkTriangle {
  std::array<Vec3, 3> vertices;
  std::array<std::array<float, 4>, 3> colors;  // Premultiplied sRGB.
};

// Ink widths are in the image plane. The centerline retains model depth so
// it can be occluded by filled geometry. A null view uses preprojected input.
std::vector<InkTriangle> strokeSplines(const Mesh &mesh,
                                       const InkBindings &bindings,
                                       const Matrix *view,
                                       const Matrix &imageToStage,
                                       const int *canceled = nullptr);
}  // namespace otam

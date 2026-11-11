#pragma once

#include "ammodel.h"

#include <optional>
#include <utility>

namespace otam {
using InkColor     = std::array<float, 4>;
using InkWidthSpan = std::array<std::array<double, 2>, 4>;
struct InkProfile {
  std::vector<InkWidthSpan> width;
  std::vector<std::pair<double, InkColor>> colors;
  bool linear     = false;
  bool perSection = false;
};

struct InkStyle {
  std::array<double, 3> widths{{3, 3, 3}};
  std::array<std::array<float, 4>, 3> colors{
      {{{0, 0, 0, 1}}, {{0, 0, 0, 1}}, {{0, 0, 0, 1}}}};
  double middle    = 0.5;
  double roughness = 0;
  double fadeIn = 0, fadeOut = 0;
  bool unfilledOnly = false;
  std::string group;
  std::optional<InkProfile> profile;
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

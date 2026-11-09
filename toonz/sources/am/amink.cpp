#include "amink.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace otam {
namespace {
struct StrokePoint {
  Vec3 position;
  double progress;
};
using XY                                 = std::array<double, 2>;
using InkColor                           = std::array<float, 4>;
constexpr std::size_t MaxStrokeTriangles = 1000000;
constexpr double Turn                    = 6.283185307179586;

XY normal(const Vec3 &a, const Vec3 &b) {
  const double dx = b[0] - a[0], dy = b[1] - a[1];
  const double length = std::hypot(dx, dy);
  return length > 1e-12 ? XY{{-dy / length, dx / length}} : XY{};
}

void emitStroke(const std::vector<StrokePoint> &points, const InkStyle &style,
                std::size_t spline, bool closed, const Matrix &imageToStage,
                std::vector<InkTriangle> &output) {
  if (points.size() < 2) return;
  std::array<Vec3, 2> previous{};
  InkColor previousColor{};
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto &p       = points[i];
    const double u      = p.progress;
    const double middle = style.middle;
    const int stop      = u < middle ? 0 : 1;
    const double blend  = stop == 0 ? u / middle : (u - middle) / (1 - middle);
    const double width =
        style.widths[stop] * (1 - blend) + style.widths[stop + 1] * blend;
    InkColor color{};
    for (int c = 0; c < 4; ++c)
      color[c] = float(style.colors[stop][c] * (1 - blend) +
                       style.colors[stop + 1][c] * blend);
    if (style.fadeIn > 0) color[3] *= float(std::min(1.0, u / style.fadeIn));
    if (style.fadeOut > 0)
      color[3] *= float(std::min(1.0, (1 - u) / style.fadeOut));
    for (int c = 0; c < 3; ++c) color[c] *= color[3];

    const auto before = i > 0 ? i - 1 : (closed ? points.size() - 2 : i);
    const auto after  = i + 1 < points.size() ? i + 1 : (closed ? 1 : i);
    auto in           = normal(points[before].position, p.position);
    auto out          = normal(p.position, points[after].position);
    if (in == XY{}) in = out;
    if (out == XY{}) out = in;
    const double miter = std::max(0.5, 1 + in[0] * out[0] + in[1] * out[1]);
    XY direction{{(in[0] + out[0]) / miter, (in[1] + out[1]) / miter}};
    if (direction == XY{}) direction = out;
    // Phase depends on the authored spline, so tiles and animation agree.
    const double phase  = double(spline % 127) * 2.399963;
    const double noise  = std::sin(10 * Turn * u + phase);
    const double grain  = std::sin(23 * Turn * u - phase);
    const double radius = width * 0.5 * (1 + 0.25 * style.roughness * grain);
    const double offset = width * 0.3 * style.roughness * noise;
    std::array<Vec3, 2> sides;
    for (int side = 0; side < 2; ++side) {
      const double d = offset + (side == 0 ? radius : -radius);
      auto vertex    = p.position;
      vertex[0] += d * direction[0];
      vertex[1] += d * direction[1];
      // Ink on a patch boundary should win a depth tie with the surface.
      vertex[2] += 1e-8 * std::max(1.0, std::abs(vertex[2]));
      sides[side] = transform(imageToStage, vertex);
    }
    if (i > 0 && (color[3] > 0 || previousColor[3] > 0)) {
      if (output.size() >= MaxStrokeTriangles)
        throw std::runtime_error("A:M ink stroke triangle budget exceeded");
      output.push_back({{{previous[0], previous[1], sides[0]}},
                        {{previousColor, previousColor, color}}});
      output.push_back({{{sides[0], previous[1], sides[1]}},
                        {{color, previousColor, color}}});
    }
    previous      = sides;
    previousColor = color;
  }
}
}  // namespace

std::vector<InkTriangle> strokeSplines(const Mesh &mesh,
                                       const InkBindings &bindings,
                                       const Matrix *view,
                                       const Matrix &imageToStage,
                                       const int *canceled) {
  std::vector<InkTriangle> result;
  if (bindings.empty()) return result;
  const auto project = [&](StrokePoint p) {
    if (view) {
      const double scale = 1000 / -p.position[2];
      p.position = {{p.position[0] * scale, p.position[1] * scale, scale}};
    }
    return p;
  };
  for (std::size_t s = 0; s < mesh.splines.size(); ++s) {
    const auto &spline    = mesh.splines[s];
    const auto count      = spline.curves.size();
    const InkStyle *style = nullptr;
    std::vector<StrokePoint> run;
    bool wholeSpline = spline.closed;
    const auto flush = [&]() {
      if (style) emitStroke(run, *style, s, wholeSpline, imageToStage, result);
      run.clear();
      wholeSpline = false;
    };
    for (std::size_t i = 0; i < count; ++i) {
      if (canceled && *canceled) return {};
      const auto &curve        = spline.curves[i];
      const InkStyle *selected = nullptr;
      for (std::size_t j = bindings.size(); j > 0; --j) {
        const auto &candidate = bindings[j - 1];
        if ((!candidate.unfilledOnly || !curve.patchEdge) &&
            (candidate.group.empty() ||
             std::any_of(curve.groups.begin(), curve.groups.end(),
                         [&](std::size_t group) {
                           return mesh.splineGroups[group] == candidate.group;
                         }))) {
          selected = &candidate;
          break;
        }
      }
      if (selected != style) {
        if (i > 0) flush();
        style = selected;
      }
      if (!style ||
          *std::max_element(style->widths.begin(), style->widths.end()) == 0)
        continue;
      if (run.size() > MaxStrokeTriangles / 2)
        throw std::runtime_error("A:M ink stroke triangle budget exceeded");
      auto bezier = curve.points;
      if (view)
        for (auto &p : bezier) p = transform(*view, p);
      constexpr int steps = 16;
      std::array<double, steps + 4> stops;
      for (int j = 0; j <= steps; ++j) stops[j] = double(j) / steps;
      stops[steps + 1] = std::clamp(style->middle * count - i, 0.0, 1.0);
      stops[steps + 2] = std::clamp(style->fadeIn * count - i, 0.0, 1.0);
      stops[steps + 3] = std::clamp((1 - style->fadeOut) * count - i, 0.0, 1.0);
      std::sort(stops.begin(), stops.end());
      const auto end = std::unique(stops.begin(), stops.end());
      StrokePoint a{bezier.front(), double(i) / count};
      if (run.empty() && (!view || a.position[2] <= -1))
        run.push_back(project(a));
      for (auto t = stops.begin() + 1; t != end; ++t) {
        const StrokePoint b{sampleSpline(bezier, *t), (i + *t) / count};
        const bool inA = !view || a.position[2] <= -1;
        const bool inB = !view || b.position[2] <= -1;
        if (inA != inB) {
          wholeSpline = false;
          const double f =
              (-1 - a.position[2]) / (b.position[2] - a.position[2]);
          StrokePoint cut{
              {{a.position[0] + f * (b.position[0] - a.position[0]),
                a.position[1] + f * (b.position[1] - a.position[1]), -1}},
              a.progress + f * (b.progress - a.progress)};
          run.push_back(project(cut));
          if (inA) flush();
        }
        if (inB) run.push_back(project(b));
        if (!inA && !inB) wholeSpline = false;
        a = b;
      }
    }
    flush();
  }
  return result;
}
}  // namespace otam

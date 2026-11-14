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
constexpr std::size_t MaxStrokeTriangles = 1000000;
constexpr double Turn                    = 6.283185307179586;

constexpr int CurveSteps = 16;
using ArcSamples         = std::array<double, CurveSteps + 1>;

ArcSamples measureCurve(const InkCurve &curve) {
  ArcSamples lengths{};
  auto previous = curve.points.front();
  if (std::all_of(curve.points.begin() + 1, curve.points.end(),
                  [&](const auto &point) { return point == previous; }))
    return lengths;
  for (int i = 1; i <= CurveSteps; ++i) {
    const auto point = sampleSpline(curve.points, double(i) / CurveSteps);
    lengths[i]       = lengths[i - 1] + std::hypot(point[0] - previous[0],
                                                   point[1] - previous[1],
                                                   point[2] - previous[2]);
    previous         = point;
  }
  return lengths;
}

double lengthAt(const ArcSamples &lengths, double t) {
  const double sample = t * CurveSteps;
  const auto i        = std::min(int(sample), CurveSteps - 1);
  return lengths[i] + (lengths[i + 1] - lengths[i]) * (sample - i);
}

double parameterAt(const ArcSamples &lengths, double length) {
  const auto upper = std::upper_bound(lengths.begin(), lengths.end(), length);
  if (upper == lengths.end()) return 1;
  const auto i = upper - lengths.begin() - 1;
  return (i + (length - lengths[i]) / (lengths[i + 1] - lengths[i])) /
         CurveSteps;
}

double cubic(double a, double b, double c, double d, double t) {
  const double u = 1 - t;
  return u * u * (u * a + 3 * t * b) + t * t * (3 * u * c + t * d);
}

double widthAt(const InkProfile &profile, double u) {
  if (profile.width.empty()) return 1;
  const auto found = std::lower_bound(
      profile.width.begin(), profile.width.end(), u,
      [](const InkWidthSpan &span, double x) { return span[3][0] < x; });
  if (found == profile.width.end())
    return std::clamp(profile.width.back()[3][1], 0.0, 2.0);
  auto [a, b, c, d] = *found;
  if (u <= a[0]) return std::clamp(a[1], 0.0, 2.0);
  if (u >= d[0]) return std::clamp(d[1], 0.0, 2.0);
  const double linear = (u - a[0]) / (d[0] - a[0]);
  if (profile.linear)
    return std::clamp(a[1] + (d[1] - a[1]) * linear, 0.0, 2.0);

  // Match the editor's tangent truncation at neighboring knots.
  const auto limitHandle = [](auto &handle, const auto &knot, double limit) {
    const double x = std::clamp(handle[0], std::min(knot[0], limit),
                                std::max(knot[0], limit));
    if (x == limit && handle[0] != x)
      handle[1] = knot[1] +
                  (handle[1] - knot[1]) * (x - knot[0]) / (handle[0] - knot[0]);
    handle[0] = x;
  };
  limitHandle(b, a, d[0]);
  limitHandle(c, d, a[0]);
  double lo = 0, hi = 1;
  for (int i = 0; i < 22; ++i) {
    const double t = (lo + hi) * 0.5;
    if (cubic(a[0], b[0], c[0], d[0], t) < u)
      lo = t;
    else
      hi = t;
  }
  return std::clamp(cubic(a[1], b[1], c[1], d[1], (lo + hi) * 0.5), 0.0, 2.0);
}

InkColor mixColor(const InkColor &a, const InkColor &b, double t) {
  InkColor color{};
  for (int c = 0; c < 4; ++c) color[c] = float(a[c] * (1 - t) + b[c] * t);
  return color;
}

InkColor colorAt(const InkProfile &profile, double u, const InkColor &base) {
  const auto &stops = profile.colors;
  if (stops.empty()) return base;
  const auto next = std::upper_bound(
      stops.begin(), stops.end(), u,
      [](double x, const auto &stop) { return x < stop.first; });
  if (next == stops.begin()) return next->second;
  if (next == stops.end()) return stops.back().second;
  const auto &a = *(next - 1);
  return mixColor(a.second, next->second,
                  (u - a.first) / (next->first - a.first));
}

XY normal(const Vec3 &a, const Vec3 &b) {
  const double dx = b[0] - a[0], dy = b[1] - a[1];
  const double length = std::hypot(dx, dy);
  return length > 1e-12 ? XY{{-dy / length, dx / length}} : XY{};
}

std::pair<double, InkColor> sampleStyle(const InkStyle &style, double u) {
  double width;
  InkColor color;
  if (style.profile) {
    width = style.widths[0] * widthAt(*style.profile, u);
    color = colorAt(*style.profile, u, style.colors[0]);
  } else {
    const double middle = style.middle;
    const int stop      = u < middle ? 0 : 1;
    const double blend  = stop == 0 ? u / middle : (u - middle) / (1 - middle);
    width = style.widths[stop] * (1 - blend) + style.widths[stop + 1] * blend;
    color = mixColor(style.colors[stop], style.colors[stop + 1], blend);
  }
  if (style.fadeIn > 0) color[3] *= float(std::min(1.0, u / style.fadeIn));
  if (style.fadeOut > 0)
    color[3] *= float(std::min(1.0, (1 - u) / style.fadeOut));
  for (int c = 0; c < 3; ++c) color[c] *= color[3];
  return {width, color};
}

void emitPoint(const Vec3 &position, const InkStyle &style,
               const Matrix &imageToStage, std::vector<InkTriangle> &output) {
  const auto [width, color] = sampleStyle(style, 0.5);
  if (width <= 0 || color[3] <= 0) return;
  constexpr int Sides = 24;
  if (output.size() + Sides > MaxStrokeTriangles)
    throw std::runtime_error("Ink stroke triangle budget exceeded");
  const auto center = transform(imageToStage, position);
  auto rim          = [&](int side) {
    const double angle = Turn * side / Sides;
    auto point         = position;
    point[0] += 0.5 * width * std::cos(angle);
    point[1] += 0.5 * width * std::sin(angle);
    return transform(imageToStage, point);
  };
  auto previous = rim(0);
  for (int side = 1; side <= Sides; ++side) {
    const auto next = rim(side % Sides);
    output.push_back({{{center, previous, next}}, {{color, color, color}}});
    previous = next;
  }
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
    const auto [width, color] = sampleStyle(style, u);

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
                                       const int *canceled, bool useArcLength,
                                       std::size_t firstSplineIndex) {
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
    if (!count) continue;
    std::vector<const InkStyle *> styles(count);
    for (std::size_t i = 0; i < count; ++i) {
      if (canceled && *canceled) return {};
      const auto &curve = spline.curves[i];
      for (auto it = bindings.rbegin(); it != bindings.rend(); ++it) {
        if ((!it->unfilledOnly || !curve.patchEdge) &&
            (it->group.empty() ||
             std::any_of(curve.groups.begin(), curve.groups.end(),
                         [&](std::size_t group) {
                           return mesh.splineGroups[group] == it->group;
                         }))) {
          styles[i] = &*it;
          break;
        }
      }
    }

    std::vector<ArcSamples> arc;
    std::vector<double> lengths;
    if (useArcLength ||
        std::any_of(styles.begin(), styles.end(), [](const InkStyle *style) {
          return style && style->profile.has_value();
        })) {
      arc.reserve(count);
      lengths.push_back(0);
      for (const auto &curve : spline.curves) {
        if (canceled && *canceled) return {};
        arc.push_back(measureCurve(curve));
        lengths.push_back(lengths.back() + arc.back().back());
      }
    }
    const auto cumulative = [&](std::size_t i) {
      return arc.empty() ? 0.0
                         : lengths[i % count] + (i / count) * lengths.back();
    };

    // Join an inked section that straddles the authored seam of a closed
    // spline.
    std::size_t start = 0;
    const auto *first = styles.front();
    if (spline.closed && first && first == styles.back() && first->profile &&
        first->profile->perSection) {
      const auto boundary =
          std::find_if(styles.begin(), styles.end(),
                       [&](auto *style) { return style != first; });
      if (boundary != styles.end()) start = boundary - styles.begin();
    }
    const auto limit = start + count;
    std::vector<StrokePoint> run;
    std::vector<double> stops;
    for (std::size_t begin = start; begin < limit;) {
      const auto *style = styles[begin % count];
      auto end          = begin + 1;
      while (end < limit && styles[end % count] == style) ++end;
      if (!style ||
          *std::max_element(style->widths.begin(), style->widths.end()) == 0) {
        begin = end;
        continue;
      }
      const bool advanced   = style->profile.has_value();
      const bool perSection = advanced && style->profile->perSection;
      const double origin   = perSection ? cumulative(begin) : 0;
      const double span =
          (advanced || useArcLength)
              ? (perSection ? cumulative(end) - origin : lengths.back())
              : 0;
      // A vector brush click can have no centerline length and still be ink.
      if (useArcLength && lengths.back() == 0) {
        emitPoint(spline.curves.front().points.front(), *style, imageToStage,
                  result);
        break;
      }
      bool wholeSpline = spline.closed && end - begin == count;
      const auto flush = [&]() {
        emitStroke(run, *style, firstSplineIndex + s, wholeSpline, imageToStage,
                   result);
        run.clear();
        wholeSpline = false;
      };
      for (auto segment = begin; segment < end; ++segment) {
        if (canceled && *canceled) return {};
        if (run.size() > MaxStrokeTriangles / 2)
          throw std::runtime_error("A:M ink stroke triangle budget exceeded");
        const auto i        = segment % count;
        const double offset = cumulative(perSection ? segment : i) - origin;
        const auto progress = [&](double t) {
          if (span <= 0) return (i + t) / count;
          return std::clamp((offset + lengthAt(arc[i], t)) / span, 0.0, 1.0);
        };
        stops.clear();
        for (int j = 0; j <= CurveSteps; ++j)
          stops.push_back(double(j) / CurveSteps);
        const auto addStop = [&](double u) {
          if (span > 0) {
            const double target = u * span - offset;
            if (target > 0 && target < arc[i].back())
              stops.push_back(parameterAt(arc[i], target));
          } else {
            const double t = u * count - i;
            if (t > 0 && t < 1) stops.push_back(t);
          }
        };
        if (advanced) {
          for (const auto &width : style->profile->width) {
            addStop(width[0][0]);
            const double a = width[0][0], b = width[3][0];
            addStop(a + (b - a) / 3);
            addStop(b - (b - a) / 3);
          }
          for (const auto &color : style->profile->colors) addStop(color.first);
        } else {
          addStop(style->middle);
        }
        addStop(style->fadeIn);
        addStop(1 - style->fadeOut);
        std::sort(stops.begin(), stops.end());
        const auto last = std::unique(stops.begin(), stops.end());

        auto bezier = spline.curves[i].points;
        if (view)
          for (auto &p : bezier) p = transform(*view, p);
        StrokePoint a{bezier.front(), progress(0)};
        if (run.empty() && (!view || a.position[2] <= -1))
          run.push_back(project(a));
        for (auto t = stops.begin() + 1; t != last; ++t) {
          const StrokePoint b{sampleSpline(bezier, *t), progress(*t)};
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
      begin = end;
    }
  }
  return result;
}
}  // namespace otam

#include "liverender.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace otglb {
namespace {
void require(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
std::array<double, 6> multiply(const std::array<double, 6> &a,
                               const std::array<double, 6> &b) {
  return {{a[0] * b[0] + a[1] * b[3], a[0] * b[1] + a[1] * b[4],
           a[0] * b[2] + a[1] * b[5] + a[2], a[3] * b[0] + a[4] * b[3],
           a[3] * b[1] + a[4] * b[4], a[3] * b[2] + a[4] * b[5] + a[5]}};
}
void append(LiveFrame &out, const RenderScene &scene,
            const std::array<double, 6> &affine, std::array<float, 4> scale,
            std::size_t &count, unsigned depth, const int *canceled,
            const std::atomic<bool> *obsolete) {
  require(depth < 64, "Live Render group nesting exceeds 64");
  if ((canceled && *canceled) || (obsolete && obsolete->load())) return;
  for (int k = 0; k < 3; ++k)
    scale[k] = 1 - (1 - scale[k]) * (1 - scene.colorScale[k]);
  scale[3] *= scene.colorScale[3];
  out.warnings.insert(out.warnings.end(), scene.warnings.begin(),
                      scene.warnings.end());
  for (const auto &layer : scene.layers)
    if (layer.scene)
      append(out, *layer.scene, multiply(affine, layer.affine), scale, count,
             depth + 1, canceled, obsolete);
  if (scene.triangles.empty()) return;
  require(scene.triangles.size() <= (LiveFrame::MaxVertices - count) / 3,
          "Live Render exceeds the 500000 triangle budget");
  count += scene.triangles.size() * 3;
  LiveBatch batch;
  batch.affine     = affine;
  batch.colorScale = scale;
  require(!scene.wireframe, "Live Render wireframe requires the normal viewer");
  batch.wireframe     = scene.wireframe;
  batch.texture       = scene.texture;
  batch.textureWidth  = scene.textureWidth;
  batch.textureHeight = scene.textureHeight;
  std::vector<EnvironmentLayer> diffuse;
  for (const auto &layer : scene.environments)
    if (layer.map) {
      if (layer.diffuseLighting)
        diffuse.push_back(layer);
      else
        batch.reflections.push_back(layer);
    }
  require(batch.reflections.size() <= LiveFrame::MaxReflections,
          "Live Render supports at most four reflection environments per "
          "source; use Preview");
  double lo = std::numeric_limits<double>::max(), hi = -lo;
  for (const auto &t : scene.triangles)
    for (const auto &v : t.vertices) {
      require(
          std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.depth),
          "Non-finite Live Render vertex");
      lo = std::min(lo, v.depth);
      hi = std::max(hi, v.depth);
    }
  struct Triangle {
    LiveVertex v[3];
    double depth;
    bool opaque;
  };
  std::vector<Triangle> transparent;
  batch.vertices.reserve(scene.triangles.size() * 3);
  for (std::size_t i = 0; i < scene.triangles.size(); ++i) {
    if ((i & 255) == 0 &&
        ((canceled && *canceled) || (obsolete && obsolete->load())))
      return;
    const auto &t = scene.triangles[i];
    Triangle triangle{};
    triangle.opaque = !t.textured;
    require(!t.textured || (batch.texture && batch.textureWidth > 0 &&
                            batch.textureHeight > 0),
            "Live Render image plane has no texture");
    for (int j = 0; j < 3; ++j) {
      const auto &p = t.vertices[j];
      auto &v       = triangle.v[j];
      std::array<double, 3> bary{};
      bary[j] = 1;
      SurfaceSample s;
      const bool surface = scene.surfaceShader && i < scene.surfaceCount;
      if (surface)
        s = scene.surfaceShader(i, bary);
      else {
        ColorPixel c{t.color[0], t.color[1], t.color[2], 1};
        if (scene.sampleShader) c = scene.sampleShader(i, bary);
        s.alpha = c.alpha;
        if (c.alpha > 0)
          s.local = {{srgbToLinear(c.r / c.alpha), srgbToLinear(c.g / c.alpha),
                      srgbToLinear(c.b / c.alpha)}};
      }
      const auto color = shadeEnvironment(s.local, s.normal, s.view, diffuse, 0,
                                          0, 0, s.diffuse, s.diffuseFalloff);
      v.position[0]    = float(p.x);
      v.position[1]    = float(p.y);
      v.position[2] = hi > lo ? float(1 - 2 * (p.depth - lo) / (hi - lo)) : 0;
      v.position[3] =
          surface && scene.surfacePerspective ? float(1 / p.depth) : 1;
      require(std::isfinite(v.position[3]) && v.position[3] > 0,
              "Invalid Live Render perspective depth");
      for (int k = 0; k < 3; ++k) {
        v.normal[k] = float(s.normal[k]);
        v.view[k]   = float(s.view[k]);
        v.color[k]  = color[k];
      }
      v.color[3]      = s.alpha;
      v.reflection[0] = s.distanceFalloff > 0 ? 0 : float(s.reflectivity);
      v.reflection[1] = float(s.blend);
      v.reflection[2] = surface ? 1 : 0;
      v.uv[0]         = t.textured ? float(t.uv[j][0]) : -1;
      v.uv[1]         = t.textured ? float(t.uv[j][1]) : -1;
      triangle.opaque &= s.alpha == 1;
      triangle.depth += p.depth / 3;
      std::array<float, sizeof(LiveVertex) / sizeof(float)> values;
      std::memcpy(values.data(), &v, sizeof(v));
      for (float value : values)
        require(std::isfinite(value), "Non-finite Live Render surface");
    }
    if (triangle.opaque)
      batch.vertices.insert(batch.vertices.end(), triangle.v, triangle.v + 3);
    else
      transparent.push_back(triangle);
  }
  batch.opaqueVertices = batch.vertices.size();
  std::stable_sort(
      transparent.begin(), transparent.end(),
      [](const Triangle &a, const Triangle &b) { return a.depth < b.depth; });
  for (const auto &t : transparent)
    batch.vertices.insert(batch.vertices.end(), t.v, t.v + 3);
  out.batches.push_back(std::move(batch));
}
}  // namespace
LiveFrame prepareLiveFrame(const RenderScene &scene, const int *canceled,
                           const std::atomic<bool> *obsolete) {
  LiveFrame frame;
  std::size_t count = 0;
  append(frame, scene, {{1, 0, 0, 0, 1, 0}}, {{0, 0, 0, 1}}, count, 0, canceled,
         obsolete);
  if ((canceled && *canceled) || (obsolete && obsolete->load())) return {};
  return frame;
}
}  // namespace otglb

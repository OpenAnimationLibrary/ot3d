#include "environment.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace otglb {
namespace {
constexpr double Pi = 3.14159265358979323846;
}
EnvironmentMap::EnvironmentMap(int width, int height,
                               std::vector<EnvironmentColor> pixels)
    : m_width(width), m_height(height), m_pixels(std::move(pixels)) {
  if (width <= 0 || height <= 0 || width > 8192 || height > 8192 ||
      std::size_t(width) * height > MaxPixels ||
      m_pixels.size() != std::size_t(width) * height)
    throw std::runtime_error(
        "Environment requires a bounded image of at most 4 megapixels");
  for (const auto &p : m_pixels)
    for (float v : p)
      if (!std::isfinite(v) || v < 0 || v > 1e12f)
        throw std::runtime_error(
            "Environment radiance must be finite and nonnegative (at most "
            "1e12)");
}
EnvironmentColor EnvironmentMap::sample(const EnvironmentDirection &d) const {
  const double length = std::hypot(d[0], d[1], d[2]);
  if (!(length > 0) || !std::isfinite(length)) return {};
  const double u = .5 + std::atan2(d[0], d[2]) / (2 * Pi);
  const double v = .5 + std::asin(std::clamp(d[1] / length, -1.0, 1.0)) / Pi;
  const double x = u * m_width - .5, y = v * m_height - .5;
  const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
  const double tx = x - x0, ty = y - y0;
  EnvironmentColor result{};
  for (int j = 0; j < 2; ++j)
    for (int i = 0; i < 2; ++i) {
      const int sx        = ((x0 + i) % m_width + m_width) % m_width;
      const int sy        = std::clamp(y0 + j, 0, m_height - 1);
      const double weight = (i ? tx : 1 - tx) * (j ? ty : 1 - ty);
      for (int k = 0; k < 3; ++k)
        result[k] +=
            float(weight * m_pixels[std::size_t(sy) * m_width + sx][k]);
    }
  return result;
}
void EnvironmentLayer::setRotation(const EnvironmentDirection &degrees) {
  for (double v : degrees)
    if (!std::isfinite(v))
      throw std::runtime_error("Non-finite environment rotation");
  const double x = degrees[0] * Pi / 180, y = degrees[1] * Pi / 180,
               z  = degrees[2] * Pi / 180;
  const double sx = std::sin(x), cx = std::cos(x), sy = std::sin(y),
               cy = std::cos(y), sz = std::sin(z), cz = std::cos(z);
  // Transpose of Rz * Ry * Rx.
  worldToMap = {{cz * cy, sz * cy, -sy, cz * sy * sx - sz * cx,
                 sz * sy * sx + cz * cx, cy * sx, cz * sy * cx + sz * sx,
                 sz * sy * cx - cz * sx, cy * cx}};
}
EnvironmentDirection EnvironmentLayer::direction(
    const EnvironmentDirection &w) const {
  EnvironmentDirection d{};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) d[i] += worldToMap[i * 3 + j] * w[j];
  return d;
}
bool EnvironmentLayer::operator==(const EnvironmentLayer &o) const {
  return map == o.map && worldToMap == o.worldToMap && gain == o.gain &&
         overrideMaterial == o.overrideMaterial &&
         reflectivity == o.reflectivity && blend == o.blend;
}
EnvironmentColor shadeEnvironment(EnvironmentColor local,
                                  const EnvironmentDirection &n,
                                  const EnvironmentDirection &v,
                                  const std::vector<EnvironmentLayer> &layers,
                                  double reflectivity, double blend,
                                  double distanceFalloff) {
  const double nv = n[0] * v[0] + n[1] * v[1] + n[2] * v[2];
  const EnvironmentDirection reflected{
      {2 * nv * n[0] - v[0], 2 * nv * n[1] - v[1], 2 * nv * n[2] - v[2]}};
  for (const auto &layer : layers) {
    if (!layer.map) continue;
    const double r = layer.overrideMaterial
                         ? layer.reflectivity
                         : (distanceFalloff > 0 ? 0 : reflectivity);
    const double b = layer.overrideMaterial ? layer.blend : blend;
    if (!(r > 0)) continue;
    const auto radiance = layer.map->sample(layer.direction(reflected));
    for (int k = 0; k < 3; ++k)
      local[k] =
          float(local[k] * (1 - r * b) + r * layer.gain[k] * radiance[k]);
  }
  return local;
}
}  // namespace otglb

#include "environment.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace otglb {
namespace {
constexpr double Pi = 3.14159265358979323846;
std::array<double, 9> harmonics(const EnvironmentDirection &n) {
  const double x = n[0], y = n[1], z = n[2];
  return {{.28209479177387814, .4886025119029199 * y, .4886025119029199 * z,
           .4886025119029199 * x, 1.0925484305920792 * x * y,
           1.0925484305920792 * y * z, .31539156525252005 * (3 * z * z - 1),
           1.0925484305920792 * x * z, .5462742152960396 * (x * x - y * y)}};
}
struct IrradianceCanceled {};
}  // namespace
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
// Degree-two SH convolution of distant diffuse illumination. See Ramamoorthi
// and Hanrahan, SIGGRAPH 2001, https://graphics.stanford.edu/papers/envmap/.
// Integrate solid angle, not pixel count (polar texels cover less of the
// sphere).
bool EnvironmentMap::prepareIrradiance(const int *canceled) const {
  try {
    std::call_once(m_irradianceOnce, [&] {
      std::array<double, 3> mean{};
      std::array<std::array<double, 3>, 9> sh{};
      for (int y = 0; y < m_height; ++y) {
        if (canceled && *canceled) throw IrradianceCanceled{};
        const double low    = Pi * (double(y) / m_height - .5);
        const double high   = Pi * (double(y + 1) / m_height - .5);
        const double weight = (std::sin(high) - std::sin(low)) / (2 * m_width);
        for (int x = 0; x < m_width; ++x)
          for (int k = 0; k < 3; ++k)
            mean[k] += weight * m_pixels[std::size_t(y) * m_width + x][k];
      }
      std::vector<std::array<double, 2>> longitude(m_width);
      for (int x = 0; x < m_width; ++x) {
        const double phi = 2 * Pi * ((x + .5) / m_width - .5);
        longitude[x]     = {{std::sin(phi), std::cos(phi)}};
      }
      for (int y = 0; y < m_height; ++y) {
        if (canceled && *canceled) throw IrradianceCanceled{};
        const double theta = Pi * ((y + .5) / m_height - .5);
        const double low   = Pi * (double(y) / m_height - .5);
        const double high  = Pi * (double(y + 1) / m_height - .5);
        const double weight =
            2 * Pi / m_width * (std::sin(high) - std::sin(low));
        for (int x = 0; x < m_width; ++x) {
          const auto basis =
              harmonics({{longitude[x][0] * std::cos(theta), std::sin(theta),
                          longitude[x][1] * std::cos(theta)}});
          // Integrating only the residual makes constant maps exact at every
          // resolution, including 1x1. l=0 is supplied separately by the mean.
          for (int k = 0; k < 3; ++k) {
            const double residual =
                weight * (m_pixels[std::size_t(y) * m_width + x][k] - mean[k]);
            for (int i = 1; i < 9; ++i) sh[i][k] += residual * basis[i];
          }
        }
      }
      m_mean = mean;
      m_sh   = sh;
    });
    return !(canceled && *canceled);
  } catch (const IrradianceCanceled &) {
    return false;
  }
}
EnvironmentColor EnvironmentMap::irradiance(const EnvironmentDirection &n,
                                            double falloff) const {
  const double length = std::hypot(n[0], n[1], n[2]);
  if (!(length > 0) || !std::isfinite(length)) return {};
  prepareIrradiance();
  const auto basis = harmonics({{n[0] / length, n[1] / length, n[2] / length}});
  const double f   = std::clamp(falloff, 0.0, 1.0);
  // 2 * integral[0,1] min(1,t/f) P_l(t) dt, normalized by pi.
  const double kernel[] = {2 - f, 1 - f * f / 3, f / 2 - f * f * f / 4};
  EnvironmentColor result;
  for (int k = 0; k < 3; ++k) {
    double value = m_mean[k] * kernel[0];
    for (int i = 1; i < 9; ++i)
      value += m_sh[i][k] * basis[i] * kernel[i < 4 ? 1 : 2];
    result[k] = float(std::max(0.0, value));
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
         diffuseLighting == o.diffuseLighting &&
         reflectivity == o.reflectivity && blend == o.blend;
}
EnvironmentColor shadeEnvironment(
    EnvironmentColor local, const EnvironmentDirection &n,
    const EnvironmentDirection &v, const std::vector<EnvironmentLayer> &layers,
    double reflectivity, double blend, double distanceFalloff,
    EnvironmentColor diffuse, double diffuseFalloff) {
  // Illuminate the local surface before mixing its reflected fraction,
  // regardless of the relative order of IBL and reflection nodes.
  for (const auto &layer : layers) {
    if (!layer.map || !layer.diffuseLighting) continue;
    const auto energy =
        layer.map->irradiance(layer.direction(n), diffuseFalloff);
    for (int k = 0; k < 3; ++k)
      local[k] += diffuse[k] * layer.gain[k] * energy[k];
  }
  const double nv = n[0] * v[0] + n[1] * v[1] + n[2] * v[2];
  const EnvironmentDirection reflected{
      {2 * nv * n[0] - v[0], 2 * nv * n[1] - v[1], 2 * nv * n[2] - v[2]}};
  for (const auto &layer : layers) {
    if (!layer.map || layer.diffuseLighting) continue;
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

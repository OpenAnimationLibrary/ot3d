#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <vector>

namespace otglb {
using EnvironmentColor     = std::array<float, 3>;
using EnvironmentDirection = std::array<double, 3>;

// Linear radiance, bottom-up equirectangular image. +Y is north; +Z is the
// image center, +X is three quarters across. Alpha contributes against black.
class EnvironmentMap {
  int m_width, m_height;
  std::vector<EnvironmentColor> m_pixels;
  mutable std::once_flag m_irradianceOnce;
  mutable std::array<double, 3> m_mean{};
  mutable std::array<std::array<double, 3>, 9> m_sh{};

public:
  static constexpr std::size_t MaxPixels = 4 * 1024 * 1024;
  EnvironmentMap(int width, int height, std::vector<EnvironmentColor> pixels);
  EnvironmentColor sample(const EnvironmentDirection &direction) const;
  bool prepareIrradiance(const int *canceled = nullptr) const;
  // E/pi: Lambert irradiance, or A:M's min(1,N.L/falloff) kernel.
  EnvironmentColor irradiance(const EnvironmentDirection &normal,
                              double diffuseFalloff = 1) const;
};

struct EnvironmentLayer {
  std::shared_ptr<const EnvironmentMap> map;
  // Inverse of the authored X/Y/Z rotation; maps world directions to the image.
  std::array<double, 9> worldToMap{{1, 0, 0, 0, 1, 0, 0, 0, 1}};
  EnvironmentColor gain{{1, 1, 1}};
  bool overrideMaterial = false;
  bool diffuseLighting  = false;
  double reflectivity = .5, blend = 1;
  void setRotation(const EnvironmentDirection &degrees);
  EnvironmentDirection direction(const EnvironmentDirection &world) const;
  bool operator==(const EnvironmentLayer &other) const;
};

// Material blend 0 adds reflected light, 1 replaces the reflected fraction of
// the local surface. A finite A:M distance falloff cannot reach an infinite
// map.
EnvironmentColor shadeEnvironment(EnvironmentColor local,
                                  const EnvironmentDirection &normal,
                                  const EnvironmentDirection &view,
                                  const std::vector<EnvironmentLayer> &layers,
                                  double reflectivity = 0, double blend = 0,
                                  double distanceFalloff   = 0,
                                  EnvironmentColor diffuse = {{1, 1, 1}},
                                  double diffuseFalloff    = 1);

}  // namespace otglb

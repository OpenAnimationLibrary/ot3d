#pragma once

#include <array>
#include <memory>
#include <vector>

namespace otglb {
using EnvironmentColor     = std::array<float, 3>;
using EnvironmentDirection = std::array<double, 3>;

// Linear radiance, bottom-up equirectangular image. +Y is north; +Z is the
// image center, +X is three quarters across. Alpha contributes against black.
class EnvironmentMap {
  int m_width, m_height;
  std::vector<EnvironmentColor> m_pixels;

public:
  static constexpr std::size_t MaxPixels = 4 * 1024 * 1024;
  EnvironmentMap(int width, int height, std::vector<EnvironmentColor> pixels);
  EnvironmentColor sample(const EnvironmentDirection &direction) const;
};

struct EnvironmentLayer {
  std::shared_ptr<const EnvironmentMap> map;
  // Inverse of the authored X/Y/Z rotation; maps world directions to the image.
  std::array<double, 9> worldToMap{{1, 0, 0, 0, 1, 0, 0, 0, 1}};
  EnvironmentColor gain{{1, 1, 1}};
  bool overrideMaterial = false;
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
                                  double distanceFalloff = 0);

}  // namespace otglb

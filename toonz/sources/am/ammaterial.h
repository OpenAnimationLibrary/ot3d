#pragma once

#include "ammodel.h"
#include <optional>

namespace otam {
// A:M percentages are normalized once at the file boundary. Unset colors
// inherit the evaluated diffuse/palette color, as in HAttrProperty ForRead.
struct Surface {
  std::optional<Color> diffuse, ambianceColor, specularColor;
  double diffuseFalloff = 1, ambiance = 0, ambianceBlend = 0;
  double specularSize = 0, specularIntensity = 1;
  double roughness = 0, roughnessScale = 1, transparency = 0;
  double density = 0, refraction = 1, translucency = 0;
  double reflectivity = 0, reflectivityFalloff = 0, reflectiveBlend = 0;
  double radiance = 1;
  bool glow       = false;
};
struct Material {
  std::string source, name, version;
  Surface surface;
  std::vector<std::string> unsupported;
  std::vector<std::string>
      deferred;  // Retained scene reflections; no ray scene yet.
};
struct MaterialBinding {
  Material material;
  std::string group;  // Empty selects the whole model; otherwise exact name.
  bool paletteColor       = false;
  bool specularOnly = false;  // Fileless controls preserve an upstream surface.
  bool previewUnsupported = false;
};
using MaterialBindings = std::vector<MaterialBinding>;

// Modern tagged MATERIALFILE, one constant ATTRIBUTE. Complex material trees
// are rejected, never flattened by accidentally taking their first attribute.
Material parseMaterial(std::string source, const Limits &limits = Limits());

struct SurfaceLight {
  Vec3 direction{{0, 0, 1}};  // Unit vector from the sample toward the light.
  Color color{{1, 1, 1}};     // Linear RGB.
  double intensity = 1;
};
// Local illumination only. Specular width is an explicit preview model, not
// a claim of the proprietary Hash highlight distribution. Returns linear RGB.
Color shadeSurface(const Surface &surface, const Color &diffuse,
                   const Vec3 &normal, const Vec3 &view,
                   const std::vector<SurfaceLight> &lights, double ambient,
                   double master);
}  // namespace otam

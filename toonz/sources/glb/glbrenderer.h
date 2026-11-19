#pragma once

#include "glbloader.h"
#include "environment.h"
#include <algorithm>
#include <type_traits>
#include <memory>
#include <functional>
#include <optional>

namespace otglb {

// A fixed image plane keeps projection independent of preview zoom, output
// resolution and tile size. The FX applies the normal OpenToonz 2D affine.
constexpr double ProjectionHeight = 1000.0;

struct PointPlacement {
  std::array<double, 3> position{};
  double radius = 1.0;

  bool operator==(const PointPlacement &other) const {
    return position == other.position && radius == other.radius;
  }
};

struct SpotCone {
  // Axis points out of the light; angles are half angles in degrees.
  std::array<double, 3> axis{{0, 0, -1}};
  double inner = 20.0, outer = 30.0;
  bool operator==(const SpotCone &b) const {
    return axis == b.axis && inner == b.inner && outer == b.outer;
  }
};

struct DiffuseLight {
  // Direction in the source's lighting space. Point positions are world-space.
  std::array<double, 3> direction{{0.0, 0.0, 1.0}};
  // Display-referred sRGB light color in [0,1].
  std::array<float, 3> color{{1.0f, 1.0f, 1.0f}};
  double intensity = 1.0;  // Negative values subtract incident light.
  std::optional<PointPlacement> point;
  std::optional<SpotCone> spot;
  // Existing rigs retain camera-relative directional lighting in GLB FX.
  bool worldDirection = false;

  bool operator==(const DiffuseLight &other) const {
    return direction == other.direction && color == other.color &&
           intensity == other.intensity && point == other.point &&
           spot == other.spot && worldDirection == other.worldDirection;
  }

  void validate() const;
  double diffuse(const std::array<double, 3> &normal,
                 const std::array<double, 3> &sample) const;
};

struct LightingRig {
  // Ambient and master are linear-light multipliers. Directional colors are
  // converted from sRGB before contributing to diffuse illumination.
  double ambient = 0.0;
  double master = 1.0;
  std::vector<DiffuseLight> lights;
  std::vector<EnvironmentLayer> environments;
  // Environment reflections alone preserve the source's default lighting.
  bool preserveDefaultLighting = false;
  // Include the source's default shading in the explicit light sum.
  bool includeDefaultLighting = false;

  bool operator==(const LightingRig &other) const {
    return ambient == other.ambient && master == other.master &&
           lights == other.lights && environments == other.environments &&
           preserveDefaultLighting == other.preserveDefaultLighting &&
           includeDefaultLighting == other.includeDefaultLighting;
  }
};

// A downstream 3D FX can add one or more transforms without flattening the
// source to a raster. Transforms are evaluated in vector order after the GLB
// Model node's own instance transform and before the camera transform.
struct ModelTransform {
  std::array<double, 3> position{};
  std::array<double, 3> rotation{};  // Degrees, applied X then Y then Z.
  std::array<double, 3> scale{{1.0, 1.0, 1.0}};
  // Camera-local pose delta, composed after the source camera pose. Geometry
  // transforms remain in source order; camera deltas are inverted for viewing.
  bool camera = false;

  bool operator==(const ModelTransform &other) const {
    return position == other.position && rotation == other.rotation &&
           scale == other.scale && camera == other.camera;
  }
};

struct RenderOptions {
  std::array<double, 3> position{}, rotation{};  // Degrees, applied X then Y then Z.
  double scale = 1.0;
  double cameraDistance = 10.0, fieldOfView = 45.0, orthoHeight = 10.0;
  double nearClip = 0.1, farClip = 1000.0;
  bool perspective = false, headlight = false, wireframe = false;
  bool materialColors = false;  // False preserves the original grayscale output.
  // Display-referred RGB, one per asset material plus the default material.
  // Empty uses the asset's linear baseColor factors, converted to sRGB.
  std::vector<std::array<float, 3>> colors;

  // Optional diffuse lighting supplied by a downstream 3D
  // FX. The rig can explicitly include the source's legacy shading.
  bool useLightingRig = false;
  LightingRig lighting;

  // Additional model-space transforms supplied by downstream 3D nodes.
  std::vector<ModelTransform> transforms;

  // NoIndex preserves the historical static/base-geometry path exactly.
  // A valid index evaluates that embedded glTF clip at sourceSeconds and
  // applies node animation plus skinning before projection. Time is always glTF
  // seconds.
  int animation = NoIndex;
  double sourceSeconds = 0.0;

  bool operator==(const RenderOptions &other) const;
};

struct ProjectedVertex {
  double x, y, depth;  // Larger depth is nearer (1/d for perspective, -d otherwise).
};
struct RenderTriangle {
  std::array<ProjectedVertex, 3> vertices;
  std::array<bool, 3> edges;  // Excludes triangulation diagonals after clipping.
  std::array<float, 3> color;
  std::array<std::array<double, 2>, 3> uv{};
  bool textured = false;
  bool toonSurface   = false;
  float illumination = -1;  // Linear diffuse energy; -1 means no lighting FX.
};
struct ColorPixel {
  float r = 0, g = 0, b = 0, alpha = 0;  // Premultiplied sRGB.
};
// Linear-light surface evaluation shared by the software and live GPU paths.
// Environment reflections remain unevaluated so the GPU can sample per pixel.
struct SurfaceSample {
  EnvironmentColor local{}, diffuse{{1, 1, 1}};
  EnvironmentDirection normal{{0, 0, 1}}, view{{0, 0, 1}};
  double reflectivity = 0, blend = 0, distanceFalloff = 0, diffuseFalloff = 1;
  float alpha = 1;
  float illumination = -1;
};
ColorPixel shadeSurfaceSample(const SurfaceSample &sample,
                              const std::vector<EnvironmentLayer> &layers);

inline float lightLuminance(double r, double g, double b) {
  return float(0.2126 * r + 0.7152 * g + 0.0722 * b);
}

struct ToonLineStyle {
  double width             = 1.5;  // Full stroke width in image plane units.
  double lightingInfluence = 0.75;
  double bottomWeight      = 0;  // Extra width on downward-facing contours.
  std::array<float, 4> color{{0, 0, 0, 1}};

  double maximumRadius() const {
    return 0.5 * width * (1 + lightingInfluence + bottomWeight);
  }
};

struct RenderScene;
struct RenderLayer {
  std::shared_ptr<const RenderScene> scene;
  std::array<double, 6> affine{{1, 0, 0, 0, 1, 0}};
};
struct RenderScene {
  // Screen-style RGB tint and opacity, applied once after coverage sampling.
  // Default is identity. Native model columns use this instead of a raster
  // wrapper which would prevent downstream geometry transforms/lighting.
  std::array<float, 4> colorScale{{0, 0, 0, 1}};
  std::vector<RenderTriangle> triangles;
  // Environment radiance is integrated/composited in linear light.
  bool linearSampling = false;
  // Optional immutable material evaluator. Barycentrics are screen-space;
  // the provider performs perspective-correct interpolation in its own space.
  // Results are premultiplied sRGB. Transparent hits composite in linear light.
  std::function<ColorPixel(std::size_t, const std::array<double, 3> &)>
      sampleShader;
  using SurfaceShader =
      std::function<SurfaceSample(std::size_t, const std::array<double, 3> &)>;
  SurfaceShader surfaceShader;
  // Bind both evaluators to a single immutable payload so copying a render
  // scene for visible lights does not duplicate the per-triangle shader data.
  void setSurfaceShader(SurfaceShader shader);
  std::size_t surfaceCount = 0;
  bool surfacePerspective  = false;
  std::vector<EnvironmentLayer> environments;
  std::array<double, 4> bounds{};  // x0, y0, x1, y1 in the fixed image plane.
  std::vector<std::string> warnings;
  std::vector<std::string>
      materialGroups;  // Native patch assignment discovery.
  std::vector<std::string> splineGroups;  // Native spline assignment discovery.
  // Geometry metadata used by target-aware FX without raster evaluation.
  std::optional<std::array<double, 3>> worldCenter;
  using WorldTriangle = std::array<std::array<double, 3>, 3>;
  std::function<std::vector<RenderTriangle>(const WorldTriangle &)>
      projectTriangle;
  // Apply a stage-image affine at the point's original camera depth.
  std::function<std::array<double, 3>(const std::array<double, 3> &,
                                      const std::array<double, 6> &)>
      placeWorldPoint;
  bool wireframe = false;
  bool texturePerspective = false;
  int textureWidth = 0, textureHeight = 0;
  std::shared_ptr<const std::vector<ColorPixel>> texture;
  // Back-to-front source layers. Each retains its own camera/depth, texture,
  // wireframe and color filter. Groups contain layers instead of triangles.
  std::vector<RenderLayer> layers;
  std::optional<ToonLineStyle> toonLine;
  bool empty() const { return triangles.empty() && layers.empty(); }
};
struct RenderTile {
  int width = 0, height = 0;
  double x = 0, y = 0;
  // Row-major 2D affine: a11,a12,a13,a21,a22,a23.
  std::array<double, 6> affine{{1, 0, 0, 0, 1, 0}};
};

// Opaque, two-sided geometry. With options.animation == NoIndex this preserves
// the original static base-geometry path. Otherwise embedded node animation and
// skeletal skinning are evaluated. Morph deformation remains deferred.
// Uses the declared default scene, otherwise the first scene.
// Throws std::runtime_error on invalid settings, animation data or resource
// limits.
RenderScene prepareRender(const Asset &asset, const RenderOptions &options,
                          const int *canceled = nullptr);

// Four coverage/depth samples per pixel; coordinates and coverage do not
// depend on tile boundaries. No graphics context or global mutable state.
std::vector<ColorPixel> renderTile(const RenderScene &scene, const RenderTile &tile,
                                   const int *canceled = nullptr);

// Center a raster on a camera-facing plane. One input pixel equals one image
// plane unit at the default orthographic camera; alpha remains in the texture.
RenderScene prepareImagePlane(
    int width, int height,
    std::shared_ptr<const std::vector<ColorPixel>> pixels,
    const std::vector<ModelTransform> &transforms);
// Geometry-only query for FX bounds; does not allocate or evaluate pixels.
RenderScene projectImagePlane(int width, int height,
                              const std::vector<ModelTransform> &transforms);

// Spatial drawing plane in stage coordinates, clipped against the optical
// near plane. Matrices are row-major doubles, as in native model placement.
RenderScene projectImagePlane3D(const std::array<double, 4> &rectangle,
                                const std::array<double, 16> &localToCamera,
                                const std::array<double, 16> &imageToStage);

// Append an emissive sphere in the source camera with ordinary depth testing.
// Existing material shaders and source geometry remain immutable.
void appendVisibleLight(RenderScene &scene,
                        const std::array<double, 3> &position, double radius,
                        const std::array<float, 3> &radiance,
                        const int *canceled = nullptr);
std::optional<std::array<double, 3>> targetCenter(
    const RenderScene &scene,
    const std::array<double, 6> &affine = {{1, 0, 0, 0, 1, 0}});
// Extended transfer retains HDR for floating output; integer outputs clamp
// here.
float linearToSrgbExtended(float value);
template <class Channel>
Channel displayChannel(float value, double maximum) {
  if constexpr (std::is_floating_point<Channel>::value)
    return Channel(std::max(0.0f, value));
  else
    return Channel(std::clamp(value, 0.0f, 1.0f) * maximum + .5);
}
float linearToSrgb(float value);
float srgbToLinear(float value);
inline ColorPixel linearPixel(const ColorPixel &p) {
  if (!(p.alpha > 0)) return {};
  return {srgbToLinear(p.r / p.alpha) * p.alpha,
          srgbToLinear(p.g / p.alpha) * p.alpha,
          srgbToLinear(p.b / p.alpha) * p.alpha, p.alpha};
}
inline ColorPixel displayPixel(const ColorPixel &p) {
  if (!(p.alpha > 0)) return {};
  return {linearToSrgbExtended(p.r / p.alpha) * p.alpha,
          linearToSrgbExtended(p.g / p.alpha) * p.alpha,
          linearToSrgbExtended(p.b / p.alpha) * p.alpha, p.alpha};
}
inline ColorPixel outputPixel(const ColorPixel &p, bool linear) {
  return linear ? linearPixel(p) : p;
}
std::array<float, 3> materialColor(const Asset &asset, std::size_t index);

}  // namespace otglb

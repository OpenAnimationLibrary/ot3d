#include "glbrenderer.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
void near(float a, float b, float e = 1e-5f) {
  require(std::abs(a - b) < e, "environment numeric mismatch");
}
otglb::Asset triangle() {
  otglb::Asset a;
  a.materials.resize(1);
  a.materials[0].baseColor = {{.25f, .25f, .25f, 1}};
  otglb::Attribute positions;
  positions.semantic   = "POSITION";
  positions.components = 3;
  positions.values     = {-3, -3, 0, 3, -3, 0, 0, 3, 0};
  otglb::Primitive p;
  p.mode          = 4;
  p.material      = 0;
  p.vertexCount   = 3;
  p.triangleCount = 1;
  p.attributes.push_back(positions);
  otglb::Mesh m;
  m.primitives.push_back(p);
  a.meshes.push_back(m);
  otglb::Node n;
  n.mesh = 0;
  n.world.fill(0);
  n.world[0] = n.world[5] = n.world[10] = n.world[15] = 1;
  n.local                                             = n.world;
  a.nodes.push_back(n);
  otglb::Scene s;
  s.roots.push_back(0);
  a.scenes.push_back(s);
  a.defaultScene = 0;
  return a;
}
}  // namespace
int main() {
  try {
    using namespace otglb;
    const auto uniform = std::make_shared<EnvironmentMap>(
        1, 1, std::vector<EnvironmentColor>{{{4, .5f, .25f}}});
    near(uniform->sample({{0, 0, 1}})[0], 4);
    near(uniform->sample({{0, 1, 0}})[1], .5f);
    near(uniform->sample({{0, -1, 0}})[2], .25f);
    require(uniform->sample({{0, 0, 0}}) == EnvironmentColor{},
            "zero direction");
    std::vector<EnvironmentColor> pixels(8);
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 4; ++x) pixels[y * 4 + x] = {{float(x), float(y), 0}};
    EnvironmentMap panorama(4, 2, pixels);
    near(panorama.sample({{0, 0, 1}})[0], 1.5f);
    near(panorama.sample({{1, 0, 0}})[0], 2.5f);
    near(panorama.sample({{0, 1, 0}})[1], 1);
    near(panorama.sample({{0, -1, 0}})[1], 0);
    const auto seamA = panorama.sample({{1e-10, 0, -1}});
    const auto seamB = panorama.sample({{-1e-10, 0, -1}});
    near(seamA[0], seamB[0]);
    bool rejected = false;
    try {
      EnvironmentMap bad(1, 1,
                         {{{std::numeric_limits<float>::infinity(), 0, 0}}});
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    require(rejected, "accepted nonfinite radiance");
    EnvironmentLayer layer;
    layer.map = uniform;
    const EnvironmentDirection n{{0, 0, 1}}, v{{0, 0, 1}};
    const EnvironmentColor base{{.25f, .25f, .25f}};
    auto shaded = shadeEnvironment(base, n, v, {layer}, .5, 1);
    near(shaded[0], 2.125f);
    near(shaded[1], .375f);
    near(shadeEnvironment(base, n, v, {layer}, .5, 0)[0], 2.25f);
    near(shadeEnvironment(base, n, v, {layer}, .5, 1, 10)[0], .25f);
    layer.overrideMaterial = true;
    near(shadeEnvironment(base, n, v, {layer}, 0, 0, 10)[0], 2.125f);
    layer.setRotation({{0, 90, 0}});
    auto direction = layer.direction({{1, 0, 0}});
    near(float(direction[0]), 0);
    near(float(direction[2]), 1);
    layer.setRotation({{0, 0, 0}});
    RenderOptions options;
    options.materialColors                   = true;
    options.useLightingRig                   = true;
    options.lighting.preserveDefaultLighting = true;
    options.lighting.environments.push_back(layer);
    RenderTile tile;
    tile.width = tile.height = 4;
    tile.x = tile.y = -2;
    auto scene      = prepareRender(triangle(), options);
    require(bool(scene.sampleShader),
            "GLB environment did not reach the shader");
    auto image = renderTile(scene, tile);
    near(srgbToLinear(image[5].r), 2.125f);
    require(displayChannel<unsigned char>(image[5].r, 255) == 255,
            "HDR integer output wrapped");
    require(displayChannel<float>(image[5].r, 1) > 1,
            "HDR float output clipped");
    options.perspective = true;
    auto perspective    = prepareRender(triangle(), options);
    near(srgbToLinear(renderTile(perspective, tile)[5].r), 2.125f);
    options.nearClip = 9.5;
    options.transforms.push_back(ModelTransform{{0, 0, 0}, {0, 20, 0}});
    auto clipped = prepareRender(triangle(), options);
    require(clipped.triangles.size() == 2, "clipped environment fixture");
    require(renderTile(clipped, tile).size() == 16, "clipped sampling");
    int canceled = 1;
    require(renderTile(scene, tile, &canceled).empty(),
            "environment cancellation");
    // Transparent HDR composites in linear light, including over an opaque hit.
    RenderScene transparent  = scene;
    transparent.sampleShader = [](std::size_t, const std::array<double, 3> &) {
      return ColorPixel{linearToSrgbExtended(4) * .5f, 0, 0, .5f};
    };
    auto mixed = renderTile(transparent, tile)[5];
    near(mixed.alpha, .5f);
    near(srgbToLinear(mixed.r / mixed.alpha), 4);
    // Diffuse convolution has a calibrated constant response: a white
    // Lambertian surface under radiance L returns L, including HDR and 1x1
    // maps.
    auto irradiance = uniform->irradiance(n);
    near(irradiance[0], 4);
    near(irradiance[1], .5f);
    near(uniform->irradiance({{0, 1, 0}})[0], 4);
    near(uniform->irradiance(n, .5)[0], 6);
    near(uniform->irradiance(n, 0)[0], 8);
    EnvironmentLayer ibl;
    ibl.map             = uniform;
    ibl.diffuseLighting = true;
    auto diffuseLit = shadeEnvironment({{0, 0, 0}}, n, v, {ibl}, 0, 0, 0, base);
    near(diffuseLit[0], 1);
    near(diffuseLit[1], .125f);
    // Diffuse illumination is applied before reflection in either node order.
    auto a = shadeEnvironment(base, n, v, {ibl, layer}, 0, 0, 0, base);
    auto b = shadeEnvironment(base, n, v, {layer, ibl}, 0, 0, 0, base);
    near(a[0], b[0]);
    near(a[0], 2.625f);
    constexpr double pi = 3.14159265358979323846;
    const int w = 128, h = 64;
    std::vector<EnvironmentColor> gradient(w * h);
    for (int y = 0; y < h; ++y) {
      const double theta = pi * ((y + .5) / h - .5);
      for (int x = 0; x < w; ++x)
        gradient[y * w + x] = {{float(1 + std::sin(theta)), 0, 0}};
    }
    EnvironmentMap sky(w, h, std::move(gradient));
    near(sky.irradiance({{0, 1, 0}})[0], 1 + 2.f / 3, .001f);
    near(sky.irradiance({{0, -1, 0}})[0], 1 - 2.f / 3, .001f);
    near(sky.irradiance({{1, 0, 0}})[0], 1, .001f);
    // Rotation changes incoming light, not normals or projection geometry.
    EnvironmentLayer rotatedIbl;
    rotatedIbl.diffuseLighting = true;
    rotatedIbl.map             = std::make_shared<EnvironmentMap>(
        w, h, std::vector<EnvironmentColor>(std::size_t(w) * h, {{1, 1, 1}}));
    rotatedIbl.setRotation({{0, 0, 90}});
    const auto rotatedNormal = rotatedIbl.direction({{1, 0, 0}});
    near(float(rotatedNormal[1]), -1);
    EnvironmentMap retry(1, 1, {{{1, 1, 1}}});
    int cancelIbl = 1;
    require(!retry.prepareIrradiance(&cancelIbl),
            "irradiance cancellation ignored");
    cancelIbl = 0;
    require(retry.prepareIrradiance(&cancelIbl),
            "cancelled irradiance poisoned cache");
    near(retry.irradiance(n)[0], 1);
    // The GLB source uses unlit albedo, preserving explicit lights separately.
    options                       = RenderOptions();
    options.materialColors        = true;
    options.useLightingRig        = true;
    options.lighting.environments = {ibl};
    auto iblScene                 = prepareRender(triangle(), options);
    auto iblPixel                 = renderTile(iblScene, tile)[5];
    near(srgbToLinear(iblPixel.r), 1);
    near(srgbToLinear(iblPixel.g), .125f);
    options.lighting.ambient = 1;
    auto combinedScene       = prepareRender(triangle(), options);
    near(srgbToLinear(renderTile(combinedScene, tile)[5].r), 1.25f);
    options.lighting.master = 0;
    near(
        srgbToLinear(renderTile(prepareRender(triangle(), options), tile)[5].r),
        1);
    // World-space reflection follows mesh normals and camera pose, rather than
    // being painted on projected screen coordinates.
    std::vector<EnvironmentColor> directional(w * h);
    for (int y = 0; y < h; ++y) {
      const double theta = pi * ((y + .5) / h - .5);
      for (int x = 0; x < w; ++x) {
        const double phi       = 2 * pi * ((x + .5) / w - .5);
        directional[y * w + x] = {
            {float(1 + std::cos(theta) * std::cos(phi)), 0, 0}};
      }
    }
    EnvironmentLayer mirror;
    mirror.overrideMaterial = true;
    mirror.reflectivity     = 1;
    mirror.map = std::make_shared<EnvironmentMap>(w, h, std::move(directional));
    options    = RenderOptions();
    options.useLightingRig                   = true;
    options.lighting.preserveDefaultLighting = true;
    options.lighting.environments            = {mirror};
    const std::array<double, 3> center{{1.0 / 3, 1.0 / 3, 1.0 / 3}};
    auto facingScene = prepareRender(triangle(), options);
    near(srgbToLinear(facingScene.sampleShader(0, center).r), 2, .002f);
    options.rotation = {{0, 45, 0}};
    auto turned      = prepareRender(triangle(), options);
    near(srgbToLinear(turned.sampleShader(0, center).r), 1, .002f);
    options.rotation = {{0, 0, 0}};
    ModelTransform camera;
    camera.camera      = true;
    camera.rotation    = {{0, 30, 0}};
    options.transforms = {camera};
    auto cameraTurned  = prepareRender(triangle(), options);
    near(srgbToLinear(cameraTurned.sampleShader(0, center).r),
         float(1 + std::cos(pi / 6)), .002f);
    std::cout << "Environment sampling, material mixing, GLB projection and "
                 "HDR passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

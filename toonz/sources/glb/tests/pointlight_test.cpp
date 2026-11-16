#include "glbrenderer.h"
#include "glbfixture.h"
#include <cmath>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
void near(double a, double b, double eps = 1e-6) {
  check(std::abs(a - b) < eps, "Point Light numerical mismatch");
}
otglb::ColorPixel sample(const otglb::RenderScene &scene, double x = 0,
                         double y = 0) {
  otglb::RenderTile tile;
  tile.width = tile.height = 1;
  tile.x                   = x - .5;
  tile.y                   = y - .5;
  return otglb::renderTile(scene, tile)[0];
}
}  // namespace
int main() {
  try {
    otglb::DiffuseLight light;
    light.point = otglb::PointPlacement{{{0, 0, 5}}, 5};
    light.spot  = otglb::SpotCone{{{0, 0, -1}}, 10, 30};
    light.validate();
    near(light.diffuse({{0, 0, 1}}, {{0, 0, 0}}), 1);
    near(light.diffuse({{0, 0, 1}}, {{5, 0, 0}}), 0);
    near(light.diffuse({{0, 0, 1}}, {{0, 0, 10}}), 0);
    const double x  = 5 * std::tan(20 * 3.141592653589793 / 180);
    const auto soft = light.diffuse({{0, 0, 1}}, {{x, 0, 0}});
    check(soft > 0 && soft < 1, "Klieg cone lacks soft transition");
    auto changed        = light;
    changed.spot->outer = 40;
    check(!(light == changed), "cone edits did not invalidate lighting");
    for (int bad = 0; bad < 4; ++bad) {
      auto invalid = light;
      if (bad == 0) invalid.spot->inner = 40;
      if (bad == 1) invalid.spot->axis = {{0, 0, 0}};
      if (bad == 2)
        invalid.spot->outer = std::numeric_limits<double>::infinity();
      if (bad == 3) invalid.point.reset();
      bool rejected = false;
      try {
        invalid.validate();
      } catch (const std::exception &) {
        rejected = true;
      }
      check(rejected, "invalid spotlight accepted");
    }
    const auto path =
        std::filesystem::temp_directory_path() /
        ("ot3d-point-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".glb");
    const auto bytes = triangleGlb();
    {
      std::ofstream f(path, std::ios::binary);
      f.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }
    auto loaded = otglb::load(path);
    std::filesystem::remove(path);
    check(bool(loaded), "Point Light fixture load");
    otglb::RenderOptions options;
    options.materialColors = true;
    options.useLightingRig = true;
    light.spot->inner = light.spot->outer = 5;
    options.lighting.lights               = {light};
    auto scene = otglb::prepareRender(*loaded.asset, options);
    check(sample(scene).r > .95 && sample(scene, 70, -60).r == 0,
          "Klieg cone was not sampled within a single large triangle");
    const auto center = otglb::targetCenter(scene);
    check(center.has_value(), "GLB target metadata missing");
    near((*center)[0], 0);
    near((*center)[1], 0);
    near((*center)[2], 0);
    auto placed = otglb::targetCenter(scene, {{1, 0, 100, 0, 1, 0}});
    near((*placed)[0], 1);
    options.transforms.push_back({{{2, 3, 4}}});
    auto moved = otglb::prepareRender(*loaded.asset, options);
    near((*moved.worldCenter)[0], 2);
    near((*moved.worldCenter)[1], 3);
    near((*moved.worldCenter)[2], 4);
    // Emitter spheres use the same projection, shader and depth test as
    // geometry.
    auto source = scene;
    otglb::appendVisibleLight(scene, {{0, 0, 1}}, .2, {{2, 0, 0}});
    check(sample(scene).r > 1.99 && sample(scene).g == 0,
          "HDR visible light missing");
    check(sample(source).g > .95, "emitter mutated shared source snapshot");
    auto hidden = source;
    otglb::appendVisibleLight(hidden, {{0, 0, -1}}, .2, {{2, 0, 0}});
    near(sample(hidden).r, sample(source).r);
    auto shifted = source;
    otglb::appendVisibleLight(shifted, {{3, 0, 1}}, .2, {{1, 0, 0}});
    check(shifted.bounds[2] > source.bounds[2],
          "visible light absent from output bounds");
    // Tile boundaries must not alter cone coverage or emissive geometry.
    otglb::RenderTile tile;
    tile.width      = 8;
    tile.height     = 6;
    tile.x          = -4;
    tile.y          = -3;
    const auto full = otglb::renderTile(scene, tile);
    for (int y = 0; y < 6; ++y)
      for (int x = 0; x < 8; ++x) {
        const auto p = sample(scene, tile.x + x + .5, tile.y + y + .5);
        near(p.r, full[y * 8 + x].r);
        near(p.alpha, full[y * 8 + x].alpha);
      }
    // Camera near clipping and world-space Sun direction.
    options.transforms.clear();
    options.perspective = true;
    auto perspective    = otglb::prepareRender(*loaded.asset, options);
    otglb::appendVisibleLight(perspective, {{0, 0, 9.89}}, .05, {{1, 1, 1}});
    check(!perspective.triangles.empty(),
          "near-plane light clipping lost geometry");
    otglb::DiffuseLight sun;
    sun.worldDirection      = true;
    options.perspective     = false;
    options.lighting.lights = {sun};
    const auto front        = otglb::prepareRender(*loaded.asset, options);
    otglb::ModelTransform camera;
    camera.camera      = true;
    camera.rotation[1] = 15;
    options.transforms = {camera};
    const auto rotated = otglb::prepareRender(*loaded.asset, options);
    near(front.triangles[0].color[0], rotated.triangles[0].color[0]);
    // Mixed legacy directional and individual positional lights retain the
    // source camera's normal convention, including nonuniform scaling.
    camera.scale       = {{1.5, 2, 1}};
    options.transforms = {camera};
    sun.worldDirection = false;
    sun.intensity      = .2;
    sun.direction      = {{.5, 0, 1}};
    light.intensity    = .1;
    light.spot.reset();
    options.lighting.lights = {sun, light};
    const auto mixed        = otglb::prepareRender(*loaded.asset, options);
    const auto pixel = mixed.sampleShader(0, {{1.0 / 3, 1.0 / 3, 1.0 / 3}});
    near(pixel.r, mixed.triangles[0].color[0]);
    std::cout << "PASS Point Light cones, sampling, targets, depth, clipping, "
                 "tiles and HDR\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

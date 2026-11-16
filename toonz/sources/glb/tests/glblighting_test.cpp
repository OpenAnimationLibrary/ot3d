#include "glbrenderer.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void identity(otglb::Matrix &m) {
  m.fill(0.0f);
  m[0] = m[5] = m[10] = m[15] = 1.0f;
}

otglb::Asset triangleAsset() {
  otglb::Asset asset;
  asset.materials.resize(1);
  asset.materials[0].baseColor = {{1.0f, 1.0f, 1.0f, 1.0f}};

  otglb::Attribute positions;
  positions.semantic = "POSITION";
  positions.components = 3;
  positions.values = {-1.0f, -1.0f, 0.0f,
                       1.0f, -1.0f, 0.0f,
                       0.0f,  1.0f, 0.0f};

  otglb::Primitive primitive;
  primitive.mode = 4;
  primitive.material = 0;
  primitive.vertexCount = 3;
  primitive.triangleCount = 1;
  primitive.attributes.push_back(positions);

  otglb::Mesh mesh;
  mesh.primitives.push_back(primitive);
  asset.meshes.push_back(mesh);

  otglb::Node node;
  node.mesh = 0;
  identity(node.local);
  identity(node.world);
  asset.nodes.push_back(node);

  otglb::Scene scene;
  scene.roots.push_back(0);
  asset.scenes.push_back(scene);
  asset.defaultScene = 0;
  return asset;
}

std::array<float, 3> color(const otglb::LightingRig &rig,
                           otglb::RenderOptions options = {}) {
  auto asset = triangleAsset();
  options.materialColors = true;
  options.useLightingRig = true;
  options.lighting = rig;
  const auto scene = otglb::prepareRender(asset, options);
  require(scene.triangles.size() == 1, "fixture did not render one triangle");
  return scene.triangles[0].color;
}

void near(float actual, float expected, float tolerance = 0.002f) {
  require(std::fabs(actual - expected) <= tolerance, "unexpected light color");
}
void testToonLines() {
  const auto asset = triangleAsset();
  otglb::RenderOptions options;
  options.orthoHeight    = 100;
  options.materialColors = true;
  const otglb::RenderTile tile{32, 32, -16, -16};
  otglb::ToonLineStyle style;
  style.width             = 2;
  style.lightingInfluence = 1;
  style.color             = {{1, 0, 0, 1}};
  const auto draw         = [&](const otglb::RenderOptions &settings,
                        const otglb::ToonLineStyle &line) {
    otglb::RenderScene out;
    out.toonLine = line;
    out.layers.push_back({std::make_shared<otglb::RenderScene>(
        otglb::prepareRender(asset, settings))});
    return otglb::renderTile(out, tile);
  };
  const auto pixel = [&tile](int x, int y) {
    return std::size_t(y + 16) * tile.width + x + 16;
  };
  const auto strength = [&](const auto &pixels) {
    return pixels[pixel(0, -12)].alpha;
  };
  const auto unlit = draw(options, style);
  require(unlit[pixel(0, -11)].r > .9f && unlit[pixel(0, -11)].g < .1f,
          "unlit GLB silhouette has no colored outline");
  near(strength(unlit), 0);
  options.useLightingRig = true;
  options.lighting.lights.emplace_back();
  near(strength(draw(options, style)), 0);
  options.lighting.lights[0].direction[2] = -1;
  require(strength(draw(options, style)) > .5f,
          "shadow-side GLB outline did not thicken");
  options.lighting.lights[0].point = otglb::PointPlacement{{{0, 0, -2}}, 2};
  require(strength(draw(options, style)) > .5f,
          "positional light did not affect contour thickness");
  options.lighting.lights[0].point->position[2] = 2;
  options.lighting.lights[0].intensity          = 2;
  near(strength(draw(options, style)), 0, .15f);
  options.lighting.lights[0].point->position[2] = -2;
  style.lightingInfluence                       = 0;
  near(strength(draw(options, style)), 0);
}

void testToonOcclusionAndTiles() {
  otglb::RenderScene scene;
  scene.toonLine = otglb::ToonLineStyle{};
  const auto add = [&](double left, double right, double depth) {
    const otglb::ProjectedVertex a{left, 2, depth}, b{right, 2, depth + 2},
        c{right, 18, depth + 2}, d{left, 18, depth};
    for (auto vertices : {std::array<otglb::ProjectedVertex, 3>{{a, b, c}},
                          std::array<otglb::ProjectedVertex, 3>{{a, c, d}}}) {
      otglb::RenderTriangle triangle{vertices, {}, {{1, 1, 1}}};
      triangle.toonSurface = true;
      scene.triangles.push_back(triangle);
    }
  };
  add(2, 30, 0);
  add(16, 28, 10);
  const auto full = otglb::renderTile(scene, {32, 20});
  require(full[8 * 32 + 12].r > .95f,
          "a continuous sloped face produced a tessellation line");
  require(full[8 * 32 + 15].r < .5f && full[8 * 32 + 16].r < .5f,
          "visible depth boundary did not produce a contour");
  for (int offset : {0, 16}) {
    const auto part = otglb::renderTile(scene, {16, 20, double(offset), 0});
    for (int y = 0; y < 20; ++y)
      for (int x = 0; x < 16; ++x) {
        const auto &a = full[y * 32 + x + offset];
        const auto &b = part[y * 16 + x];
        near(a.r, b.r, 1e-6f);
        near(a.alpha, b.alpha, 1e-6f);
      }
  }
}
}  // namespace

int main() {
  try {
    otglb::LightingRig rig;
    rig.ambient = 0.0;
    rig.master = 1.0;
    otglb::DiffuseLight key;
    key.direction = {{0.0, 0.0, 1.0}};
    key.color = {{1.0f, 1.0f, 1.0f}};
    key.intensity = 1.0;
    rig.lights.push_back(key);

    auto lit = color(rig);
    near(lit[0], 1.0f); near(lit[1], 1.0f); near(lit[2], 1.0f);

    rig.lights[0].direction = {{0.0, 0.0, -1.0}};
    lit = color(rig);
    near(lit[0], 0.0f); near(lit[1], 0.0f); near(lit[2], 0.0f);

    rig.lights[0].direction = {{0.0, 0.0, 1.0}};
    rig.lights[0].color = {{1.0f, 0.0f, 0.0f}};
    lit = color(rig);
    near(lit[0], 1.0f); near(lit[1], 0.0f); near(lit[2], 0.0f);

    rig.lights.clear();
    rig.ambient = 0.25;
    lit = color(rig);
    near(lit[0], otglb::linearToSrgb(0.25f));
    near(lit[1], otglb::linearToSrgb(0.25f));
    near(lit[2], otglb::linearToSrgb(0.25f));

    rig.master = 2.0;
    lit = color(rig);
    near(lit[0], otglb::linearToSrgb(0.5f));

    rig.ambient      = 0.0;
    rig.master       = 1.0;
    key.intensity    = 0.5;
    key.point        = otglb::PointPlacement{{{0.0, -1.0 / 3.0, 2.0}}, 2.0};
    rig.lights       = {key};
    const auto front = color(rig);
    near(front[0], otglb::linearToSrgb(0.5f));

    otglb::RenderOptions camera;
    otglb::ModelTransform cameraMove;
    cameraMove.camera      = true;
    cameraMove.rotation[1] = 15.0;
    cameraMove.scale[0]    = 1.5;
    camera.transforms.push_back(cameraMove);
    near(color(rig, camera)[0], front[0]);

    const auto original              = rig;
    rig.lights[0].point->position[2] = 4.0;
    require(!(rig == original),
            "moving a point light did not invalidate the rig");
    near(color(rig)[0], otglb::linearToSrgb(0.2f));
    rig.lights[0].point->position[2] = -2.0;
    near(color(rig)[0], 0.0f);
    rig.lights[0].point->position[2] = 0.0;
    near(color(rig)[0], 0.0f);

    testToonLines();
    testToonOcclusionAndTiles();
    std::cout << "Directional and positional diffuse lighting passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL glblighting: " << error.what() << '\n';
    return 1;
  }
}

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

  options.useLightingRig = false;
  style.bottomWeight     = 1;
  const auto weighted    = draw(options, style);
  require(strength(weighted) > .5f,
          "Bottom Weight did not widen the lower silhouette");
  for (int y = 5; y < 16; ++y)
    for (int x = -16; x < 16; ++x) {
      const auto i = pixel(x, y);
      near(weighted[i].alpha, unlit[i].alpha);
      near(weighted[i].r, unlit[i].r);
      near(weighted[i].g, unlit[i].g);
    }
  options.useLightingRig  = true;
  style.lightingInfluence = 1;
  require(draw(options, style)[pixel(0, -13)].alpha > .5f,
          "Bottom Weight and shadow thickness did not combine");
}

void testBottomContourWeight() {
  const auto rectangle = [](otglb::RenderScene &scene, double y, double depth) {
    const otglb::ProjectedVertex a{8, y, depth}, b{32, y, depth},
        c{32, y + 15, depth}, d{8, y + 15, depth};
    for (auto vertices : {std::array<otglb::ProjectedVertex, 3>{{a, b, c}},
                          std::array<otglb::ProjectedVertex, 3>{{a, c, d}}}) {
      otglb::RenderTriangle triangle{vertices, {}, {{1, 1, 1}}};
      triangle.toonSurface = true;
      scene.triangles.push_back(triangle);
    }
  };
  for (bool overlap : {false, true}) {
    otglb::RenderScene scene;
    scene.toonLine                    = otglb::ToonLineStyle{};
    scene.toonLine->width             = 2;
    scene.toonLine->lightingInfluence = 0;
    if (overlap) {
      // A large rear surface makes both rectangles internal depth contours.
      const otglb::ProjectedVertex a{0, 0, 0}, b{40, 0, 0}, c{40, 68, 0},
          d{0, 68, 0};
      for (auto vertices : {std::array<otglb::ProjectedVertex, 3>{{a, b, c}},
                            std::array<otglb::ProjectedVertex, 3>{{a, c, d}}}) {
        otglb::RenderTriangle triangle{vertices, {}, {{1, 1, 1}}};
        triangle.toonSurface = true;
        scene.triangles.push_back(triangle);
      }
    }
    rectangle(scene, 10, 5);
    rectangle(scene, 45, 5);
    const otglb::RenderTile tile{40, 68};
    const auto base              = otglb::renderTile(scene, tile);
    scene.toonLine->bottomWeight = 1;
    const auto heavy             = otglb::renderTile(scene, tile);
    const auto ink               = [&](const auto &pixels, int x, int y) {
      const auto &p = pixels[y * tile.width + x];
      return overlap ? 1 - p.r : p.alpha;
    };
    for (int bottom : {10, 45}) {
      require(ink(heavy, 20, bottom - 2) > ink(base, 20, bottom - 2) + .4,
              "Bottom Weight missed an underside above the model base");
      // Sample far enough from the corners to exclude rounded stroke caps.
      for (int y = bottom + 5; y < bottom + 10; ++y)
        for (int x : {6, 7, 8, 9, 30, 31, 32, 33}) {
          const auto i = y * tile.width + x;
          near(heavy[i].r, base[i].r, 1e-6f);
          near(heavy[i].alpha, base[i].alpha, 1e-6f);
        }
      for (int y = bottom + 13; y <= bottom + 17; ++y)
        for (int x = 15; x <= 25; ++x) {
          const auto i = y * tile.width + x;
          near(heavy[i].r, base[i].r, 1e-6f);
          near(heavy[i].alpha, base[i].alpha, 1e-6f);
        }
    }
    // Tile origins do not change which direction a contour faces.
    const auto part = otglb::renderTile(scene, {24, 22, 6, 38});
    for (int y = 0; y < 22; ++y)
      for (int x = 0; x < 24; ++x) {
        const auto &full = heavy[(y + 38) * tile.width + x + 6];
        near(full.r, part[y * 24 + x].r, 1e-6f);
        near(full.alpha, part[y * 24 + x].alpha, 1e-6f);
      }
  }
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
void testWideToonStrokes() {
  const auto quad = [](otglb::RenderScene &scene, double x0, double y0,
                       double x1, double y1, double depth) {
    const otglb::ProjectedVertex a{x0, y0, depth}, b{x1, y0, depth},
        c{x1, y1, depth}, d{x0, y1, depth};
    for (auto vertices : {std::array<otglb::ProjectedVertex, 3>{{a, b, c}},
                          std::array<otglb::ProjectedVertex, 3>{{a, c, d}}}) {
      otglb::RenderTriangle triangle{vertices, {}, {{1, 1, 1}}};
      triangle.toonSurface = true;
      scene.triangles.push_back(triangle);
    }
  };
  otglb::RenderScene source;
  quad(source, 15, 15, 155, 155, 0);
  quad(source, 60, 60, 64, 64, 1);  // A short depth fragment.
  quad(source, 100, 40, 104, 130, 1);
  source.toonLine                    = otglb::ToonLineStyle{};
  source.toonLine->width             = 20;
  source.toonLine->lightingInfluence = 0;
  const otglb::RenderTile tile{170, 170};
  const auto wide = otglb::renderTile(source, tile);
  for (int y = 50; y < 74; ++y)
    for (int x = 50; x < 74; ++x)
      require(wide[y * 170 + x].r > .99f,
              "wide stroke turned a short depth fragment into a blot");
  require(wide[80 * 170 + 95].r < .01f,
          "wide stroke removed a continuous occlusion contour");
  const auto part = otglb::renderTile(source, {35, 45, 85, 65});
  for (int y = 0; y < 45; ++y)
    for (int x = 0; x < 35; ++x) {
      const auto &full = wide[(y + 65) * 170 + x + 85];
      near(full.r, part[y * 35 + x].r, 1e-6f);
      near(full.alpha, part[y * 35 + x].alpha, 1e-6f);
    }

  // Curved, finely tessellated silhouettes must remain connected at high
  // width even with rapidly changing per-face lighting.
  otglb::RenderScene circle;
  constexpr int segments = 96;
  const double pi        = std::acos(-1.0);
  for (int i = 0; i < segments; ++i) {
    const double a = 2 * pi * i / segments, b = 2 * pi * (i + 1) / segments;
    otglb::RenderTriangle triangle{
        {{{85, 85, 0},
          {85 + 45 * std::cos(a), 85 + 45 * std::sin(a), 0},
          {85 + 45 * std::cos(b), 85 + 45 * std::sin(b), 0}}},
        {},
        {{1, 1, 1}}};
    triangle.toonSurface  = true;
    triangle.illumination = i % 2 ? 0.f : 1.f;
    circle.triangles.push_back(triangle);
  }
  circle.toonLine                    = otglb::ToonLineStyle{};
  circle.toonLine->width             = 20;
  circle.toonLine->lightingInfluence = 1;
  const auto curved                  = otglb::renderTile(circle, tile);
  for (int i = 0; i < 360; ++i) {
    const double theta = i * pi / 180;
    const int x        = int(std::floor(85 + 48 * std::cos(theta))),
              y        = int(std::floor(85 + 48 * std::sin(theta)));
    const auto &p      = curved[y * 170 + x];
    require(p.alpha > .99f && p.r < .01f,
            "wide curved outline has a gap or soft interior");
  }
  const auto curvedPart = otglb::renderTile(circle, {35, 45, 95, 70});
  for (int y = 0; y < 45; ++y)
    for (int x = 0; x < 35; ++x) {
      const auto &full = curved[(y + 70) * 170 + x + 95];
      near(full.r, curvedPart[y * 35 + x].r, 1e-6f);
      near(full.alpha, curvedPart[y * 35 + x].alpha, 1e-6f);
    }
}

void testToonSpeckles() {
  otglb::RenderScene source;
  const auto quad = [&](int x0, int y0, int x1, int y1, double depth) {
    const otglb::ProjectedVertex a{double(x0), double(y0), depth},
        b{double(x1), double(y0), depth}, c{double(x1), double(y1), depth},
        d{double(x0), double(y1), depth};
    for (auto vertices : {std::array<otglb::ProjectedVertex, 3>{{a, b, c}},
                          std::array<otglb::ProjectedVertex, 3>{{a, c, d}}}) {
      otglb::RenderTriangle triangle{vertices, {}, {{1, 1, 1}}};
      triangle.toonSurface = true;
      source.triangles.push_back(triangle);
    }
  };
  // Leave a one-pixel tessellation gap and add a tiny depth discontinuity.
  quad(2, 2, 16, 22, 0);
  quad(17, 2, 29, 22, 0);
  quad(16, 2, 17, 11, 0);
  quad(16, 12, 17, 22, 0);
  quad(9, 8, 10, 9, 1);
  // A narrow but continuous overlap and a tiny standalone silhouette.
  quad(23, 6, 24, 16, 1);
  quad(36, 15, 37, 16, 1);
  const otglb::RenderTile tile{42, 24};
  const auto unoutlined = otglb::renderTile(source, tile);
  const auto pixel      = [&](int x, int y) { return y * tile.width + x; };
  for (double width : {1.5, 5.0}) {
    auto outlined            = source;
    outlined.toonLine        = otglb::ToonLineStyle{};
    outlined.toonLine->width = width;
    const auto full          = otglb::renderTile(outlined, tile);
    for (int y = 5; y <= 15; ++y)
      for (int x = 7; x <= 19; ++x) {
        const int i = pixel(x, y);
        require(std::abs(full[i].r - unoutlined[i].r) < 1e-6f &&
                    std::abs(full[i].alpha - unoutlined[i].alpha) < 1e-6f,
                "a tiny depth boundary or pinhole grew into a Toon Line speck");
      }
    require(full[pixel(22, 10)].r < .75f,
            "a continuous thin overlap lost its line");
    require(full[pixel(35, 15)].alpha > .1f,
            "a tiny exterior silhouette lost its line");
    const auto sliced = otglb::renderTile(outlined, {17, 12, 0, 0});
    for (int y = 0; y < 12; ++y)
      for (int x = 0; x < 17; ++x) {
        near(full[pixel(x, y)].r, sliced[y * 17 + x].r, 1e-6f);
        near(full[pixel(x, y)].alpha, sliced[y * 17 + x].alpha, 1e-6f);
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
    testBottomContourWeight();
    testToonOcclusionAndTiles();
    testToonSpeckles();
    testWideToonStrokes();
    std::cout << "Directional and positional diffuse lighting passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL glblighting: " << error.what() << '\n';
    return 1;
  }
}

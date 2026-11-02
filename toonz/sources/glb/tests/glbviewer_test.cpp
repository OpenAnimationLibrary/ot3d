#include "glbviewer.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace otglb;
namespace {
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
void near(double a, double b) {
  check(std::abs(a - b) < 1e-5, "Viewer and reference transform differ");
}
template <class F>
void rejects(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::runtime_error &) {
    caught = true;
  }
  check(caught, "Expected explicit failure");
}
Asset triangle() {
  Asset asset;
  Primitive p;
  p.vertexCount   = 3;
  p.triangleCount = 1;
  p.attributes.push_back({"POSITION", 3, {-1, -1, 0, 1, -1, 0, 0, 1, 0}});
  asset.meshes.push_back({"triangle", {p}, {}});
  Node node;
  node.mesh  = 0;
  node.local = node.world = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  asset.nodes.push_back(node);
  asset.scenes.push_back({"scene", {0}, {}});
  asset.defaultScene = 0;
  return asset;
}

void testTriangleTopologies() {
  for (int mode : {5, 6}) {
    for (bool indexed : {false, true}) {
      auto asset              = triangle();
      auto &primitive         = asset.meshes[0].primitives[0];
      primitive.mode          = mode;
      primitive.vertexCount   = 4;
      primitive.triangleCount = 2;
      primitive.attributes[0].values =
          mode == 5
              ? std::vector<float>{-1, -1, 0, 1, -1, 0, -1, 1, 0, 1, 1, 0}
              : std::vector<float>{-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0};
      if (indexed) primitive.indices = {3, 2, 1, 0};
      RenderOptions options;
      const auto viewer    = prepareViewerGeometry(asset, options, 2);
      const auto reference = prepareRender(asset, options);
      check(viewer.triangles.size() == 2 && reference.triangles.size() == 2,
            "Strip/fan did not produce the reference triangles");
      for (int t = 0; t < 2; ++t) {
        for (int corner = 0; corner < 3; ++corner) {
          // The two-sided renderer leaves odd strip winding reversed. The
          // Viewer keeps consistent winding while preserving the same surface.
          const int referenceCorner =
              mode == 5 && t % 2 && corner < 2 ? 1 - corner : corner;
          const auto &v = viewer.triangles[t].vertices[corner];
          const auto &r = reference.triangles[t].vertices[referenceCorner];
          near(v[0] * viewer.stageUnitsPerModelUnit, r.x);
          near(v[1] * viewer.stageUnitsPerModelUnit, r.y);
          near(v[2] - options.cameraDistance, r.depth);
        }
        const auto &v        = viewer.triangles[t].vertices;
        const double winding = (v[1][0] - v[0][0]) * (v[2][1] - v[0][1]) -
                               (v[1][1] - v[0][1]) * (v[2][0] - v[0][0]);
        check(indexed && mode == 6 ? winding < 0 : winding > 0,
              "Strip/fan triangle winding changed");
      }
      rejects([&] { prepareViewerGeometry(asset, options, 1); });
      primitive.indices = {0, 1, 2, 4};
      rejects([&] { prepareViewerGeometry(asset, options); });
      primitive.indices = {0, 1};
      rejects([&] { prepareViewerGeometry(asset, options); });
    }
  }
}

void testOpaqueAlpha() {
  auto asset = triangle();
  asset.materials.push_back(Material());
  asset.meshes[0].primitives[0].material = 0;
  RenderOptions options;
  options.materialColors = true;
  for (float alpha : {0.0f, 0.5f, 1.0f}) {
    asset.materials[0].baseColor[3] = alpha;
    const auto geometry             = prepareViewerGeometry(asset, options);
    check(geometry.triangles.size() == 1 && geometry.warnings.empty(),
          "Opaque material disappeared because of its stored alpha factor");
  }
  for (int mode : {1, 2}) {
    asset.materials[0].alphaMode = mode;
    const auto geometry          = prepareViewerGeometry(asset, options);
    check(geometry.triangles.empty() && !geometry.warnings.empty(),
          "Unsupported mask/blend material was not reported");
  }
}
}  // namespace

int main() {
  try {
    testTriangleTopologies();
    testOpaqueAlpha();
    auto asset = triangle();
    RenderOptions options;
    options.position     = {{1, 2, 0.5}};
    options.rotation     = {{15, 25, 35}};
    options.scale        = 0.8;
    const auto viewer    = prepareViewerGeometry(asset, options);
    const auto reference = prepareRender(asset, options);
    check(viewer.triangles.size() == 1 && reference.triangles.size() == 1,
          "Missing reference triangle");
    // Orthographic camera reference and unprojected scene geometry must agree
    // on XY while retaining the real Z coordinate for navigable 3D views.
    for (int i = 0; i < 3; ++i) {
      const auto &v = viewer.triangles[0].vertices[i];
      const auto &r = reference.triangles[0].vertices[i];
      near(v[0] * viewer.stageUnitsPerModelUnit, r.x);
      near(v[1] * viewer.stageUnitsPerModelUnit, r.y);
      near(v[2] - options.cameraDistance, r.depth);
    }
    check(viewer.triangles[0].vertices[0][2] !=
              viewer.triangles[0].vertices[1][2],
          "Depth was flattened");

    // Node animation uses the same evaluator as the final renderer.
    Animation animation;
    animation.lastKeyTime = 1;
    animation.samplers.push_back(
        {AnimationInterpolation::Linear, 3, {0, 1}, {0, 0, 0, 2, 0, 0}});
    animation.channels.push_back({0, 0, AnimationPath::Translation, 3});
    asset.animations.push_back(animation);
    options               = RenderOptions();
    options.animation     = 0;
    options.sourceSeconds = 0.5;
    const auto animated   = prepareViewerGeometry(asset, options);
    near(animated.triangles[0].vertices[0][0], 0);

    asset                                 = triangle();
    options                               = RenderOptions();
    asset.meshes[0].primitives[0].indices = {2, 1, 0};
    near(prepareViewerGeometry(asset, options).triangles[0].vertices[0][1], 1);
    asset.meshes[0].primitives[0].indices[0] = 3;
    rejects([&] { prepareViewerGeometry(asset, options); });
    asset = triangle();
    rejects([&] { prepareViewerGeometry(asset, options, 0); });
    asset.meshes[0].primitives[0].attributes[0].values[0] =
        std::numeric_limits<float>::infinity();
    rejects([&] { prepareViewerGeometry(asset, options); });

    asset                  = triangle();
    asset.nodes[0].hasSkin = true;
    asset.nodes[0].skin    = 0;
    Skin binding;
    binding.joints              = {0};
    binding.inverseBindMatrices = {asset.nodes[0].local};
    asset.skins.push_back(binding);
    const auto skin = prepareViewerGeometry(asset, options);
    check(skin.triangles.empty() && !skin.warnings.empty(),
          "Unsupported skin should be reported");
    asset = triangle();
    asset.materials.push_back(Material());
    asset.meshes[0].primitives[0].material = 0;
    asset.materials[0].alphaMode           = 2;
    const auto alpha = prepareViewerGeometry(asset, options);
    check(alpha.triangles.empty() && !alpha.warnings.empty(),
          "Transparent primitive should be reported");
    options.scale = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { prepareViewerGeometry(asset, options); });
    std::cout
        << "PASS Viewer reference transforms, retained depth, node animation, "
           "indexing, budgets, invalid data and unsupported-feature "
           "diagnostics\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

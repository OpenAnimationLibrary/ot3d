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
}  // namespace

int main() {
  try {
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

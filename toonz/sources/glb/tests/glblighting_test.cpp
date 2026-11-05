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

    std::cout << "Directional and positional diffuse lighting passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL glblighting: " << error.what() << '\n';
    return 1;
  }
}

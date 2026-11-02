#include "glbviewer.h"
#include "glbpose.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace otglb {
namespace {
using Point         = std::array<double, 3>;
constexpr double Pi = 3.14159265358979323846;

void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

void warn(ViewerGeometry &out, const char *message) {
  if (std::find(out.warnings.begin(), out.warnings.end(), message) ==
      out.warnings.end())
    out.warnings.push_back(message);
}

Point instance(Point p, const RenderOptions &o) {
  for (double &v : p) v *= o.scale;
  for (int axis = 0; axis < 3; ++axis) {
    const int a = (axis + 1) % 3, b = (axis + 2) % 3;
    const double s  = std::sin(o.rotation[axis] * Pi / 180.0);
    const double c  = std::cos(o.rotation[axis] * Pi / 180.0);
    const double va = p[a], vb = p[b];
    p[a] = c * va - s * vb;
    p[b] = s * va + c * vb;
  }
  for (int i = 0; i < 3; ++i) {
    p[i] += o.position[i];
    require(std::isfinite(p[i]), "Non-finite Viewer vertex");
  }
  return p;
}
}  // namespace

ViewerGeometry prepareViewerGeometry(const Asset &asset, const RenderOptions &o,
                                     std::size_t maxTriangles) {
  require(std::isfinite(o.scale) && o.scale > 0 &&
              std::isfinite(o.orthoHeight) && o.orthoHeight > 0,
          "Viewer scale and reference height must be positive and finite");
  for (double v : o.position)
    require(std::isfinite(v), "Non-finite Viewer instance position");
  for (double v : o.rotation)
    require(std::isfinite(v), "Non-finite Viewer instance rotation");
  require(o.transforms.empty(),
          "Viewer downstream transforms are not supported yet");

  ViewerGeometry out;
  out.stageUnitsPerModelUnit = ProjectionHeight / o.orthoHeight;
  require(std::isfinite(out.stageUnitsPerModelUnit),
          "Viewer reference scale overflow");
  out.wireframe = o.wireframe;
  if (o.perspective)
    warn(out,
         "Viewer uses the OT3D stage camera; per-FX perspective is not "
         "reproduced.");
  if (asset.scenes.empty()) return out;
  const int scene = asset.defaultScene == NoIndex ? 0 : asset.defaultScene;
  require(scene >= 0 && std::size_t(scene) < asset.scenes.size(),
          "Invalid Viewer scene index");
  const auto evaluated = evaluatePose(asset, o.animation, o.sourceSeconds);
  require(bool(evaluated), evaluated.error.c_str());
  out.warnings.insert(out.warnings.end(), evaluated.warnings.begin(),
                      evaluated.warnings.end());

  std::vector<int> pending = asset.scenes[scene].roots;
  std::vector<bool> visited(asset.nodes.size(), false);
  while (!pending.empty()) {
    const int index = pending.back();
    pending.pop_back();
    require(index >= 0 && std::size_t(index) < asset.nodes.size(),
            "Invalid Viewer node index");
    if (visited[index]) continue;
    visited[index]   = true;
    const auto &node = asset.nodes[index];
    pending.insert(pending.end(), node.children.begin(), node.children.end());
    if (node.mesh == NoIndex) continue;
    require(node.mesh >= 0 && std::size_t(node.mesh) < asset.meshes.size(),
            "Invalid Viewer mesh index");
    if (node.hasSkin || node.skin != NoIndex) {
      warn(out, "Skinned meshes are not drawn by the initial hybrid Viewer.");
      continue;
    }
    const auto &matrix = evaluated.pose.nodes[index].world;
    for (const auto &primitive : asset.meshes[node.mesh].primitives) {
      if (primitive.mode != 4 || primitive.hasMorphTargets) {
        warn(out,
             "Only triangle primitives without morph targets are drawn "
             "initially.");
        continue;
      }
      if (primitive.material != NoIndex) {
        require(primitive.material >= 0 &&
                    std::size_t(primitive.material) < asset.materials.size(),
                "Invalid Viewer material index");
        const auto &material = asset.materials[primitive.material];
        if (material.alphaMode != 0 || material.baseColor[3] < 1.0f) {
          warn(out,
               "Transparent and alpha-cutout materials are not drawn "
               "initially.");
          continue;
        }
        if (material.baseColorTexture.texture != NoIndex)
          warn(
              out,
              "Viewer shows base color factors; texture sampling is deferred.");
      }
      const auto position = std::find_if(
          primitive.attributes.begin(), primitive.attributes.end(),
          [](const Attribute &a) { return a.semantic == "POSITION"; });
      require(position != primitive.attributes.end() &&
                  position->components == 3 &&
                  position->values.size() / 3 == primitive.vertexCount &&
                  position->values.size() % 3 == 0,
              "Invalid Viewer POSITION attribute");
      const std::size_t count = primitive.indices.empty()
                                    ? primitive.vertexCount
                                    : primitive.indices.size();
      require(count % 3 == 0, "Incomplete Viewer triangle");
      require(out.triangles.size() <= maxTriangles &&
                  count / 3 <= maxTriangles - out.triangles.size(),
              "Viewer triangle budget exceeded");
      const std::size_t material = primitive.material == NoIndex
                                       ? asset.materials.size()
                                       : std::size_t(primitive.material);
      auto color = o.materialColors ? materialColor(asset, material)
                                    : std::array<float, 3>{{0.7f, 0.7f, 0.7f}};
      if (o.materialColors && material < o.colors.size())
        color = o.colors[material];
      for (float v : color)
        require(std::isfinite(v) && v >= 0 && v <= 1,
                "Invalid Viewer base color");
      for (std::size_t t = 0; t < count; t += 3) {
        ViewerTriangle triangle;
        triangle.color = color;
        for (int corner = 0; corner < 3; ++corner) {
          const auto vertex = primitive.indices.empty()
                                  ? t + corner
                                  : std::size_t(primitive.indices[t + corner]);
          require(vertex < primitive.vertexCount,
                  "Invalid Viewer vertex index");
          const float *p = position->values.data() + vertex * 3;
          Point world;
          for (int row = 0; row < 3; ++row)
            world[row] = matrix[row] * double(p[0]) +
                         matrix[4 + row] * double(p[1]) +
                         matrix[8 + row] * double(p[2]) + matrix[12 + row];
          triangle.vertices[corner] = instance(world, o);
        }
        out.triangles.push_back(triangle);
      }
    }
  }
  return out;
}
}  // namespace otglb

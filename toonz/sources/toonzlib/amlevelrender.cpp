#include "toonz/amlevelrender.h"
#include "ambones.h"
#include "ext/plasticskeletondeformation.h"
#include "toonz/sceneproperties.h"
#include "toonz/toonzscene.h"
#include "toonz/t3drendersource.h"
#include "toonz/txshcell.h"
#include "toonz/tcolumnfx.h"
#include "toonz/txshlevelcolumn.h"
#include "toonz/txshcolumn.h"
#include "toonz/txshamlevel.h"
#include "toonz/txshglblevel.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txsheet.h"
#include "toonz/tstageobject.h"
#include "toonz/tstageobjecttree.h"
#include "toonz/stage.h"
#include "tfxattributes.h"
#include "toutputproperties.h"
#include "ttile.h"
#include "traster.h"
#include "tpixel.h"
#include <QCryptographicHash>
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>

namespace {
const double amStageUnits = Stage::inch / 2.54;
}

AMModelCamera amModelCamera(TXsheet *xsheet, double frame, bool preview) {
  const auto id =
      preview ? xsheet->getStageObjectTree()->getCurrentPreviewCameraId()
              : xsheet->getStageObjectTree()->getCurrentCameraId();
  auto *camera          = xsheet->getStageObject(id);
  const double distance = 1000.0 + camera->getZ(frame);
  if (!std::isfinite(distance) || distance <= 0)
    throw std::runtime_error(
        "Spatial camera must remain in front of its film plane");
  AMModelCamera result;
  // The historical optical offset belongs to the camera's parent space.
  // A spatial parent must move the eye with the rig, not rotate the world
  // around a fixed eye. The animated Z channel itself remains unchanged.
  const auto parentId = camera->getParent();
  const auto parent =
      parentId == TStageObjectId::NoneId
          ? otam::identity()
          : xsheet->getStageObject(parentId)->getAMPlacement(frame);
  const auto opticalOffset = otam::multiply(
      parent,
      otam::multiply(otam::translation(0, 0, 1000), otam::inverse(parent)));
  result.pose  = otam::multiply(opticalOffset, camera->getAMPlacement(frame));
  const auto a = camera->getPlacement(frame) * TScale(distance / 1000.0);
  result.imageToStage = {
      {a.a11, a.a12, 0, a.a13, a.a21, a.a22, 0, a.a23, 0, 0, 1, 0, 0, 0, 0, 1}};
  return result;
}
static otam::Matrix modelPlacement(TXsheet *xsheet, int column, double frame,
                                   double units) {
  auto *object       = xsheet->getStageObject(TStageObjectId::ColumnId(column));
  const double scale = units * (1.0 - object->getGlobalNoScaleZ() / 1000.0);
  return otam::multiply(object->getAMPlacement(frame),
                        otam::scaling(scale, scale, scale));
}

otam::Matrix amModelPlacement(TXsheet *xsheet, int column, double frame) {
  return modelPlacement(xsheet, column, frame, amStageUnits);
}

otam::Matrix amPlasticPlacement(TXsheet *xsheet, int column, double frame) {
  return modelPlacement(xsheet, column, frame, 1.0);
}

double amActionTime(TXsheet *xsheet, int startRow, double frame) {
  const auto *scene = xsheet ? xsheet->getScene() : nullptr;
  const double fps =
      scene ? scene->getProperties()->getOutputProperties()->getFrameRate()
            : 24.0;
  return (frame - startRow) / (std::isfinite(fps) && fps > 0 ? fps : 24.0);
}

namespace {
bool validModelFrame(double frame) {
  return frame >= 0 && frame < double(std::numeric_limits<int>::max()) + 1;
}

AMActionBinding matchingAction(TXsheet *xsheet, int column,
                               const TXshLevelP &level) {
  auto *xcolumn     = xsheet->getColumn(column);
  auto *levelColumn = xcolumn ? xcolumn->getLevelColumn() : nullptr;
  if (!levelColumn) return {};
  auto binding = levelColumn->getAMAction();
  if (binding.level != level) return {};
  return binding;
}

std::vector<otam::PlasticBone> amPlasticBones(TXsheet *xsheet, int column,
                                              double frame) {
  const auto *object = xsheet->getStageObject(TStageObjectId::ColumnId(column));
  const auto deformation = object->getPlasticSkeletonDeformation();
  if (!deformation) return {};
  const double time    = object->paramsTime(frame);
  const int skeletonId = deformation->skeletonId(time);
  const auto rest      = deformation->skeleton(skeletonId);
  if (!rest || rest->vertices().empty()) return {};

  const bool spatial = deformation->isSpatial();
  PlasticSkeleton posed;
  SkD::BoneFrames frames;
  if (spatial)
    deformation->storeSpatialSkeleton(skeletonId, time, posed, &frames);
  else
    deformation->storeDeformedSkeleton(skeletonId, time, posed);
  const auto vector = [](const T3DPointD &p) {
    return otam::Vec3{p.x, p.y, p.z};
  };
  const auto modelPoint = [&](const PlasticSkeletonVertex &v) {
    return vector(v.position3D() / amStageUnits);
  };
  std::vector<otam::PlasticBone> bones;
  bones.reserve(rest->vertices().size());
  bool moves = false;
  for (auto vertex = rest->vertices().begin(); vertex != rest->vertices().end();
       ++vertex) {
    const int parent = vertex->parent();
    if (parent < 0) continue;
    const auto from      = modelPoint(rest->vertex(parent));
    const auto to        = modelPoint(*vertex);
    const auto posedFrom = modelPoint(posed.vertex(parent));
    const auto posedTo   = modelPoint(posed.vertex(vertex.m_idx));
    otam::PlasticBone bone{from, to, posedFrom, posedTo, spatial};
    if (spatial) {
      const auto &basis = frames.at(vertex.m_idx);
      bone.restSide     = vector(basis.first.side);
      bone.posedSide    = vector(basis.second.side);
      bone.posedAxis    = vector(basis.second.axis);
    }
    moves |= rest->vertex(parent).position3D() !=
                 posed.vertex(parent).position3D() ||
             vertex->position3D() != posed.vertex(vertex.m_idx).position3D() ||
             bone.restSide != bone.posedSide;
    bones.push_back(bone);
  }
  // A spatial rig can translate the entire model even with only its root.
  if (spatial &&
      (bones.empty() || std::all_of(bones.begin(), bones.end(),
                                    [](const otam::PlasticBone &bone) {
                                      return bone.restStart == bone.restEnd;
                                    }))) {
    const auto root = rest->vertices().begin();
    if (root->position3D() == posed.vertex(root.m_idx).position3D()) return {};
    auto from      = modelPoint(*root),
         posedFrom = modelPoint(posed.vertex(root.m_idx));
    auto to = from, posedTo = posedFrom;
    to[0] += 1.0;
    posedTo[0] += 1.0;
    return {{from, to, posedFrom, posedTo, true}};
  }
  if (!moves) bones.clear();
  return bones;
}

otam::Matrix modelTransform(const otglb::ModelTransform &t) {
  for (double v : t.position)
    if (!std::isfinite(v)) throw std::runtime_error("Invalid 3D position");
  for (double v : t.rotation)
    if (!std::isfinite(v)) throw std::runtime_error("Invalid 3D rotation");
  for (double v : t.scale)
    if (!std::isfinite(v) || v <= 0)
      throw std::runtime_error("3D scale must be positive and finite");
  return otam::multiply(
      otam::translation(t.position[0], t.position[1], t.position[2]),
      otam::multiply(
          otam::rotation(t.rotation[0], t.rotation[1], t.rotation[2]),
          otam::scaling(t.scale[0], t.scale[1], t.scale[2])));
}
}  // namespace

std::shared_ptr<const otam::Model> amModelAt(TXsheet *xsheet, int column,
                                             double frame) {
  if (!xsheet || column < 0 || !validModelFrame(frame)) return {};
  const auto cell = xsheet->getCell(int(std::floor(frame)), column);
  auto *level     = cell.isEmpty() ? nullptr : cell.m_level->getAMLevel();
  if (!level) return {};
  const auto binding = matchingAction(xsheet, column, cell.m_level);
  if (!binding.action) return level->editableModel();
  return level->actionModel(binding.action,
                            amActionTime(xsheet, binding.startRow, frame));
}

static std::shared_ptr<const otam::Mesh> prepareModelGeometryImpl(
    TXsheet *xsheet, int column, double frame, bool projectToCamera,
    bool previewCamera, bool allowGlb, bool applyPlastic = true,
    bool applyNativeBones = true, AMLevelCagePose *cagePose = nullptr) {
  if (!xsheet || column < 0 || !validModelFrame(frame)) return {};
  const int row   = int(std::floor(frame));
  const auto cell = xsheet->getCell(row, column);
  if (cell.isEmpty()) return {};
  std::shared_ptr<const otam::Mesh> mesh;
  double units = amStageUnits;
  if (auto *am = cell.m_level->getAMLevel()) {
    const auto binding = matchingAction(xsheet, column, cell.m_level);
    const auto bones   = applyPlastic ? amPlasticBones(xsheet, column, frame)
                                      : std::vector<otam::PlasticBone>();
    auto *xcol         = xsheet->getColumn(column);
    auto *levelColumn  = xcol ? xcol->getLevelColumn() : nullptr;
    const auto rig = applyNativeBones && levelColumn ? levelColumn->getAMBones()
                                                     : AMBoneBinding{};
    mesh               = am->renderMesh(row, binding.action,
                                        amActionTime(xsheet, binding.startRow, frame), bones,
                          rig.level == cell.m_level ? rig.animation : nullptr,
                                        frame, cagePose);
    if (!mesh)
      throw std::runtime_error(
          am->error().empty() ? "A:M model source is not loaded" : am->error());
  } else if (auto *glb = allowGlb ? cell.m_level->getGlbLevel() : nullptr) {
    const auto geometry = glb->geometry();
    if (!geometry)
      throw std::runtime_error(glb->error().empty()
                                   ? "GLB model source is not loaded"
                                   : glb->error());
    // Adapt only the draw triangles. The GLB asset remains a separate
    // resource and never becomes an A:M document or spline/patch level.
    auto triangles         = std::make_shared<otam::Mesh>();
    triangles->warnings    = geometry->warnings;
    triangles->amMaterials = false;
    triangles->triangles.reserve(geometry->triangles.size());
    for (const auto &triangle : geometry->triangles)
      triangles->triangles.push_back({triangle.vertices, triangle.color});
    units = geometry->stageUnitsPerModelUnit;
    mesh  = std::move(triangles);
  } else {
    return {};
  }
  const auto matrix = modelPlacement(xsheet, column, frame, units);
  auto result = std::make_shared<otam::Mesh>(otam::transformed(*mesh, matrix));
  if (projectToCamera) {
    const auto camera = amModelCamera(xsheet, frame, previewCamera);
    *result = otam::projected(*result, camera.pose, camera.imageToStage);
  }
  return result;
}

std::shared_ptr<const otam::Mesh> prepareAMLevelGeometry(TXsheet *xsheet,
                                                         int column,
                                                         double frame,
                                                         bool projectToCamera,
                                                         bool previewCamera) {
  return prepareModelGeometryImpl(xsheet, column, frame, projectToCamera,
                                  previewCamera, false);
}

std::shared_ptr<const otam::Mesh> prepareNativeModelGeometry(
    TXsheet *xsheet, int column, double frame, bool projectToCamera,
    bool applyPlastic, bool applyNativeBones) {
  return prepareModelGeometryImpl(xsheet, column, frame, projectToCamera, false,
                                  true, applyPlastic, applyNativeBones);
}

namespace {
QVector3D toVector(const otam::Vec3 &point) {
  return {float(point[0]), float(point[1]), float(point[2])};
}

void updateClosestModelHit(const QVector3D &origin, const QVector3D &ray,
                           const otam::Triangle &triangle, double &distance,
                           QVector3D &normal) {
  const QVector3D a        = toVector(triangle.vertices[0]);
  const QVector3D u        = toVector(triangle.vertices[1]) - a;
  const QVector3D v        = toVector(triangle.vertices[2]) - a;
  const QVector3D cross    = QVector3D::crossProduct(ray, v);
  const double determinant = QVector3D::dotProduct(u, cross);
  if (std::abs(determinant) < 1e-8) return;
  const QVector3D offset = origin - a;
  const double s         = QVector3D::dotProduct(offset, cross) / determinant;
  if (s < 0 || s > 1) return;
  const QVector3D q = QVector3D::crossProduct(offset, u);
  const double t    = QVector3D::dotProduct(ray, q) / determinant;
  if (t < 0 || s + t > 1) return;
  const double hit = QVector3D::dotProduct(v, q) / determinant;
  // The ray spans 1000 optical units. Match the renderer's near plane.
  if (!std::isfinite(hit) || hit <= 0.001 || hit >= distance) return;
  distance = hit;
  normal   = QVector3D::crossProduct(u, v).normalized();
  if (QVector3D::dotProduct(normal, ray) > 0) normal = -normal;
}
}  // namespace

otam::Vec3 nativeModelDropPosition(TXsheet *xsheet, int frame,
                                   const TPointD &imagePoint, bool snap,
                                   double halfExtent) {
  const auto camera      = amModelCamera(xsheet, frame, false);
  const auto film   = otam::transform(otam::inverse(camera.imageToStage),
                                      {imagePoint.x, imagePoint.y, 0});
  const auto eye    = otam::transform(camera.pose, {0, 0, 0});
  const auto focus  = otam::transform(camera.pose, {film[0], film[1], -1000});
  const QVector3D origin = toVector(eye);
  const QVector3D ray    = toVector(focus) - origin;
  const auto table =
      xsheet->getStageObject(TStageObjectId::TableId)->getAMPlacement(frame);
  const auto worldToTable = otam::inverse(table);

  if (snap) {
    double distance = std::numeric_limits<double>::infinity();
    QVector3D normal;
    for (int col = 0; col < xsheet->getColumnCount(); ++col) {
      auto *column = xsheet->getColumn(col);
      if (!column || !column->isCamstandVisible() || column->getOpacity() == 0)
        continue;
      const auto cell = xsheet->getCell(frame, col);
      if (cell.isEmpty() || !isNativeModelLevel(cell.m_level->getType()))
        continue;
      try {
        const auto mesh = prepareNativeModelGeometry(xsheet, col, frame, false);
        if (!mesh) continue;
        for (const auto &triangle : mesh->triangles)
          updateClosestModelHit(origin, ray, triangle, distance, normal);
      } catch (const std::runtime_error &) {
        // Broken or missing models cannot supply a snap surface.
      }
    }
    if (std::isfinite(distance)) {
      const auto base  = toVector(otam::transform(table, {0, 0, 0}));
      double clearance = 0;
      for (int axis = 0; axis < 3; ++axis) {
        otam::Vec3 edge{};
        edge[axis]           = halfExtent;
        const auto direction = toVector(otam::transform(table, edge)) - base;
        clearance += std::abs(QVector3D::dotProduct(normal, direction));
      }
      const QVector3D center =
          origin + float(distance) * ray + float(clearance) * normal;
      return otam::transform(worldToTable,
                             {center.x(), center.y(), center.z()});
    }
  }

  const auto localEye   = otam::transform(worldToTable, eye);
  const auto localFocus = otam::transform(worldToTable, focus);
  const double dz       = localFocus[2] - localEye[2];
  const double t        = std::abs(dz) > 1e-8 ? -localEye[2] / dz : 0;
  if (t <= 0.001) return localFocus;
  return {localEye[0] + t * (localFocus[0] - localEye[0]),
          localEye[1] + t * (localFocus[1] - localEye[1]), 0};
}

static otam::Color litPaletteColor(const otam::Color &color,
                                   const otam::Vec3 &normal,
                                   const otam::Vec3 &position,
                                   const otglb::LightingRig &rig) {
  std::array<double, 3> energy{rig.ambient, rig.ambient, rig.ambient};
  for (const auto &light : rig.lights) {
    const double diffuse = light.diffuse(normal, position);
    for (int k = 0; k < 3; ++k)
      energy[k] += diffuse * otglb::srgbToLinear(light.color[k]);
  }
  otam::Color result;
  for (int k = 0; k < 3; ++k)
    result[k] = otglb::linearToSrgb(
        float(otglb::srgbToLinear(color[k]) * energy[k] * rig.master));
  return result;
}

// Render-tree snapshots own world-space preview geometry and the camera state.
// Neither downstream FX nor worker threads retain mutable A:M
// documents, GLB assets or columns.
TAMCageRenderData::TAMCageRenderData(const std::string &source, double frame,
                                     double strength)
    : m_cage(otam::readCage(source)), m_frame(frame), m_strength(strength) {
  QCryptographicHash hash(QCryptographicHash::Sha256);
  hash.addData(source.data(), int(source.size()));
  hash.addData(reinterpret_cast<const char *>(&m_frame), sizeof(m_frame));
  hash.addData(reinterpret_cast<const char *>(&m_strength), sizeof(m_strength));
  m_alias = "amCage[" + hash.result().toHex().toStdString() + "]";
}

TAMCageRenderData::~TAMCageRenderData() = default;

bool TAMCageRenderData::operator==(const TRasterFxRenderData &other) const {
  const auto *cage = dynamic_cast<const TAMCageRenderData *>(&other);
  return cage && cage->m_alias == m_alias;
}

bool TAMCageRenderData::apply(const otam::Model &rest,
                              otam::Model &pose) const {
  return otam::applyCage(m_cage, rest, pose, m_frame, m_strength);
}

class NativeModelRenderFx final : public TRasterFx, public T3DRenderSource {
  FX_DECLARATION(NativeModelRenderFx)
  std::shared_ptr<const otam::Mesh> m_geometry;
  AMModelCamera m_camera;
  bool m_project         = false;
  TPixel32 m_colorFilter = TPixel32::Black;
  std::string m_key;
  mutable std::mutex m_normalMutex;
  mutable otam::NormalSmoothingStack m_normalSettings;
  mutable std::shared_ptr<const otam::Mesh> m_smoothed, m_smoothedSource;

  std::shared_ptr<const otam::Mesh> smoothedGeometry(
      const std::shared_ptr<const otam::Mesh> &source,
      const otam::NormalSmoothingStack &options, const int *canceled) const {
    std::lock_guard<std::mutex> lock(m_normalMutex);
    if (canceled && *canceled) return {};
    if (m_smoothedSource == source && m_normalSettings == options)
      return m_smoothed;
    auto result = std::make_shared<otam::Mesh>(*source);
    for (const auto &option : options)
      if (!otam::smoothNormals(*result, option, canceled)) return {};
    m_normalSettings = options;
    m_smoothedSource = source;
    m_smoothed       = result;
    return result;
  }

  struct CageSource {
    AMLevelCagePose pose;
    otam::Matrix placement;
    mutable std::mutex mutex;
    mutable std::string lastKey;
    mutable std::shared_ptr<const otam::Mesh> lastMesh;
  };
  std::shared_ptr<CageSource> m_cageSource;

  std::shared_ptr<const otam::Mesh> cageGeometry(
      const TRenderSettings *settings, const int *canceled) const {
    if (!settings || !m_cageSource || settings->m_data.empty())
      return m_geometry;
    std::vector<const TAMCageRenderData *> cages;
    std::string key;
    for (auto it = settings->m_data.rbegin(); it != settings->m_data.rend();
         ++it)
      if (auto *cage =
              dynamic_cast<const TAMCageRenderData *>(it->getPointer())) {
        cages.push_back(cage);
        key += cage->toString();
      }
    if (cages.empty()) return m_geometry;

    auto &source = *m_cageSource;
    std::lock_guard<std::mutex> lock(source.mutex);
    if (canceled && *canceled) return {};
    if (source.lastKey == key) return source.lastMesh;
    const auto &input = source.pose;
    auto posed        = *input.actionPose;
    bool moved        = false;
    for (const auto *cage : cages) {
      if (canceled && *canceled) return {};
      moved |= cage->apply(*input.rest, posed);
    }
    auto result = m_geometry;
    if (moved) {
      if (input.animation)
        otam::applyBoneAnimation(posed, *input.animation, input.boneFrame);
      otam::applyPlasticBones(*input.rest, posed, input.plasticBones);
      auto mesh     = otam::previewMesh(posed, {});
      mesh.warnings = m_geometry->warnings;
      for (auto &triangle : mesh.triangles) {
        const auto color = input.paletteColors.find(triangle.colorGroup);
        if (color != input.paletteColors.end()) triangle.color = color->second;
      }
      result = std::make_shared<otam::Mesh>(
          otam::transformed(mesh, source.placement));
    }
    if (canceled && *canceled) return {};
    source.lastMesh = result;
    source.lastKey  = std::move(key);
    return result;
  }

public:
  NativeModelRenderFx() { enableComputeInFloat(true); }
  void setGeometry(std::shared_ptr<const otam::Mesh> geometry,
                   bool project         = false,
                   AMModelCamera camera = AMModelCamera()) {
    m_geometry = std::move(geometry);
    m_smoothed.reset();
    m_project  = project;
    m_camera   = camera;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (m_geometry)
      for (const auto &triangle : m_geometry->triangles) {
        for (const auto &vertex : triangle.vertices)
          hash.addData(reinterpret_cast<const char *>(vertex.data()),
                       int(3 * sizeof(double)));
        hash.addData(reinterpret_cast<const char *>(triangle.color.data()),
                     int(3 * sizeof(float)));
        hash.addData(triangle.hasNormals ? "smooth" : "flat");
        if (triangle.hasNormals)
          for (const auto &n : triangle.normals)
            hash.addData(reinterpret_cast<const char *>(n.data()),
                         int(3 * sizeof(double)));
      }
    if (m_geometry) {
      for (const auto &groups : m_geometry->patchGroups) {
        hash.addData("patch:");
        for (const auto &name : groups) {
          hash.addData(QByteArray::number(qulonglong(name.size())));
          hash.addData(":");
          hash.addData(name.data(), int(name.size()));
        }
      }
      hash.addData(m_geometry->amMaterials ? "AM" : "GLB");
      for (const auto &[a, b] : m_geometry->adjoiningPatches)
        hash.addData(QByteArray::number(qulonglong(a)) + "," +
                     QByteArray::number(qulonglong(b)) + ";");
      for (const auto &t : m_geometry->triangles)
        hash.addData(QByteArray::number(qulonglong(t.patch)) + ":");
      hash.addData("splines:");
      for (const auto &group : m_geometry->splineGroups) {
        hash.addData(QByteArray::number(qulonglong(group.size())) + ":");
        hash.addData(group.data(), int(group.size()));
      }
      for (const auto &spline : m_geometry->splines) {
        hash.addData(spline.closed ? "closed" : "open");
        for (const auto &curve : spline.curves) {
          hash.addData(curve.patchEdge ? "patchEdge" : "freeEdge");
          for (const auto &point : curve.points)
            hash.addData(reinterpret_cast<const char *>(point.data()),
                         int(3 * sizeof(double)));
          for (const auto group : curve.groups)
            hash.addData(QByteArray::number(qulonglong(group)) + ":");
        }
      }
    }
    hash.addData(reinterpret_cast<const char *>(m_camera.pose.data()),
                 int(16 * sizeof(double)));
    hash.addData(reinterpret_cast<const char *>(m_camera.imageToStage.data()),
                 int(16 * sizeof(double)));
    hash.addData(m_project ? "world" : "projected");
    m_key = hash.result().toHex().toStdString();
  }
  void setCageSource(AMLevelCagePose pose, const otam::Matrix &placement) {
    if (!pose.rest) return;
    m_cageSource            = std::make_shared<CageSource>();
    m_cageSource->pose      = std::move(pose);
    m_cageSource->placement = placement;

    // Two visually identical base poses may have different rest bounds,
    // skinning, or bone assignments. Include these in the render alias.
    const auto &input = m_cageSource->pose;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto add = [&](const auto &value) {
      hash.addData(reinterpret_cast<const char *>(&value), sizeof(value));
    };
    add(placement);
    add(input.rest->points.size());
    for (const auto &point : input.rest->points) {
      add(point.head);
      add(point.position);
    }
    for (const auto &point : input.actionPose->points) add(point.position);
    for (int bone : input.rest->boneByHead) add(bone);
    for (const auto &bone : input.rest->bones) {
      add(bone.path.size());
      hash.addData(bone.path.data(), int(bone.path.size()));
      add(bone.parent);
      add(bone.inverseBind);
    }
    if (input.animation)
      for (const auto &bone : otam::poseBones(
               *input.rest, input.animation.get(), input.boneFrame)) {
        add(bone.frame);
        add(bone.animated);
      }
    for (const auto &[group, color] : input.paletteColors) {
      add(group);
      add(color);
    }
    for (const auto &bone : input.plasticBones) {
      add(bone.restStart);
      add(bone.restEnd);
      add(bone.posedStart);
      add(bone.posedEnd);
      add(bone.spatial);
      add(bone.restSide);
      add(bone.posedSide);
      add(bone.posedAxis);
    }
    m_key += hash.result().toHex().toStdString();
  }
  void setColorFilter(const TPixel32 &color) { m_colorFilter = color; }
  TFx *clone(bool recursive = true) const override {
    auto *fx       = static_cast<NativeModelRenderFx *>(TFx::clone(recursive));
    fx->m_geometry = m_geometry;
    fx->m_camera   = m_camera;
    fx->m_cageSource  = m_cageSource;
    fx->m_project  = m_project;
    fx->m_colorFilter = m_colorFilter;
    fx->m_key         = m_key;
    return fx;
  }
  std::string getPluginId() const override { return {}; }
  std::string getAlias(double, const TRenderSettings &info) const override {
    return "amModelSnapshot[" + m_key + "," + std::to_string(m_colorFilter.r) +
           "," + std::to_string(m_colorFilter.g) + "," +
           std::to_string(m_colorFilter.b) + "," +
           std::to_string(m_colorFilter.m) + "," + info.toString() + "]";
  }
  std::shared_ptr<const otglb::RenderScene> get3DRenderScene(
      double, const int *canceled, const otglb::LightingRig *lighting = nullptr,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *settings                      = nullptr,
      const otam::MaterialBindings *materials              = nullptr,
      const otam::InkBindings *ink                         = nullptr,
      const otam::NormalSmoothingStack *smoothing = nullptr) const override {
    auto geometry = cageGeometry(settings, canceled);
    if (!geometry || (canceled && *canceled)) return {};
    const otglb::LightingRig dark;
    otglb::LightingRig noDefault;
    if (settings && settings->m_disableDefaultLighting && lighting) {
      noDefault                         = *lighting;
      noDefault.preserveDefaultLighting = false;
      lighting                          = &noDefault;
    }
    if (settings && settings->m_flatRender)
      lighting = nullptr;
    else if (settings && settings->m_disableDefaultLighting && !lighting)
      lighting = &dark;
    if (materials && !materials->empty() && !geometry->amMaterials)
      throw std::runtime_error(
          "A:M Material requires a native A:M model source");
    // Resolve assignments against all patch memberships, including groups with
    // no diffuse color. Downstream assignments win over upstream assignments.
    struct ResolvedSurface {
      otam::Surface surface;
      bool paletteColor;
    };
    std::vector<ResolvedSurface> surfaces;
    std::map<std::pair<std::size_t, std::size_t>, std::size_t> combinations;
    std::map<std::size_t, std::size_t> assigned;
    if (materials)
      for (std::size_t i = 0; i < materials->size(); ++i) {
        const auto &binding = (*materials)[i];
        if (!binding.previewUnsupported &&
            !binding.material.unsupported.empty())
          throw std::runtime_error("A:M Material cannot render " +
                                   binding.material.unsupported.front() +
                                   "; use Supported Attributes Preview to "
                                   "inspect the supported subset");
        const std::size_t base = surfaces.size();
        surfaces.push_back({binding.material.surface, binding.paletteColor});
        bool matched = false;
        std::set<std::size_t> visited;
        for (const auto &triangle : geometry->triangles) {
          const auto p = triangle.patch;
          if (!visited.insert(p).second) continue;
          const auto &groups = geometry->patchGroups;
          if (binding.group.empty() ||
              (p < groups.size() &&
               std::find(groups[p].begin(), groups[p].end(), binding.group) !=
                   groups[p].end())) {
            auto previous        = assigned.find(p);
            std::size_t selected = base;
            if (binding.specularOnly && previous != assigned.end()) {
              const auto key = std::make_pair(i, previous->second);
              auto merged    = combinations.find(key);
              if (merged == combinations.end()) {
                auto combined = surfaces[previous->second];
                combined.surface.specularSize =
                    binding.material.surface.specularSize;
                combined.surface.specularIntensity =
                    binding.material.surface.specularIntensity;
                combined.surface.specularColor =
                    binding.material.surface.specularColor;
                const auto index = surfaces.size();
                surfaces.push_back(combined);
                merged = combinations.emplace(key, index).first;
              }
              selected = merged->second;
            }
            assigned[p] = selected;
            matched     = true;
          }
        }
        if (!matched && !geometry->triangles.empty())
          throw std::runtime_error(
              "A:M Material group has no renderable patches: " + binding.group);
      }
    const bool smoothLighting = smoothing && !smoothing->empty() &&
                                !geometry->triangles.empty() &&
                                !(settings && settings->m_flatRender);
    if (smoothLighting) {
      geometry = smoothedGeometry(geometry, *smoothing, canceled);
      if (!geometry) return {};
    }
    otam::Mesh mesh = *geometry;
    auto cameraPose = m_camera.pose;
    if (transforms)
      for (const auto &t : *transforms) {
        if (canceled && *canceled) return {};
        const auto matrix = modelTransform(t);
        if (t.camera) {
          if (!m_project)
            throw std::runtime_error(
                "Camera transforms require unprojected model geometry");
          // Camera-local deltas compose in source-to-output order. Their
          // inverse is applied to geometry, never a negated model transform.
          cameraPose = otam::multiply(cameraPose, matrix);
        } else {
          mesh = otam::transformed(mesh, matrix);
        }
      }
    const bool hasEnvironment = lighting && !lighting->environments.empty();
    if (lighting) {
      if (!std::isfinite(lighting->ambient) || lighting->ambient < 0 ||
          !std::isfinite(lighting->master) || lighting->master < 0)
        throw std::runtime_error("Invalid A:M preview lighting");
      for (const auto &light : lighting->lights) light.validate();
      for (auto &triangle : mesh.triangles) {
        if (smoothLighting || assigned.count(triangle.patch) ||
            hasEnvironment || lighting->preserveDefaultLighting)
          continue;
        if (canceled && *canceled) return {};
        // Preserve the native preview's world-space light directions. Camera
        // motion changes the eye/facing, not the authored lighting rig.
        const auto &a = triangle.vertices[0];
        const auto &b = triangle.vertices[1];
        const auto &c = triangle.vertices[2];
        otam::Vec3 n{
            {(b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]),
             (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]),
             (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])}};
        const double length =
            std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (length < 1e-15) continue;
        const otam::Vec3 center{{(a[0] + b[0] + c[0]) / 3,
                                 (a[1] + b[1] + c[1]) / 3,
                                 (a[2] + b[2] + c[2]) / 3}};
        const otam::Vec3 view{{cameraPose[3] - center[0],
                               cameraPose[7] - center[1],
                               cameraPose[11] - center[2]}};
        const double facing = n[0] * view[0] + n[1] * view[1] + n[2] * view[2];
        for (double &v : n) v /= facing < 0 ? -length : length;
        triangle.color = litPaletteColor(triangle.color, n, center, *lighting);
      }
    }
    std::vector<otam::InkTriangle> strokes;
    if (ink && !mesh.splines.empty()) {
      const auto view =
          m_project ? otam::inverse(cameraPose) : otam::identity();
      strokes = otam::strokeSplines(mesh, *ink, m_project ? &view : nullptr,
                                    m_camera.imageToStage, canceled);
    }
    if (m_project)
      mesh = otam::projected(mesh, cameraPose, m_camera.imageToStage);
    auto scene        = std::make_shared<otglb::RenderScene>();
    scene->warnings   = mesh.warnings;
    scene->linearSampling = hasEnvironment;
    std::set<std::string> groups;
    for (const auto &patch : mesh.patchGroups)
      groups.insert(patch.begin(), patch.end());
    groups.erase("");
    scene->materialGroups.assign(groups.begin(), groups.end());
    groups = {geometry->splineGroups.begin(), geometry->splineGroups.end()};
    groups.erase("");
    scene->splineGroups.assign(groups.begin(), groups.end());
    scene->colorScale = {{m_colorFilter.r / 255.f, m_colorFilter.g / 255.f,
                          m_colorFilter.b / 255.f, m_colorFilter.m / 255.f}};
    scene->triangles.reserve(mesh.triangles.size() + strokes.size());
    const auto appendTriangle = [&](const std::array<otam::Vec3, 3> &points,
                                    const otam::Color &color) {
      otglb::RenderTriangle triangle{};
      triangle.color = color;
      triangle.edges = {{true, true, true}};
      for (int i = 0; i < 3; ++i) {
        const auto &v        = points[i];
        triangle.vertices[i] = {v[0], v[1], v[2]};
        if (scene->triangles.empty() && i == 0)
          scene->bounds = {{v[0], v[1], v[0], v[1]}};
        scene->bounds[0] = std::min(scene->bounds[0], v[0]);
        scene->bounds[1] = std::min(scene->bounds[1], v[1]);
        scene->bounds[2] = std::max(scene->bounds[2], v[0]);
        scene->bounds[3] = std::max(scene->bounds[3], v[1]);
      }
      scene->triangles.push_back(triangle);
    };
    for (const auto &t : mesh.triangles) {
      if (canceled && *canceled) return {};
      appendTriangle(t.vertices, t.color);
    }
    if (!assigned.empty() || smoothLighting || hasEnvironment) {
      struct MaterialSample {
        std::array<otam::Vec3, 3> positions, normals;
        bool hasNormals = false;
        otam::Color color;
        int binding = -1;
      };
      std::vector<MaterialSample> samples;
      samples.reserve(mesh.triangles.size());
      const auto imageInverse = otam::inverse(m_camera.imageToStage);
      for (const auto &triangle : mesh.triangles) {
        MaterialSample sample;
        sample.positions  = triangle.vertices;
        sample.normals    = triangle.normals;
        sample.hasNormals = triangle.hasNormals;
        sample.color      = triangle.color;
        auto found        = assigned.find(triangle.patch);
        if (found != assigned.end()) sample.binding = int(found->second);
        if (m_project)
          for (auto &p : sample.positions) {
            p                     = otam::transform(imageInverse, p);
            const double distance = 1000.0 / p[2];
            p                     = otam::transform(cameraPose,
                                                    {{p[0] * distance / 1000.0,
                                                      p[1] * distance / 1000.0, -distance}});
          }
        samples.push_back(sample);
      }
      const auto bindings = materials ? *materials : otam::MaterialBindings();
      const bool perspective = m_project;
      const bool useRig      = lighting && !lighting->preserveDefaultLighting;
      const auto rig         = lighting ? *lighting : otglb::LightingRig();
      const otam::Vec3 eye{{cameraPose[3], cameraPose[7], cameraPose[11]}};
      const auto triangles = scene->triangles;
      scene->sampleShader  = [samples = std::move(samples), surfaces,
                             perspective, useRig, rig, eye, smoothLighting,
                             triangles](std::size_t index,
                                        const std::array<double, 3> &bary) {
        const auto &sample = samples.at(index);
        if (sample.binding < 0 && !smoothLighting && rig.environments.empty())
          return otglb::ColorPixel{sample.color[0], sample.color[1],
                                   sample.color[2], 1};
        auto weights        = bary;
        if (perspective) {
          double sum = 0;
          for (int j = 0; j < 3; ++j) {
            weights[j] *= triangles[index].vertices[j].depth;
            sum += weights[j];
          }
          for (double &w : weights) w /= sum;
        }
        otam::Vec3 position{}, a{}, b{}, view{}, normal{};
        for (int k = 0; k < 3; ++k) {
          for (int j = 0; j < 3; ++j)
            position[k] += weights[j] * sample.positions[j][k];
          a[k]    = sample.positions[1][k] - sample.positions[0][k];
          b[k]    = sample.positions[2][k] - sample.positions[0][k];
          view[k] = eye[k] - position[k];
        }
        const auto dot = [](const otam::Vec3 &a, const otam::Vec3 &b) {
          return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        };
        normal = {{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                   a[0] * b[1] - a[1] * b[0]}};
        if (sample.hasNormals) {
          otam::Vec3 smooth{};
          for (int k = 0; k < 3; ++k)
            for (int j = 0; j < 3; ++j)
              smooth[k] += weights[j] * sample.normals[j][k];
          if (dot(smooth, smooth) > 1e-20) normal = smooth;
        }
        const double nl = std::sqrt(dot(normal, normal));
        const double vl = std::sqrt(dot(view, view));
        if (nl < 1e-15 || vl < 1e-15) return otglb::ColorPixel{};
        const double facing = dot(normal, view) < 0 ? -1 : 1;
        for (int k = 0; k < 3; ++k) {
          normal[k] *= facing / nl;
          view[k] /= vl;
        }
        // The neutral headlight makes smoothing visible without explicit
        // lighting. Reflections preserve the source's local shading.
        const double headlight = 0.2 + 0.8 * std::max(0.0, dot(normal, view));
        if (sample.binding < 0 && rig.environments.empty()) {
          auto color = sample.color;
          if (useRig) {
            color = litPaletteColor(color, normal, position, rig);
          } else {
            for (float &channel : color)
              channel = otglb::linearToSrgb(
                   float(otglb::srgbToLinear(channel) * headlight));
          }
          return otglb::ColorPixel{color[0], color[1], color[2], 1};
        }
        const ResolvedSurface fallback{otam::Surface(), true};
        const auto &binding =
            sample.binding < 0 ? fallback : surfaces.at(sample.binding);
        const auto &surface = binding.surface;
        std::vector<otam::SurfaceLight> lights;
        if (!useRig) lights.push_back({view, {{1, 1, 1}}, 1});
        for (const auto &light : rig.lights) {
          otam::SurfaceLight l;
          l.direction = light.direction;
          if (light.point)
            for (int k = 0; k < 3; ++k)
              l.direction[k] = light.point->position[k] - position[k];
          const double length = std::sqrt(dot(l.direction, l.direction));
          if (length < 1e-15) continue;
          for (int k = 0; k < 3; ++k) {
            l.direction[k] /= length;
            l.color[k] = otglb::srgbToLinear(light.color[k]);
          }
          l.intensity = light.diffuse(l.direction, position);
          lights.push_back(l);
        }
        const auto diffuse = !binding.paletteColor && surface.diffuse
                                  ? *surface.diffuse
                                  : sample.color;
        auto color = otam::shadeSurface(surface, diffuse, normal, view, lights,
                                        rig.ambient, rig.master);
        if (sample.binding < 0 && !useRig) {
          const double energy = smoothLighting ? headlight : 1.0;
          for (int k = 0; k < 3; ++k)
            color[k] = float(otglb::srgbToLinear(diffuse[k]) * energy);
        }
        otglb::EnvironmentColor albedo;
        for (int k = 0; k < 3; ++k)
          albedo[k] = float(otglb::srgbToLinear(diffuse[k]) *
                            (1 - surface.ambiance * surface.ambianceBlend));
        color = otglb::shadeEnvironment(
            color, normal, view, rig.environments, surface.reflectivity,
            surface.reflectiveBlend, surface.reflectivityFalloff, albedo,
            surface.diffuseFalloff);
        const float alpha  = float(1 - surface.transparency);
        return otglb::ColorPixel{otglb::linearToSrgbExtended(color[0]) * alpha,
                                 otglb::linearToSrgbExtended(color[1]) * alpha,
                                 otglb::linearToSrgbExtended(color[2]) * alpha,
                                 alpha};
      };
      for (const auto &binding : bindings)
        for (const auto &attribute : binding.material.deferred)
          if (rig.environments.empty())
            scene->warnings.push_back("Deferred A:M " + attribute);
      for (const auto &binding : bindings)
        if (hasEnvironment && binding.material.surface.reflectivityFalloff > 0)
          scene->warnings.push_back(
              "Finite A:M reflection distance falloff excludes the infinite "
              "environment; use Override Material to bypass it");
      for (const auto &binding : bindings)
        for (const auto &attribute : binding.material.unsupported)
          scene->warnings.push_back("Supported Attributes Preview omits " +
                                    attribute);
      if (!assigned.empty())
        scene->warnings.push_back(
            "A:M specular highlights use a Blinn preview; Hash renderer parity "
            "is unvalidated.");
    }
    if (!strokes.empty()) {
      const auto first = scene->triangles.size();
      std::vector<std::array<std::array<float, 4>, 3>> colors;
      colors.reserve(strokes.size());
      for (const auto &stroke : strokes) {
        if (canceled && *canceled) return {};
        appendTriangle(stroke.vertices, {});
        colors.push_back(stroke.colors);
      }
      auto surface = std::move(scene->sampleShader);
      std::vector<otam::Color> baseColors;
      if (!surface) {
        baseColors.reserve(first);
        for (std::size_t i = 0; i < first; ++i)
          baseColors.push_back(scene->triangles[i].color);
      }
      scene->sampleShader = [first, surface = std::move(surface),
                             baseColors = std::move(baseColors),
                             colors     = std::move(colors)](
                                std::size_t index,
                                const std::array<double, 3> &bary) {
        if (index < first) {
          if (surface) return surface(index, bary);
          const auto &c = baseColors[index];
          return otglb::ColorPixel{c[0], c[1], c[2], 1};
        }
        const auto &c          = colors[index - first];
        const auto interpolate = [&](int channel) {
          return float(bary[0] * c[0][channel] + bary[1] * c[1][channel] +
                       bary[2] * c[2][channel]);
        };
        return otglb::ColorPixel{interpolate(0), interpolate(1), interpolate(2),
                                 std::clamp(interpolate(3), 0.f, 1.f)};
      };
    }
    return scene;
  }
  bool canHandle(const TRenderSettings &, double) override { return true; }
  bool toBeComputedInLinearColorSpace(bool, bool tileIsLinear) const override {
    return tileIsLinear;
  }
  bool doGetBBox(double frame, TRectD &bbox,
                 const TRenderSettings &info) override {
    bbox = TRectD();
    const auto scene =
        get3DRenderGeometry(frame, info.m_isCanceled, nullptr, &info);
    if (!scene || scene->triangles.empty()) return false;
    const auto &b = scene->bounds;
    bbox          = TRectD(b[0], b[1], b[2], b[3]);
    return !bbox.isEmpty();
  }
  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &info) override {
    auto raster = tile.getRaster();
    raster->clear();
    const auto scene =
        get3DRenderScene(frame, info.m_isCanceled, nullptr, nullptr, &info);
    if (!scene || !raster->getLx() || !raster->getLy()) return;
    TRaster32P r32 = raster;
    TRaster64P r64 = raster;
    TRasterFP rf   = raster;
    if (!r32 && !r64 && !rf)
      throw TException("3D model: unsupported output raster");
    const auto &a = info.m_affine;
    otglb::RenderTile request;
    request.width  = raster->getLx();
    request.x      = tile.m_pos.x;
    request.affine = {{a.a11, a.a12, a.a13, a.a21, a.a22, a.a23}};
    const int band =
        std::max(1, std::min(128, 2 * 1024 * 1024 / request.width));
    for (int y0 = 0; y0 < raster->getLy(); y0 += band) {
      request.y         = tile.m_pos.y + y0;
      request.height    = std::min(band, raster->getLy() - y0);
      const auto pixels = otglb::renderTile(*scene, request, info.m_isCanceled);
      if (pixels.empty()) {
        raster->clear();
        return;
      }
      raster->lock();
      for (int y = 0; y < request.height; ++y)
        for (int x = 0; x < request.width; ++x) {
          const auto p = otglb::outputPixel(
              pixels[std::size_t(y) * request.width + x], raster->isLinear());
          if (r32)
            r32->pixels(y + y0)[x] =
                TPixel32(otglb::displayChannel<UCHAR>(p.r, 255),
                         otglb::displayChannel<UCHAR>(p.g, 255),
                         otglb::displayChannel<UCHAR>(p.b, 255),
                         otglb::displayChannel<UCHAR>(p.alpha, 255));
          else if (r64)
            r64->pixels(y + y0)[x] =
                TPixel64(otglb::displayChannel<USHORT>(p.r, 65535),
                         otglb::displayChannel<USHORT>(p.g, 65535),
                         otglb::displayChannel<USHORT>(p.b, 65535),
                         otglb::displayChannel<USHORT>(p.alpha, 65535));
          else
            rf->pixels(y + y0)[x] = TPixelF(p.r, p.g, p.b, p.alpha);
        }
      raster->unlock();
    }
  }
};
FX_IDENTIFIER_IS_HIDDEN(NativeModelRenderFx, "amModelSnapshot")

TRasterFx *makeAMLevelRenderFx(std::shared_ptr<const otam::Mesh> geometry) {
  auto *fx = new NativeModelRenderFx;
  fx->setGeometry(std::move(geometry));
  return fx;
}
static TRasterFx *makeModelSnapshot(TXsheet *xsheet, int column, double frame,
                                    bool previewCamera, bool allowGlb) {
  AMLevelCagePose cagePose;
  auto geometry =
      prepareModelGeometryImpl(xsheet, column, frame, false, previewCamera,
                               allowGlb, true, true, &cagePose);
  if (!geometry) return nullptr;
  auto fx = std::make_unique<NativeModelRenderFx>();
  fx->setGeometry(std::move(geometry), true,
                  amModelCamera(xsheet, frame, previewCamera));
  if (cagePose.rest)
    fx->setCageSource(std::move(cagePose),
                      amModelPlacement(xsheet, column, frame));
  return fx.release();
}
TRasterFx *makeAMLevelRenderFx(TXsheet *xsheet, int column, double frame,
                               bool previewCamera) {
  return makeModelSnapshot(xsheet, column, frame, previewCamera, false);
}
TRasterFx *makeNativeModelRenderFx(TXsheet *xsheet, int column, double frame,
                                   bool previewCamera) {
  return makeModelSnapshot(xsheet, column, frame, previewCamera, true);
}
bool setAMLevelColorFilter(TRasterFx *fx, const TPixel32 &color) {
  auto *model = dynamic_cast<NativeModelRenderFx *>(fx);
  if (!model) return false;
  model->setColorFilter(color);
  return true;
}
static TXshLevel *firstColumnLevel(TFx *fx) {
  auto *levelFx = dynamic_cast<TLevelColumnFx *>(fx);
  auto *column  = levelFx ? levelFx->getColumn() : nullptr;
  int first = 0, last = -1;
  if (!column || column->getRange(first, last) <= 0) return nullptr;
  return column->getCell(first).m_level.getPointer();
}
std::shared_ptr<const otam::Model> amCageSourceModel(TFx *fx, int row) {
  while (fx && fx->getFxType() == "STD_amDistortionCageFx")
    fx = fx->getInputPort(0)->getFx();
  auto *levelFx = dynamic_cast<TLevelColumnFx *>(fx);
  auto *column  = levelFx ? levelFx->getColumn() : nullptr;
  if (!column) return {};
  const auto cell = column->getCell(row);
  auto *level =
      cell.isEmpty() ? firstColumnLevel(fx) : cell.m_level.getPointer();
  const auto *am = level ? level->getAMLevel() : nullptr;
  return am ? am->editableModel() : nullptr;
}

bool isAMModelColumn(TFx *fx) {
  auto *level = firstColumnLevel(fx);
  return level && level->getAMLevel();
}
bool isNativeModelColumn(TFx *fx) {
  auto *level = firstColumnLevel(fx);
  return level && isNativeModelLevel(level->getType());
}
std::shared_ptr<const otglb::RenderScene> prepareNativeModelColumnRenderScene(
    TLevelColumnFx *column, double frame, const int *canceled,
    const otglb::LightingRig *lighting,
    const std::vector<otglb::ModelTransform> *transforms,
    const TRenderSettings *settings, const otam::MaterialBindings *materials,
    const otam::InkBindings *ink, const otam::NormalSmoothingStack *smoothing) {
  if (!column || !column->getColumn() || (canceled && *canceled)) return {};
  auto *source        = column->getColumn();
  TRasterFxP snapshot = makeNativeModelRenderFx(
      source->getXsheet(), source->getIndex(), frame, false);
  const auto *model =
      dynamic_cast<const T3DRenderSource *>(snapshot.getPointer());
  return model ? model->get3DRenderScene(frame, canceled, lighting, transforms,
                                         settings, materials, ink, smoothing)
               : nullptr;
}

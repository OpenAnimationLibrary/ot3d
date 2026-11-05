#include "toonz/amlevelrender.h"
#include "toonz/t3drendersource.h"
#include "toonz/txshcell.h"
#include "toonz/tcolumnfx.h"
#include "toonz/txshlevelcolumn.h"
#include "toonz/txshamlevel.h"
#include "toonz/txsheet.h"
#include "toonz/tstageobject.h"
#include "toonz/tstageobjecttree.h"
#include "toonz/stage.h"
#include "tfxattributes.h"
#include "ttile.h"
#include "traster.h"
#include "tpixel.h"
#include <QCryptographicHash>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

std::shared_ptr<const otam::Mesh> prepareAMLevelGeometry(TXsheet *xsheet,
                                                         int column,
                                                         double frame,
                                                         bool projectToCamera,
                                                         bool previewCamera) {
  if (!xsheet || column < 0 || !std::isfinite(frame)) return {};
  const auto cell = xsheet->getCell(int(std::floor(frame)), column);
  auto *level     = cell.isEmpty() ? nullptr : cell.m_level->getAMLevel();
  if (!level) return {};
  auto mesh = level->mesh();
  if (!mesh)
    throw std::runtime_error(level->error().empty()
                                 ? "A:M model source is not loaded"
                                 : level->error());
  auto *object = xsheet->getStageObject(TStageObjectId::ColumnId(column));
  // Initial explicit bridge: A:M model units are interpreted as centimeters,
  // Y up, +Z toward the OT camera. Calibration remains experimental.
  const double units   = Stage::inch / 2.54;
  const double noScale = 1.0 - object->getGlobalNoScaleZ() / 1000.0;
  const auto matrix    = otam::multiply(
         object->getAMPlacement(frame),
         otam::scaling(units * noScale, units * noScale, units * noScale));
  auto result = std::make_shared<otam::Mesh>(otam::transformed(*mesh, matrix));
  if (projectToCamera) {
    const auto cameraId =
        previewCamera
            ? xsheet->getStageObjectTree()->getCurrentPreviewCameraId()
            : xsheet->getStageObjectTree()->getCurrentCameraId();
    auto *camera      = xsheet->getStageObject(cameraId);
    const auto affine = camera->getPlacement(frame);
    *result =
        otam::projected(*result, affine.a13, affine.a23, camera->getZ(frame));
  }
  return result;
}

// Render-tree snapshots own world-space preview geometry and the camera state.
// Neither downstream FX nor worker threads retain mutable A:M
// documents/columns.
class AMLevelRenderFx final : public TRasterFx, public T3DRenderSource {
  FX_DECLARATION(AMLevelRenderFx)
  std::shared_ptr<const otam::Mesh> m_geometry;
  otam::Vec3 m_camera{{0, 0, 0}};
  bool m_project         = false;
  TPixel32 m_colorFilter = TPixel32::Black;
  std::string m_key;

public:
  AMLevelRenderFx() { enableComputeInFloat(true); }
  void setGeometry(std::shared_ptr<const otam::Mesh> geometry,
                   bool project = false, otam::Vec3 camera = {{0, 0, 0}}) {
    m_geometry = std::move(geometry);
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
      }
    hash.addData(reinterpret_cast<const char *>(m_camera.data()),
                 int(3 * sizeof(double)));
    hash.addData(m_project ? "world" : "projected");
    m_key = hash.result().toHex().toStdString();
  }
  void setColorFilter(const TPixel32 &color) { m_colorFilter = color; }
  TFx *clone(bool recursive = true) const override {
    auto *fx          = static_cast<AMLevelRenderFx *>(TFx::clone(recursive));
    fx->m_geometry    = m_geometry;
    fx->m_camera      = m_camera;
    fx->m_project     = m_project;
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
      const TRenderSettings * = nullptr) const override {
    if (!m_geometry || (canceled && *canceled)) return {};
    otam::Mesh mesh = *m_geometry;
    if (transforms)
      for (const auto &t : *transforms) {
        if (canceled && *canceled) return {};
        // Additional FX transforms are applied in order, before projection.
        const auto matrix = otam::multiply(
            otam::translation(t.position[0], t.position[1], t.position[2]),
            otam::multiply(
                otam::rotation(t.rotation[0], t.rotation[1], t.rotation[2]),
                otam::scaling(t.scale[0], t.scale[1], t.scale[2])));
        mesh = otam::transformed(mesh, matrix);
      }
    if (lighting) {
      if (!std::isfinite(lighting->ambient) || lighting->ambient < 0 ||
          !std::isfinite(lighting->master) || lighting->master < 0)
        throw std::runtime_error("Invalid A:M preview lighting");
      for (auto &triangle : mesh.triangles) {
        if (canceled && *canceled) return {};
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
        const otam::Vec3 view{{m_camera[0] - (a[0] + b[0] + c[0]) / 3,
                               m_camera[1] - (a[1] + b[1] + c[1]) / 3,
                               1000 + m_camera[2] - (a[2] + b[2] + c[2]) / 3}};
        const double facing = n[0] * view[0] + n[1] * view[1] + n[2] * view[2];
        for (double &v : n) v /= facing < 0 ? -length : length;
        std::array<double, 3> energy{
            {lighting->ambient, lighting->ambient, lighting->ambient}};
        for (const auto &light : lighting->lights) {
          const auto &d  = light.direction;
          const double l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
          if (!(l > 0) || !std::isfinite(l) ||
              !std::isfinite(light.intensity) || light.intensity < 0)
            throw std::runtime_error(
                "Invalid A:M preview light direction/intensity");
          const double diffuse =
              std::max(0.0, (n[0] * d[0] + n[1] * d[1] + n[2] * d[2]) / l) *
              light.intensity;
          for (int k = 0; k < 3; ++k) {
            if (!std::isfinite(light.color[k]))
              throw std::runtime_error("Invalid A:M preview light color");
            energy[k] += diffuse * otglb::srgbToLinear(light.color[k]);
          }
        }
        for (int k = 0; k < 3; ++k)
          triangle.color[k] =
              otglb::linearToSrgb(float(otglb::srgbToLinear(triangle.color[k]) *
                                        energy[k] * lighting->master));
      }
    }
    if (m_project)
      mesh = otam::projected(mesh, m_camera[0], m_camera[1], m_camera[2]);
    auto scene        = std::make_shared<otglb::RenderScene>();
    scene->warnings   = mesh.warnings;
    scene->colorScale = {{m_colorFilter.r / 255.f, m_colorFilter.g / 255.f,
                          m_colorFilter.b / 255.f, m_colorFilter.m / 255.f}};
    scene->triangles.reserve(mesh.triangles.size());
    for (const auto &t : mesh.triangles) {
      if (canceled && *canceled) return {};
      otglb::RenderTriangle triangle;
      triangle.color = t.color;
      triangle.edges = {{true, true, true}};
      for (int i = 0; i < 3; ++i) {
        const auto &v        = t.vertices[i];
        triangle.vertices[i] = {v[0], v[1], v[2]};
        if (scene->triangles.empty() && i == 0)
          scene->bounds = {{v[0], v[1], v[0], v[1]}};
        scene->bounds[0] = std::min(scene->bounds[0], v[0]);
        scene->bounds[1] = std::min(scene->bounds[1], v[1]);
        scene->bounds[2] = std::max(scene->bounds[2], v[0]);
        scene->bounds[3] = std::max(scene->bounds[3], v[1]);
      }
      scene->triangles.push_back(triangle);
    }
    return scene;
  }
  bool canHandle(const TRenderSettings &, double) override { return true; }
  bool toBeComputedInLinearColorSpace(bool, bool) const override {
    return false;
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
      throw TException("A:M model: unsupported output raster");
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
          const auto &p = pixels[std::size_t(y) * request.width + x];
          if (r32)
            r32->pixels(y + y0)[x] =
                TPixel32(UCHAR(p.r * 255 + 0.5f), UCHAR(p.g * 255 + 0.5f),
                         UCHAR(p.b * 255 + 0.5f), UCHAR(p.alpha * 255 + 0.5f));
          else if (r64)
            r64->pixels(y + y0)[x] = TPixel64(
                USHORT(p.r * 65535 + 0.5f), USHORT(p.g * 65535 + 0.5f),
                USHORT(p.b * 65535 + 0.5f), USHORT(p.alpha * 65535 + 0.5f));
          else
            rf->pixels(y + y0)[x] = TPixelF(p.r, p.g, p.b, p.alpha);
        }
      raster->unlock();
    }
  }
};
FX_IDENTIFIER_IS_HIDDEN(AMLevelRenderFx, "amModelSnapshot")

TRasterFx *makeAMLevelRenderFx(std::shared_ptr<const otam::Mesh> geometry) {
  auto *fx = new AMLevelRenderFx;
  fx->setGeometry(std::move(geometry));
  return fx;
}
TRasterFx *makeAMLevelRenderFx(TXsheet *xsheet, int column, double frame,
                               bool previewCamera) {
  auto geometry = prepareAMLevelGeometry(xsheet, column, frame, false);
  if (!geometry) return nullptr;
  const auto id =
      previewCamera ? xsheet->getStageObjectTree()->getCurrentPreviewCameraId()
                    : xsheet->getStageObjectTree()->getCurrentCameraId();
  auto *camera      = xsheet->getStageObject(id);
  const auto affine = camera->getPlacement(frame);
  auto fx           = std::make_unique<AMLevelRenderFx>();
  fx->setGeometry(std::move(geometry), true,
                  {{affine.a13, affine.a23, camera->getZ(frame)}});
  return fx.release();
}
bool setAMLevelColorFilter(TRasterFx *fx, const TPixel32 &color) {
  auto *model = dynamic_cast<AMLevelRenderFx *>(fx);
  if (!model) return false;
  model->setColorFilter(color);
  return true;
}
bool isAMModelColumn(TFx *fx) {
  auto *levelFx = dynamic_cast<TLevelColumnFx *>(fx);
  auto *column  = levelFx ? levelFx->getColumn() : nullptr;
  int first = 0, last = -1;
  return column && column->getRange(first, last) > 0 &&
         !column->getCell(first).isEmpty() &&
         column->getCell(first).m_level->getAMLevel();
}
std::shared_ptr<const otglb::RenderScene> prepareAMColumnRenderScene(
    TLevelColumnFx *column, double frame, const int *canceled,
    const otglb::LightingRig *lighting,
    const std::vector<otglb::ModelTransform> *transforms,
    const TRenderSettings *settings) {
  if (!column || !column->getColumn() || (canceled && *canceled)) return {};
  auto *source = column->getColumn();
  TRasterFxP snapshot =
      makeAMLevelRenderFx(source->getXsheet(), source->getIndex(), frame);
  const auto *model =
      dynamic_cast<const T3DRenderSource *>(snapshot.getPointer());
  return model ? model->get3DRenderScene(frame, canceled, lighting, transforms,
                                         settings)
               : nullptr;
}

#include "toonz/amlevelrender.h"
#include "toonz/txshamlevel.h"
#include "toonz/txsheet.h"
#include "toonz/txshcell.h"
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

class AMLevelRenderFx final : public TRasterFx {
  FX_DECLARATION(AMLevelRenderFx)
  std::shared_ptr<const otam::Mesh> m_geometry;
  std::string m_key;

public:
  AMLevelRenderFx() { enableComputeInFloat(true); }
  void setGeometry(std::shared_ptr<const otam::Mesh> geometry) {
    m_geometry = std::move(geometry);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (m_geometry)
      for (const auto &triangle : m_geometry->triangles) {
        // Hash values, not padding; transformation and camera changes
        // invalidate the same cache even when the source document has not
        // changed.
        for (const auto &vertex : triangle.vertices)
          hash.addData(reinterpret_cast<const char *>(vertex.data()),
                       int(3 * sizeof(double)));
        hash.addData(reinterpret_cast<const char *>(triangle.color.data()),
                     int(3 * sizeof(float)));
      }
    m_key = hash.result().toHex().toStdString();
  }
  TFx *clone(bool recursive = true) const override {
    auto *fx       = static_cast<AMLevelRenderFx *>(TFx::clone(recursive));
    fx->m_geometry = m_geometry;
    fx->m_key      = m_key;
    return fx;
  }
  std::string getPluginId() const override { return {}; }
  std::string getAlias(double, const TRenderSettings &info) const override {
    return "amModelSnapshot[" + m_key + "," + info.toString() + "]";
  }
  bool canHandle(const TRenderSettings &, double) override { return true; }
  bool doGetBBox(double, TRectD &bbox, const TRenderSettings &) override {
    if (!m_geometry || m_geometry->triangles.empty()) return false;
    double x0 = std::numeric_limits<double>::infinity(), y0 = x0;
    double x1 = -x0, y1 = -y0;
    for (const auto &t : m_geometry->triangles)
      for (const auto &p : t.vertices) {
        x0 = std::min(x0, p[0]);
        y0 = std::min(y0, p[1]);
        x1 = std::max(x1, p[0]);
        y1 = std::max(y1, p[1]);
      }
    bbox = TRectD(x0, y0, x1, y1);
    return !bbox.isEmpty();
  }
  void doCompute(TTile &tile, double, const TRenderSettings &info) override {
    auto raster = tile.getRaster();
    raster->clear();
    if (!m_geometry || (info.m_isCanceled && *info.m_isCanceled)) return;
    const auto &a = info.m_affine;
    const otam::Matrix affine{{a.a11, a.a12, 0, a.a13 - tile.m_pos.x, a.a21,
                               a.a22, 0, a.a23 - tile.m_pos.y, 0, 0, 1, 0, 0, 0,
                               0, 1}};
    const auto pixels = otam::rasterize(otam::transformed(*m_geometry, affine),
                                        raster->getLx(), raster->getLy());
    if (info.m_isCanceled && *info.m_isCanceled) return;
    TRaster32P r32 = raster;
    TRaster64P r64 = raster;
    TRasterFP rf   = raster;
    if (!r32 && !r64 && !rf)
      throw TException("A:M model: unsupported output raster");
    raster->lock();
    for (int y = 0; y < raster->getLy(); ++y)
      for (int x = 0; x < raster->getLx(); ++x) {
        const auto &p = pixels[std::size_t(y) * raster->getLx() + x];
        if (r32)
          r32->pixels(y)[x] = TPixel32(UCHAR(p[0] * 255), UCHAR(p[1] * 255),
                                       UCHAR(p[2] * 255), UCHAR(p[3] * 255));
        else if (r64)
          r64->pixels(y)[x] =
              TPixel64(USHORT(p[0] * 65535), USHORT(p[1] * 65535),
                       USHORT(p[2] * 65535), USHORT(p[3] * 65535));
        else
          rf->pixels(y)[x] = TPixelF(p[0], p[1], p[2], p[3]);
      }
    raster->unlock();
  }
};
FX_IDENTIFIER_IS_HIDDEN(AMLevelRenderFx, "amModelSnapshot")

TRasterFx *makeAMLevelRenderFx(std::shared_ptr<const otam::Mesh> geometry) {
  auto *fx = new AMLevelRenderFx;
  fx->setGeometry(std::move(geometry));
  return fx;
}

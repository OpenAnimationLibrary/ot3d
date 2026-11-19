#include "toonz/amlevelrender.h"
#include "toonz/t3drendersource.h"
#include "t3draster.h"
#include "tfxattributes.h"
#include <QCryptographicHash>
#include <cmath>
#include <stdexcept>

namespace {
class SpatialDrawingFx final : public TRasterFx, public T3DRenderSource {
  FX_DECLARATION(SpatialDrawingFx)
  TRasterFxPort m_source;
  otam::Matrix m_camera = otam::identity(), m_film = otam::identity();
  std::string m_placementAlias;

  std::shared_ptr<otglb::RenderScene> prepare(double frame,
                                              const TRenderSettings *settings,
                                              bool pixels,
                                              const int *canceled) const {
    if (!m_source.isConnected() || (canceled && *canceled)) return {};
    auto *input                = static_cast<TRasterFx *>(m_source.getFx());
    TRenderSettings neutral    = settings ? *settings : TRenderSettings();
    neutral.m_affine           = TAffine();
    neutral.m_bpp              = 32;
    neutral.m_linearColorSpace = false;
    TRectD bbox;
    if (!input->doGetBBox(frame, bbox, neutral) || bbox.isEmpty()) return {};
    const double x0 = std::floor(bbox.x0), y0 = std::floor(bbox.y0);
    const double x1 = std::ceil(bbox.x1), y1 = std::ceil(bbox.y1);
    const double width = x1 - x0, height = y1 - y0;
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 ||
        height <= 0 || width > 8192 || height > 8192 ||
        width * height > 16.0 * 1024 * 1024)
      throw std::runtime_error(
          "Spatial drawing requires a finite input of at most 16 megapixels");
    auto result = std::make_shared<otglb::RenderScene>(
        otglb::projectImagePlane3D({{x0, y0, x1, y1}}, m_camera, m_film));
    // Bounds retain their true local coordinates; changing artwork bounds must
    // never recenter or resize an attached drawing.
    if (!pixels || result->empty()) return result;
    const int w = int(width), h = int(height);
    TRaster32P raster(w, h);
    raster->clear();
    TTile tile(raster, TPointD(x0, y0));
    input->compute(tile, frame, neutral);
    if (canceled && *canceled) return {};
    auto texture =
        std::make_shared<std::vector<otglb::ColorPixel>>(std::size_t(w) * h);
    raster->lock();
    for (int y = 0; y < h; ++y) {
      if (canceled && *canceled) {
        raster->unlock();
        return {};
      }
      const auto *row = raster->pixels(y);
      for (int x = 0; x < w; ++x) {
        const auto &p                      = row[x];
        (*texture)[std::size_t(y) * w + x] = {p.r / 255.f, p.g / 255.f,
                                              p.b / 255.f, p.m / 255.f};
      }
    }
    raster->unlock();
    result->texture       = std::move(texture);
    result->textureWidth  = w;
    result->textureHeight = h;
    return result;
  }

public:
  std::string getPluginId() const override { return {}; }
  SpatialDrawingFx() {
    addInputPort("source", m_source);
    enableComputeInFloat(true);
  }
  void initialize(TRasterFx *input, const otam::Matrix &placement,
                  const AMModelCamera &camera) {
    m_source.setFx(input);
    m_camera = otam::multiply(otam::inverse(camera.pose), placement);
    m_film   = camera.imageToStage;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(reinterpret_cast<const char *>(m_camera.data()),
                 sizeof(double) * 16);
    hash.addData(reinterpret_cast<const char *>(m_film.data()),
                 sizeof(double) * 16);
    m_placementAlias = hash.result().toHex().toStdString();
  }
  TFx *clone(bool recursive = true) const override {
    auto *copy     = static_cast<SpatialDrawingFx *>(TFx::clone(recursive));
    copy->m_camera = m_camera;
    copy->m_film   = m_film;
    copy->m_placementAlias = m_placementAlias;
    return copy;
  }
  std::string getAlias(double frame,
                       const TRenderSettings &info) const override {
    return TRasterFx::getAlias(frame, info) +
           "[SpatialDrawing:" + m_placementAlias + "]";
  }
  std::shared_ptr<const otglb::RenderScene> get3DRenderScene(
      double frame, const int *canceled, const otglb::LightingRig * = nullptr,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *settings                      = nullptr,
      const otam::MaterialBindings *materials              = nullptr,
      const otam::InkBindings *ink                         = nullptr,
      const otam::NormalSmoothingStack *smoothing = nullptr) const override {
    if ((transforms && !transforms->empty()) ||
        (materials && !materials->empty()) || (ink && !ink->empty()) ||
        (smoothing && !smoothing->empty()))
      throw std::runtime_error(
          "Apply model FX before attaching the drawing; spatial drawing "
          "supports column orientation only");
    return prepare(frame, settings, true, canceled);
  }
  std::shared_ptr<const otglb::RenderScene> get3DRenderGeometry(
      double frame, const int *canceled,
      const std::vector<otglb::ModelTransform> * = nullptr,
      const TRenderSettings *settings            = nullptr,
      const otam::InkBindings                  * = nullptr) const override {
    return prepare(frame, settings, false, canceled);
  }
  bool doGetBBox(double frame, TRectD &bbox,
                 const TRenderSettings &info) override {
    auto result = prepare(frame, &info, false, nullptr);
    bbox        = result && !result->empty()
                      ? TRectD(result->bounds[0], result->bounds[1], result->bounds[2],
                               result->bounds[3])
                      : TRectD();
    return !bbox.isEmpty();
  }
  bool canHandle(const TRenderSettings &, double) override { return true; }
  bool toBeComputedInLinearColorSpace(bool, bool tileLinear) const override {
    return tileLinear;
  }
  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &info) override {
    tile.getRaster()->clear();
    auto scene = prepare(frame, &info, true, info.m_isCanceled);
    if (scene) ot3d::renderScene(*scene, tile, info);
  }
};
FX_IDENTIFIER_IS_HIDDEN(SpatialDrawingFx, "spatialDrawingFx")
}  // namespace
TRasterFx *makeSpatialDrawingFx(TRasterFx *input, const otam::Matrix &placement,
                                const AMModelCamera &camera) {
  auto *fx = new SpatialDrawingFx;
  fx->initialize(input, placement, camera);
  return fx;
}

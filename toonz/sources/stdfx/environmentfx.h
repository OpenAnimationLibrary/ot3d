#pragma once

#include "stdfx.h"
#include "t3dsource.h"
#include "tfxparam.h"
#include "tnotanimatableparam.h"
#include "tparamset.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>

// Shared typed-geometry, image loading and raster output for environment FX.
// Environment pixels are evaluated only inside an active rendering session.
class EnvironmentFxBase : public TStandardRasterFx, public T3DRenderSource {
  struct Cache {
    std::mutex mutex;
    std::string key;
    std::shared_ptr<const otglb::EnvironmentMap> map;
  };
  std::shared_ptr<Cache> m_cache = std::make_shared<Cache>();
  T3DSourcePort m_source;
  TRasterFxPort m_environment;
  TDoubleParamP m_intensity, m_exposure, m_rotationX, m_rotationY, m_rotationZ;
  TPixelParamP m_tint;
  TIntEnumParamP m_materialMode;
  TDoubleParamP m_reflectivity, m_blend;

  std::shared_ptr<const otglb::EnvironmentMap> environment(
      double frame, const int *canceled,
      const TRenderSettings *settings) const {
    if (!m_environment.isConnected()) return {};
    if (!settings)
      throw std::runtime_error(
          "Environment image evaluation requires render settings");
    TRenderSettings neutral    = *settings;
    neutral.m_affine           = TAffine();
    neutral.m_bpp              = 128;
    neutral.m_linearColorSpace = true;
    neutral.m_isCanceled       = const_cast<int *>(canceled);
    auto *input    = static_cast<TRasterFx *>(m_environment.getFx());
    const auto key = input->getAlias(frame, neutral) + neutral.toString();
    {
      std::lock_guard<std::mutex> lock(m_cache->mutex);
      if (key == m_cache->key && m_cache->map) return m_cache->map;
    }
    TRectD bbox;
    if (!input->doGetBBox(frame, bbox, neutral) || bbox.isEmpty())
      throw std::runtime_error("Environment input is empty");
    const double x0 = std::floor(bbox.x0), y0 = std::floor(bbox.y0);
    const double w = std::ceil(bbox.x1) - x0, h = std::ceil(bbox.y1) - y0;
    if (!(std::isfinite(x0) && std::isfinite(y0) && w > 0 && h > 0 &&
          w <= 8192 && h <= 8192 && w * h <= otglb::EnvironmentMap::MaxPixels))
      throw std::runtime_error(
          "Environment requires a finite image of at most 4 megapixels; resize "
          "or crop the input");
    TRasterFP raster{int(w), int(h)};
    raster->clear();
    raster->setLinear(true);
    TTile tile(raster, TPointD(x0, y0));
    input->compute(tile, frame, neutral);
    if (canceled && *canceled) return {};
    // Premultiplied radiance treats transparent areas as black. Do not divide
    // by alpha: partial coverage must reduce the light from that direction.
    std::vector<otglb::EnvironmentColor> pixels(std::size_t(w) * int(h));
    raster->lock();
    for (int y = 0; y < int(h); ++y)
      for (int x = 0; x < int(w); ++x) {
        const auto p                        = raster->pixels(y)[x];
        pixels[std::size_t(y) * int(w) + x] = {{p.r, p.g, p.b}};
      }
    raster->unlock();
    auto map = std::make_shared<otglb::EnvironmentMap>(int(w), int(h),
                                                       std::move(pixels));
    {
      std::lock_guard<std::mutex> lock(m_cache->mutex);
      m_cache->key = key;
      m_cache->map = map;
    }
    return map;
  }

protected:
  virtual bool suppliesLighting() const { return false; }
  virtual otglb::EnvironmentLayer layer(
      double frame, std::shared_ptr<const otglb::EnvironmentMap> map) const {
    otglb::EnvironmentLayer result;
    result.map = std::move(map);
    result.setRotation(
        {{m_rotationX->getValue(frame), m_rotationY->getValue(frame),
          m_rotationZ->getValue(frame)}});
    const double intensity = m_intensity->getValue(frame) / 100 *
                             std::exp2(m_exposure->getValue(frame));
    const auto tint      = m_tint->getValueD(frame);
    const double color[] = {tint.r, tint.g, tint.b};
    for (int k = 0; k < 3; ++k)
      result.gain[k] =
          float(intensity *
                otglb::srgbToLinear(float(std::clamp(color[k], 0.0, 1.0))));
    result.overrideMaterial = m_materialMode->getValue() == 0;
    result.reflectivity     = m_reflectivity->getValue(frame) / 100;
    result.blend            = m_blend->getValue(frame) / 100;
    return result;
  }

public:
  explicit EnvironmentFxBase(bool reflections = true)
      : m_intensity(100.0)
      , m_exposure(0.0)
      , m_rotationX(0.0)
      , m_rotationY(0.0)
      , m_rotationZ(0.0)
      , m_tint(TPixel32::White)
      , m_materialMode(new TIntEnumParam(0, "Override Material"))
      , m_reflectivity(50.0)
      , m_blend(100.0) {
    addInputPort("Source", m_source);
    addInputPort("Environment", m_environment);
    bindParam(this, "intensity", m_intensity);
    bindParam(this, "exposure", m_exposure);
    bindParam(this, "tint", m_tint);
    bindParam(this, "rotationX", m_rotationX);
    bindParam(this, "rotationY", m_rotationY);
    bindParam(this, "rotationZ", m_rotationZ);
    m_materialMode->addItem(1, "A:M Material");
    if (reflections) {
      bindParam(this, "materialMode", m_materialMode);
      bindParam(this, "reflectivity", m_reflectivity);
      bindParam(this, "reflectiveBlend", m_blend);
    }
    m_intensity->setValueRange(0.0, 1000.0);
    m_exposure->setValueRange(-20.0, 20.0);
    m_reflectivity->setValueRange(0.0, 100.0);
    m_blend->setValueRange(0.0, 100.0);
    for (auto p : {m_rotationX, m_rotationY, m_rotationZ}) {
      p->setMeasureName("angle");
      p->setValueRange(-36000.0, 36000.0);
    }
    enableComputeInFloat(true);
  }
  TFx *clone(bool recursive = true) const override {
    auto *result    = static_cast<EnvironmentFxBase *>(TFx::clone(recursive));
    result->m_cache = m_cache;
    return result;
  }
  std::shared_ptr<const otglb::RenderScene> get3DRenderScene(
      double frame, const int *canceled,
      const otglb::LightingRig *downstream                 = nullptr,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *settings                      = nullptr,
      const otam::MaterialBindings *materials = nullptr) const override {
    auto *source = m_source.source();
    if (!source || (canceled && *canceled)) return {};
    if (!m_environment.isConnected() || (settings && settings->m_flatRender))
      return source->get3DRenderScene(frame, canceled, downstream, transforms,
                                      settings, materials);
    auto map = environment(frame, canceled, settings);
    if (!map) return {};
    auto rig = downstream ? *downstream : otglb::LightingRig();
    if (!downstream) rig.preserveDefaultLighting = true;
    if (suppliesLighting()) {
      rig.preserveDefaultLighting = false;
      if (!map->prepareIrradiance(canceled)) return {};
    }
    rig.environments.insert(rig.environments.begin(),
                            layer(frame, std::move(map)));
    return source->get3DRenderScene(frame, canceled, &rig, transforms, settings,
                                    materials);
  }
  std::shared_ptr<const otglb::RenderScene> get3DRenderGeometry(
      double frame, const int *canceled,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *settings = nullptr) const override {
    auto *source = m_source.source();
    return source ? source->get3DRenderGeometry(frame, canceled, transforms,
                                                settings)
                  : nullptr;
  }
  bool doGetBBox(double frame, TRectD &bbox,
                 const TRenderSettings &info) override {
    bbox = TRectD();
    if (!m_source.isConnected()) return false;
    try {
      auto scene =
          get3DRenderGeometry(frame, info.m_isCanceled, nullptr, &info);
      if (!scene || scene->empty()) return false;
      const auto &b = scene->bounds;
      bbox          = TRectD(b[0], b[1], b[2], b[3]);
    } catch (const std::exception &) {
      bbox = TRectD(-500, -500, 500, 500);
    }
    return true;
  }
  // Predict the actual full panorama request, independently of output zoom or
  // tile placement. Source traversal also discovers upstream environment FX.
  void transform(double frame, int port, const TRectD &rect,
                 const TRenderSettings &info, TRectD &inputRect,
                 TRenderSettings &inputInfo) override {
    if (port != 1) {
      TStandardRasterFx::transform(frame, port, rect, info, inputRect,
                                   inputInfo);
      return;
    }
    inputInfo                    = info;
    inputInfo.m_affine           = TAffine();
    inputInfo.m_bpp              = 128;
    inputInfo.m_linearColorSpace = true;
    inputRect                    = TRectD();
    if (!m_environment.isConnected() || info.m_flatRender) return;
    TRectD box;
    if (!m_environment->doGetBBox(frame, box, inputInfo) || box.isEmpty())
      return;
    if (std::isfinite(box.x0) && std::isfinite(box.y0) &&
        std::isfinite(box.x1) && std::isfinite(box.y1)) {
      const TRectD rounded(std::floor(box.x0), std::floor(box.y0),
                           std::ceil(box.x1), std::ceil(box.y1));
      if (rounded.getLx() <= 8192 && rounded.getLy() <= 8192 &&
          rounded.getLx() * rounded.getLy() <= otglb::EnvironmentMap::MaxPixels)
        inputRect = rounded;
    }
  }
  bool canHandle(const TRenderSettings &, double) override { return true; }
  bool toBeComputedInLinearColorSpace(bool, bool tileIsLinear) const override {
    return tileIsLinear;
  }
  int getMemoryRequirement(const TRectD &r, double,
                           const TRenderSettings &) override {
    return int(std::min(
        double(std::numeric_limits<int>::max()),
        std::ceil(128 + std::max(0.0, r.getLx()) * std::max(0.0, r.getLy()) *
                            112 / (1024 * 1024))));
  }
  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &info) override {
    auto raster = tile.getRaster();
    raster->clear();
    try {
      auto scene =
          get3DRenderScene(frame, info.m_isCanceled, nullptr, nullptr, &info);
      if (!scene || scene->empty() || !raster->getLx() || !raster->getLy())
        return;
      otglb::RenderTile request;
      request.width  = raster->getLx();
      request.x      = tile.m_pos.x;
      const auto a   = info.m_affine;
      request.affine = {{a.a11, a.a12, a.a13, a.a21, a.a22, a.a23}};
      const int band =
          std::max(1, std::min(128, 2 * 1024 * 1024 / request.width));
      for (int y = 0; y < raster->getLy(); y += band) {
        request.y      = tile.m_pos.y + y;
        request.height = std::min(band, raster->getLy() - y);
        auto pixels    = otglb::renderTile(*scene, request, info.m_isCanceled);
        if (pixels.empty()) {
          raster->clear();
          return;
        }
        TRasterFP rf   = raster;
        TRaster32P r32 = raster;
        TRaster64P r64 = raster;
        if (!rf && !r32 && !r64)
          throw std::runtime_error("Unsupported environment output pixel type");
        raster->lock();
        for (int j = 0; j < request.height; ++j)
          for (int x = 0; x < request.width; ++x) {
            const auto p = otglb::outputPixel(
                pixels[std::size_t(j) * request.width + x], raster->isLinear());
            if (rf)
              rf->pixels(y + j)[x] = TPixelF(p.r, p.g, p.b, p.alpha);
            else if (r32)
              r32->pixels(y + j)[x] =
                  TPixel32(otglb::displayChannel<UCHAR>(p.r, 255),
                           otglb::displayChannel<UCHAR>(p.g, 255),
                           otglb::displayChannel<UCHAR>(p.b, 255),
                           otglb::displayChannel<UCHAR>(p.alpha, 255));
            else
              r64->pixels(y + j)[x] =
                  TPixel64(otglb::displayChannel<USHORT>(p.r, 65535),
                           otglb::displayChannel<USHORT>(p.g, 65535),
                           otglb::displayChannel<USHORT>(p.b, 65535),
                           otglb::displayChannel<USHORT>(p.alpha, 65535));
          }
        raster->unlock();
      }
    } catch (const std::exception &e) {
      throw TException(QString("Environment FX [%1]: %2")
                           .arg(QString::fromStdWString(getFxId()),
                                QString::fromUtf8(e.what()))
                           .toStdWString());
    }
  }
};

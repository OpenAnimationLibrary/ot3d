#include "stdfx.h"
#include "t3draster.h"
#include "t3dsource.h"
#include "tfxparam.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

class SurfaceSmoothing3DFx final : public TStandardRasterFx,
                                   public T3DRenderSource {
  FX_PLUGIN_DECLARATION(SurfaceSmoothing3DFx)
  T3DSourcePort m_source;
  TDoubleParamP m_strength{100.0}, m_creaseAngle{55.0};

public:
  SurfaceSmoothing3DFx() {
    addInputPort("Source", m_source);
    bindParam(this, "strength", m_strength);
    bindParam(this, "creaseAngle", m_creaseAngle);
    m_strength->setValueRange(0.0, 100.0);
    m_creaseAngle->setMeasureName("angle");
    m_creaseAngle->setValueRange(0.0, 180.0);
    enableComputeInFloat(true);
  }

  std::shared_ptr<const otglb::RenderScene> get3DRenderScene(
      double frame, const int *canceled,
      const otglb::LightingRig *lighting                   = nullptr,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *settings                      = nullptr,
      const otam::MaterialBindings *materials              = nullptr,
      const otam::InkBindings *ink                         = nullptr,
      const otam::NormalSmoothingStack *downstream = nullptr) const override {
    auto *source = m_source.source();
    if (!source || (canceled && *canceled)) return {};
    const double strength = m_strength->getValue(frame) / 100.0;
    const double crease   = m_creaseAngle->getValue(frame);
    if (!std::isfinite(strength) || !std::isfinite(crease))
      throw std::runtime_error("Invalid 3D Surface Smoothing parameters");
    if (strength <= 0 || (settings && settings->m_flatRender))
      return source->get3DRenderScene(frame, canceled, lighting, transforms,
                                      settings, materials, ink, downstream);

    otam::NormalSmoothingStack smoothing = {
        {std::min(1.0, strength), std::clamp(crease, 0.0, 180.0)}};
    if (downstream)
      smoothing.insert(smoothing.end(), downstream->begin(), downstream->end());
    if (smoothing.size() > 64)
      throw std::runtime_error("3D Surface Smoothing chain exceeds 64 nodes");
    return source->get3DRenderScene(frame, canceled, lighting, transforms,
                                    settings, materials, ink, &smoothing);
  }

  std::shared_ptr<const otglb::RenderScene> get3DRenderGeometry(
      double frame, const int *canceled,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *settings                      = nullptr,
      const otam::InkBindings *ink = nullptr) const override {
    auto *source = m_source.source();
    return source && !(canceled && *canceled)
               ? source->get3DRenderGeometry(frame, canceled, transforms,
                                             settings, ink)
               : nullptr;
  }

  bool doGetBBox(double frame, TRectD &bbox,
                 const TRenderSettings &info) override {
    bbox = TRectD();
    const auto scene =
        get3DRenderGeometry(frame, info.m_isCanceled, nullptr, &info);
    if (!scene || scene->empty()) return false;
    const auto &b = scene->bounds;
    bbox          = TRectD(b[0], b[1], b[2], b[3]);
    return !bbox.isEmpty();
  }
  bool canHandle(const TRenderSettings &, double) override { return true; }
  bool toBeComputedInLinearColorSpace(bool, bool tileIsLinear) const override {
    return tileIsLinear;
  }
  int getMemoryRequirement(const TRectD &rect, double,
                           const TRenderSettings &) override {
    if (rect.isEmpty()) return 0;
    const double mb = std::ceil(rect.getLx()) * std::ceil(rect.getLy()) *
                      256.0 / (1024.0 * 1024.0);
    return int(
        std::min<double>(std::numeric_limits<int>::max(), std::ceil(mb)));
  }
  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &info) override {
    tile.getRaster()->clear();
    try {
      const auto scene =
          get3DRenderScene(frame, info.m_isCanceled, nullptr, nullptr, &info);
      if (scene && !scene->empty()) ot3d::renderScene(*scene, tile, info);
    } catch (const std::exception &error) {
      throw TException(QString("3D Surface Smoothing [%1]: %2")
                           .arg(QString::fromStdWString(getFxId()),
                                QString::fromUtf8(error.what()))
                           .toStdWString());
    }
  }
};

FX_PLUGIN_IDENTIFIER(SurfaceSmoothing3DFx, "surfaceSmoothing3DFx")

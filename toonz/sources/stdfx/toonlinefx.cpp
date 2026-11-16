#include "stdfx.h"
#include "t3draster.h"
#include "t3dsource.h"
#include "tfxparam.h"
#include "tparamset.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

class ToonLineFx final : public TStandardRasterFx, public T3DRenderSource {
  FX_PLUGIN_DECLARATION(ToonLineFx)
  T3DSourcePort m_source;
  TDoubleParamP m_width{1.5}, m_lightInfluence{75.0};
  TPixelParamP m_color{TPixel32::Black};

  std::shared_ptr<const otglb::RenderScene> withOutline(
      std::shared_ptr<const otglb::RenderScene> source, double frame,
      const TRenderSettings *settings) const {
    if (!source || source->empty() || (settings && settings->m_flatRender))
      return source;
    const auto value = [frame](const TDoubleParamP &param, double maximum) {
      const double v = param->getValue(frame);
      if (!std::isfinite(v))
        throw std::runtime_error("Invalid Toon Line parameter");
      return std::clamp(v, 0.0, maximum);
    };
    otglb::ToonLineStyle style;
    style.width         = value(m_width, 20.0);
    const TPixelD color = m_color->getValueD(frame);
    if (style.width == 0 || color.m == 0) return source;
    style.lightingInfluence = value(m_lightInfluence, 100.0) / 100;
    style.color             = {
                    {float(color.r), float(color.g), float(color.b), float(color.m)}};
    auto scene            = std::make_shared<otglb::RenderScene>();
    const auto &b         = source->bounds;
    const double pad      = style.maximumRadius() + 1;
    scene->bounds         = {{b[0] - pad, b[1] - pad, b[2] + pad, b[3] + pad}};
    scene->linearSampling = source->linearSampling;
    scene->materialGroups = source->materialGroups;
    scene->splineGroups   = source->splineGroups;
    scene->warnings       = source->warnings;
    scene->toonLine       = style;
    scene->layers.push_back({std::move(source)});
    return scene;
  }

public:
  ToonLineFx() {
    addInputPort("Source", m_source);
    bindParam(this, "width", m_width);
    bindParam(this, "color", m_color);
    m_color->enableMatte(true);
    bindParam(this, "lightInfluence", m_lightInfluence);
    m_width->setMeasureName("fxLength");
    m_width->setValueRange(0, 20);
    m_lightInfluence->setValueRange(0, 100);
    enableComputeInFloat(true);
  }

  std::shared_ptr<const otglb::RenderScene> get3DRenderScene(
      double frame, const int *canceled,
      const otglb::LightingRig *lighting                   = nullptr,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *settings                      = nullptr,
      const otam::MaterialBindings *materials              = nullptr,
      const otam::InkBindings *ink                         = nullptr,
      const otam::NormalSmoothingStack *smoothing = nullptr) const override {
    const auto *source = m_source.source();
    if (!source || (canceled && *canceled)) return {};
    return withOutline(
        source->get3DRenderScene(frame, canceled, lighting, transforms,
                                 settings, materials, ink, smoothing),
        frame, settings);
  }

  std::shared_ptr<const otglb::RenderScene> get3DRenderGeometry(
      double frame, const int *canceled,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *settings                      = nullptr,
      const otam::InkBindings *ink = nullptr) const override {
    const auto *source = m_source.source();
    if (!source || (canceled && *canceled)) return {};
    return withOutline(
        source->get3DRenderGeometry(frame, canceled, transforms, settings, ink),
        frame, settings);
  }

  bool doGetBBox(double frame, TRectD &box,
                 const TRenderSettings &info) override {
    box = TRectD();
    const auto scene =
        get3DRenderGeometry(frame, info.m_isCanceled, nullptr, &info);
    if (!scene || scene->empty()) return false;
    const auto &b = scene->bounds;
    box           = TRectD(b[0], b[1], b[2], b[3]);
    return !box.isEmpty();
  }

  bool canHandle(const TRenderSettings &, double) override { return true; }
  bool toBeComputedInLinearColorSpace(bool, bool tileIsLinear) const override {
    return tileIsLinear;
  }

  int getMemoryRequirement(const TRectD &rect, double,
                           const TRenderSettings &) override {
    if (rect.isEmpty()) return 0;
    const double width  = std::min(std::ceil(rect.getLx()), 512.0) + 100;
    const double height = std::min(std::ceil(rect.getLy()), 128.0) + 100;
    return int(std::ceil(width * height * 512 / (1024 * 1024)));
  }

  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &info) override {
    tile.getRaster()->clear();
    try {
      const auto scene =
          get3DRenderScene(frame, info.m_isCanceled, nullptr, nullptr, &info);
      if (scene && !scene->empty()) ot3d::renderScene(*scene, tile, info);
    } catch (const std::exception &error) {
      throw TException(QString("Toon Line [%1]: %2")
                           .arg(QString::fromStdWString(getFxId()),
                                QString::fromUtf8(error.what()))
                           .toStdWString());
    }
  }
};

FX_PLUGIN_IDENTIFIER(ToonLineFx, "toonLineFx")

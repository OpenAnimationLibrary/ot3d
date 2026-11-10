#include "stdfx.h"
#include "t3draster.h"
#include "t3dsource.h"
#include "tfxammaterial.h"
#include "tfxparam.h"
#include "tparamset.h"
#include "tnotanimatableparam.h"
#include "tspectrumparam.h"
#include "ttonecurveparam.h"

#include <cmath>
#include <limits>

class InkLineFx final : public TStandardRasterFx,
                        public T3DRenderSource,
                        public TFxAMGroupSource {
  FX_PLUGIN_DECLARATION(InkLineFx)
  T3DSourcePort m_source;
  TStringParamP m_group;
  TIntEnumParamP m_scope, m_profileRange;
  TBoolParamP m_profile, m_useSplineProfile, m_useRamp;
  TToneCurveParamP m_widthCurve;
  TSpectrumParamP m_colorRamp;
  std::array<TDoubleParamP, 3> m_widths;
  std::array<TPixelParamP, 3> m_colors;
  TDoubleParamP m_middle, m_roughness, m_fadeIn, m_fadeOut;

  static otam::InkColor color(const TPixelD &pixel) {
    const auto channel = [](double v) {
      if (!std::isfinite(v)) throw std::runtime_error("Invalid ink color");
      return float(std::clamp(v, 0.0, 1.0));
    };
    return {{channel(pixel.r), channel(pixel.g), channel(pixel.b),
             channel(pixel.m)}};
  }

  otam::InkProfile splineProfile(double frame) const {
    otam::InkProfile profile;
    profile.perSection = m_profileRange->getValue() == 1;
    profile.linear     = m_widthCurve->isLinear();
    const auto points  = m_widthCurve->getValue(frame);
    // Tone Curve stores one extrapolation span outside each graph edge.
    if (points.size() < 10 || (points.size() - 1) % 3 || points.size() > 256)
      throw std::runtime_error("Invalid Ink Line width curve");
    double position = 0;
    for (int i = 3; i < points.size() - 4; i += 3) {
      auto &span = profile.width.emplace_back();
      for (int j = 0; j < 4; ++j) {
        const auto &p = points[i + j];
        if (!std::isfinite(p.x) || !std::isfinite(p.y))
          throw std::runtime_error("Invalid Ink Line width curve point");
        span[j] = {{p.x / 255, 2 * p.y / 255}};
      }
      span.front()[0] = position;
      position        = i + 3 == points.size() - 4
                            ? 1
                            : std::clamp(span.back()[0], position, 1.0);
      span.back()[0]  = position;
    }
    if (m_useRamp->getValue()) {
      const auto count = m_colorRamp->getKeyCount();
      if (count > 256)
        throw std::runtime_error("Ink Line ramp exceeds 256 stops");
      for (int i = 0; i < count; ++i) {
        const auto u = m_colorRamp->getPosition(i)->getValue(frame);
        if (!std::isfinite(u))
          throw std::runtime_error("Invalid Ink Line ramp position");
        profile.colors.emplace_back(
            std::clamp(u, 0.0, 1.0),
            color(m_colorRamp->getColor(i)->getValueD(frame)));
      }
      std::stable_sort(
          profile.colors.begin(), profile.colors.end(),
          [](const auto &a, const auto &b) { return a.first < b.first; });
    }
    return profile;
  }

  otam::InkBindings bindings(double frame,
                             const otam::InkBindings *downstream) const {
    const auto value = [frame](const TDoubleParamP &param, double low,
                               double high) {
      const double v = param->getValue(frame);
      if (!std::isfinite(v)) throw std::runtime_error("Invalid ink parameter");
      return std::clamp(v, low, high);
    };
    otam::InkStyle style;
    style.group =
        QString::fromStdWString(m_group->getValue()).toUtf8().toStdString();
    style.unfilledOnly = m_scope->getValue() == 1;
    const bool advanced = m_useSplineProfile->getValue();
    const bool vary     = !advanced && m_profile->getValue();
    style.middle       = vary ? value(m_middle, 1, 99) / 100 : 0.5;
    style.roughness    = value(m_roughness, 0, 100) / 100;
    style.fadeIn       = value(m_fadeIn, 0, 100) / 100;
    style.fadeOut      = value(m_fadeOut, 0, 100) / 100;
    for (int i = 0; i < 3; ++i) {
      const int index    = vary ? i : 0;
      style.widths[i]    = value(m_widths[index], 0, 1000);
      style.colors[i]    = color(m_colors[index]->getValueD(frame));
    }
    if (advanced) style.profile = splineProfile(frame);
    otam::InkBindings ink{std::move(style)};
    if (downstream) {
      if (downstream->size() >= 64)
        throw std::runtime_error("A:M ink chain exceeds 64 assignments");
      ink.insert(ink.end(), downstream->begin(), downstream->end());
    }
    return ink;
  }

public:
  InkLineFx()
      : m_group(new TStringParam())
      , m_scope(new TIntEnumParam(0, "All Splines"))
      , m_profileRange(new TIntEnumParam(0, "Whole Spline"))
      , m_profile(false)
      , m_useSplineProfile(false)
      , m_useRamp(false)
      , m_widthCurve(new TToneCurveParam())
      , m_colorRamp(
            new TSpectrumParam({{0, TPixel32::Black}, {1, TPixel32::Black}}))
      , m_middle(50.0)
      , m_roughness(0.0)
      , m_fadeIn(0.0)
      , m_fadeOut(0.0) {
    addInputPort("Source", m_source);
    m_scope->addItem(1, "Unfilled Splines Only");
    bindParam(this, "group", m_group);
    bindParam(this, "scope", m_scope);
    bindParam(this, "profile", m_profile);
    m_profileRange->addItem(1, "Each Inked Section");
    bindParam(this, "useSplineProfile", m_useSplineProfile);
    bindParam(this, "profileRange", m_profileRange);
    bindParam(this, "widthCurve", m_widthCurve);
    bindParam(this, "useColorRamp", m_useRamp);
    bindParam(this, "colorRamp", m_colorRamp);
    auto width = m_widthCurve->getValue(0);
    for (auto &point : width) point.y = 127.5;
    m_widthCurve->setDefaultValue(width);
    m_colorRamp->enableMatte(true);
    const char *widthNames[] = {"startWidth", "middleWidth", "endWidth"};
    const char *colorNames[] = {"startColor", "middleColor", "endColor"};
    for (int i = 0; i < 3; ++i) {
      m_widths[i] = TDoubleParamP(3.0);
      m_widths[i]->setMeasureName("fxLength");
      m_widths[i]->setValueRange(0, 1000);
      m_colors[i] = TPixelParamP(TPixel32::Black);
      bindParam(this, widthNames[i], m_widths[i]);
      bindParam(this, colorNames[i], m_colors[i]);
    }
    bindParam(this, "middle", m_middle);
    bindParam(this, "roughness", m_roughness);
    bindParam(this, "fadeIn", m_fadeIn);
    bindParam(this, "fadeOut", m_fadeOut);
    m_middle->setValueRange(1, 99);
    for (auto param : {m_roughness, m_fadeIn, m_fadeOut})
      param->setValueRange(0, 100);
    enableComputeInFloat(true);
  }

  std::vector<std::wstring> getAMGroups(double frame) const override {
    const auto *source = m_source.source();
    const auto geometry =
        source ? source->get3DRenderGeometry(frame, nullptr) : nullptr;
    std::vector<std::wstring> result;
    if (geometry)
      for (const auto &group : geometry->splineGroups)
        if (!group.empty())
          result.push_back(QString::fromUtf8(group.data(), int(group.size()))
                               .toStdWString());
    return result;
  }

  std::shared_ptr<const otglb::RenderScene> get3DRenderScene(
      double frame, const int *canceled,
      const otglb::LightingRig *lighting                   = nullptr,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *settings                      = nullptr,
      const otam::MaterialBindings *materials              = nullptr,
      const otam::InkBindings *downstream = nullptr) const override {
    const auto *source = m_source.source();
    if (!source || (canceled && *canceled)) return {};
    const auto ink = bindings(frame, downstream);
    return source->get3DRenderScene(frame, canceled, lighting, transforms,
                                    settings, materials, &ink);
  }

  std::shared_ptr<const otglb::RenderScene> get3DRenderGeometry(
      double frame, const int *canceled,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *settings                      = nullptr,
      const otam::InkBindings *downstream = nullptr) const override {
    const auto *source = m_source.source();
    if (!source || (canceled && *canceled)) return {};
    const auto ink = bindings(frame, downstream);
    return source->get3DRenderGeometry(frame, canceled, transforms, settings,
                                       &ink);
  }

  bool doGetBBox(double frame, TRectD &bbox,
                 const TRenderSettings &info) override {
    bbox = TRectD();
    try {
      const auto scene =
          get3DRenderGeometry(frame, info.m_isCanceled, nullptr, &info);
      if (!scene || scene->empty()) return false;
      const auto &b = scene->bounds;
      bbox          = TRectD(b[0], b[1], b[2], b[3]);
      return !bbox.isEmpty();
    } catch (const std::exception &) {
      bbox = TRectD(-500, -500, 500, 500);
      return true;
    }
  }

  bool canHandle(const TRenderSettings &, double) override { return true; }
  bool toBeComputedInLinearColorSpace(bool, bool) const override {
    return false;
  }

  int getMemoryRequirement(const TRectD &rect, double,
                           const TRenderSettings &) override {
    if (rect.isEmpty()) return 0;
    const double mb = std::ceil(rect.getLx()) * std::ceil(rect.getLy()) *
                      256.0 / (1024.0 * 1024.0);
    return int(
        std::min(double(std::numeric_limits<int>::max()), std::ceil(mb)));
  }

  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &info) override {
    tile.getRaster()->clear();
    try {
      const auto scene =
          get3DRenderScene(frame, info.m_isCanceled, nullptr, nullptr, &info);
      if (scene && !scene->empty()) ot3d::renderScene(*scene, tile, info);
    } catch (const std::exception &error) {
      throw TException(QString("Ink Line [%1]: %2")
                           .arg(QString::fromStdWString(getFxId()),
                                QString::fromUtf8(error.what()))
                           .toStdWString());
    }
  }
};

FX_PLUGIN_IDENTIFIER(InkLineFx, "inkLineFx")

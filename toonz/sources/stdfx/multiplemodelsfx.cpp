#include "stdfx.h"
#include "t3dsource.h"
#include "t3draster.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

// Keep each source unevaluated until downstream lighting/transforms have been
// forwarded. The raster boundary uses ordinary foreground-over-background.
class MultipleModelsFx final : public TStandardRasterFx,
                               public T3DRenderSource,
                               public T3DRenderGroup,
                               public TFxDynamicPortFactory {
  FX_PLUGIN_DECLARATION(MultipleModelsFx)
  TFxPortDG m_models;
  std::vector<TAffine> m_affines;  // Render-tree-only column placement.

  std::shared_ptr<const otglb::RenderScene> scene(
      double frame, const int *canceled, const otglb::LightingRig *lighting,
      const std::vector<otglb::ModelTransform> *transforms,
      const TRenderSettings *settings, bool geometryOnly,
      const otam::MaterialBindings *materials     = nullptr,
      const otam::InkBindings *ink                = nullptr,
      const otam::NormalSmoothingStack *smoothing = nullptr) const {
    auto result       = std::make_shared<otglb::RenderScene>();
    const auto &ports = m_models.ports();
    TRectD bounds;
    // Model1 is the foreground. Composite from the last input toward it.
    for (int i = int(ports.size()) - 1; i >= 0; --i) {
      if (canceled && *canceled) return {};
      const auto *port = dynamic_cast<const T3DSourcePort *>(ports[i]);
      if (!port) throw std::runtime_error("Invalid Multiple Models input");
      auto *source = port->source();
      if (!source) continue;
      auto child =
          geometryOnly
              ? source->get3DRenderGeometry(frame, canceled, transforms,
                                            settings, ink)
              : source->get3DRenderScene(frame, canceled, lighting, transforms,
                                         settings, materials, ink, smoothing);
      if (!child) continue;
      for (const auto &group : child->splineGroups)
        if (std::find(result->splineGroups.begin(), result->splineGroups.end(),
                      group) == result->splineGroups.end())
          result->splineGroups.push_back(group);
      if (child->empty()) continue;
      const auto a  = i < int(m_affines.size()) ? m_affines[i] : TAffine();
      const auto &b = child->bounds;
      bounds += a * TRectD(b[0], b[1], b[2], b[3]);
      result->linearSampling |= child->linearSampling;
      result->layers.push_back(
          {child, {{a.a11, a.a12, a.a13, a.a21, a.a22, a.a23}}});
      result->warnings.insert(result->warnings.end(), child->warnings.begin(),
                              child->warnings.end());
      for (const auto &group : child->materialGroups)
        if (std::find(result->materialGroups.begin(),
                      result->materialGroups.end(),
                      group) == result->materialGroups.end())
          result->materialGroups.push_back(group);
    }
    result->bounds = {{bounds.x0, bounds.y0, bounds.x1, bounds.y1}};
    return result;
  }

public:
  MultipleModelsFx() : m_models("Model", 1) {
    addInputPort("Model1", createDynamicPort(0), 0);
    enableComputeInFloat(true);
  }
  int dynamicPortGroupsCount() const override { return 1; }
  const TFxPortDG *dynamicPortGroup(int group) const override {
    return group == 0 ? &m_models : nullptr;
  }
  TFxPort *createDynamicPort(int group) const override {
    return group == 0 ? new T3DSourcePort : nullptr;
  }
  void set3DInputAffine(int input, const TAffine &affine) override {
    if (input < 0 || input >= getInputPortCount()) return;
    m_affines.resize(getInputPortCount());
    m_affines[input] = affine;
  }
  TFx *clone(bool recursive = true) const override {
    auto *result      = static_cast<MultipleModelsFx *>(TFx::clone(recursive));
    result->m_affines = m_affines;
    return result;
  }
  std::string getAlias(double frame,
                       const TRenderSettings &settings) const override {
    std::ostringstream out;
    out << TStandardRasterFx::getAlias(frame, settings)
        << std::setprecision(17);
    for (const auto &a : m_affines)
      out << '[' << a.a11 << ',' << a.a12 << ',' << a.a13 << ',' << a.a21 << ','
          << a.a22 << ',' << a.a23 << ']';
    return out.str();
  }
  std::shared_ptr<const otglb::RenderScene> get3DRenderScene(
      double frame, const int *canceled,
      const otglb::LightingRig *lighting                   = nullptr,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *settings                      = nullptr,
      const otam::MaterialBindings *materials              = nullptr,
      const otam::InkBindings *ink                         = nullptr,
      const otam::NormalSmoothingStack *smoothing = nullptr) const override {
    return scene(frame, canceled, lighting, transforms, settings, false,
                 materials, ink, smoothing);
  }
  std::shared_ptr<const otglb::RenderScene> get3DRenderGeometry(
      double frame, const int *canceled,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *settings                      = nullptr,
      const otam::InkBindings *ink = nullptr) const override {
    return scene(frame, canceled, nullptr, transforms, settings, true, nullptr,
                 ink);
  }
  bool doGetBBox(double frame, TRectD &bbox,
                 const TRenderSettings &info) override {
    bbox = TRectD();
    const auto geometry =
        get3DRenderGeometry(frame, info.m_isCanceled, nullptr, &info);
    if (!geometry || geometry->empty()) return false;
    const auto &b = geometry->bounds;
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
                      144.0 / (1024.0 * 1024.0);
    return int(
        std::min(double(std::numeric_limits<int>::max()), std::ceil(mb)));
  }
  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &info) override {
    tile.getRaster()->clear();
    try {
      const auto merged =
          get3DRenderScene(frame, info.m_isCanceled, nullptr, nullptr, &info);
      if (!merged || merged->empty()) return;
      ot3d::renderScene(*merged, tile, info);
    } catch (const std::exception &error) {
      throw TException(QString("Multiple Models [%1]: %2")
                           .arg(QString::fromStdWString(getFxId()),
                                QString::fromUtf8(error.what()))
                           .toStdWString());
    }
  }
};

FX_PLUGIN_IDENTIFIER(MultipleModelsFx, "multipleModelsFx")

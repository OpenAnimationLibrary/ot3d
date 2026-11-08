#include "stdfx.h"
#include "t3dsource.h"
#include "tfxparam.h"
#include "tnotanimatableparam.h"
#include "tparamset.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
template <class PIXEL>
void copyPointLightPixels(TRasterPT<PIXEL> raster,
                          const std::vector<otglb::ColorPixel> &pixels,
                          int y0) {
  using Channel = typename PIXEL::Channel;
  raster->lock();
  for (int y = 0; y < int(pixels.size() / raster->getLx()); ++y)
    for (int x = 0; x < raster->getLx(); ++x) {
      const auto p = otglb::outputPixel(
          pixels[std::size_t(y) * raster->getLx() + x], raster->isLinear());
      raster->pixels(y + y0)[x] = PIXEL(
          otglb::displayChannel<Channel>(p.r, PIXEL::maxChannelValue),
          otglb::displayChannel<Channel>(p.g, PIXEL::maxChannelValue),
          otglb::displayChannel<Channel>(p.b, PIXEL::maxChannelValue),
          otglb::displayChannel<Channel>(p.alpha, PIXEL::maxChannelValue));
    }
  raster->unlock();
}
}  // namespace

class PointLightFx final : public TStandardRasterFx,
                           public T3DRenderSource,
                           public T3DRenderGroup {
  FX_PLUGIN_DECLARATION(PointLightFx)
  T3DSourcePort m_source, m_target;
  TIntEnumParamP m_type;
  TDoubleParamP m_x, m_y, m_z, m_targetX, m_targetY, m_targetZ;
  TDoubleParamP m_intensity, m_radius, m_cone, m_softness, m_visibleSize;
  TPixelParamP m_color;
  TBoolParamP m_visible;
  TAffine m_affines[2];

  std::array<double, 3> position(double frame) const {
    return {{m_x->getValue(frame), m_y->getValue(frame), m_z->getValue(frame)}};
  }
  otglb::DiffuseLight light(double frame, const int *canceled,
                            const TRenderSettings *settings) const {
    otglb::DiffuseLight result;
    const auto p = position(frame);
    std::array<double, 3> target{{m_targetX->getValue(frame),
                                  m_targetY->getValue(frame),
                                  m_targetZ->getValue(frame)}};
    if (auto *source = m_target.source()) {
      // Bounds only: target pixels/materials are never evaluated.
      auto geometry =
          source->get3DRenderGeometry(frame, canceled, nullptr, settings);
      const auto relative = m_affines[0].inv() * m_affines[1];
      const std::array<double, 6> a{{relative.a11, relative.a12, relative.a13,
                                     relative.a21, relative.a22, relative.a23}};
      if (geometry)
        if (auto center = otglb::targetCenter(*geometry, a))
          for (int k = 0; k < 3; ++k) target[k] += (*center)[k];
    }
    result.direction = {{p[0] - target[0], p[1] - target[1], p[2] - target[2]}};
    const double length = std::hypot(result.direction[0], result.direction[1],
                                     result.direction[2]);
    // Coincident target has a stable forward direction, including animated
    // crossings.
    if (length <= 1e-9)
      result.direction = {{0, 0, 1}};
    else
      for (auto &v : result.direction) v /= length;
    result.worldDirection = true;
    result.intensity      = m_intensity->getValue(frame) / 100;
    const auto c          = m_color->getValueD(frame);
    result.color          = {{float(std::clamp(c.r, 0.0, 1.0)),
                              float(std::clamp(c.g, 0.0, 1.0)),
                              float(std::clamp(c.b, 0.0, 1.0))}};
    if (m_type->getValue() != 0)
      result.point = otglb::PointPlacement{p, m_radius->getValue(frame)};
    if (m_type->getValue() == 2) {
      otglb::SpotCone cone;
      for (int k = 0; k < 3; ++k) cone.axis[k] = -result.direction[k];
      cone.outer  = m_cone->getValue(frame) / 2;
      cone.inner  = cone.outer * (1 - m_softness->getValue(frame) / 100);
      result.spot = cone;
    }
    result.validate();
    return result;
  }
  std::shared_ptr<const otglb::RenderScene> finish(
      std::shared_ptr<const otglb::RenderScene> source, double frame) const {
    if (!source) return {};
    if (m_visible->getValue() && m_intensity->getValue(frame) > 0) {
      auto copy         = std::make_shared<otglb::RenderScene>(*source);
      const auto c      = m_color->getValueD(frame);
      const double gain = m_intensity->getValue(frame) / 100;
      const std::array<float, 3> radiance{
          {otglb::linearToSrgbExtended(float(
               gain * otglb::srgbToLinear(float(std::clamp(c.r, 0.0, 1.0))))),
           otglb::linearToSrgbExtended(float(
               gain * otglb::srgbToLinear(float(std::clamp(c.g, 0.0, 1.0))))),
           otglb::linearToSrgbExtended(float(
               gain * otglb::srgbToLinear(float(std::clamp(c.b, 0.0, 1.0)))))}};
      otglb::appendVisibleLight(*copy, position(frame),
                                m_visibleSize->getValue(frame), radiance);
      source = copy;
    }
    if (m_affines[0] == TAffine()) return source;
    auto placed            = std::make_shared<otglb::RenderScene>();
    const auto &a          = m_affines[0];
    const auto &b          = source->bounds;
    const auto box         = a * TRectD(b[0], b[1], b[2], b[3]);
    placed->bounds         = {{box.x0, box.y0, box.x1, box.y1}};
    placed->linearSampling = source->linearSampling;
    placed->layers.push_back(
        {source, {{a.a11, a.a12, a.a13, a.a21, a.a22, a.a23}}});
    placed->warnings       = source->warnings;
    placed->materialGroups = source->materialGroups;
    return placed;
  }

public:
  PointLightFx()
      : m_type(new TIntEnumParam(1, "Bulb"))
      , m_x(0.0)
      , m_y(0.0)
      , m_z(5.0)
      , m_targetX(0.0)
      , m_targetY(0.0)
      , m_targetZ(0.0)
      , m_intensity(100.0)
      , m_radius(5.0)
      , m_cone(60.0)
      , m_softness(25.0)
      , m_visibleSize(.1)
      , m_color(TPixel32::White)
      , m_visible(false) {
    addInputPort("Source", m_source);
    addInputPort("Target", m_target);
    m_type->addItem(0, "Sun");
    m_type->addItem(2, "Klieg (Spotlight)");
    bindParam(this, "lightType", m_type);
    bindParam(this, "positionX", m_x);
    bindParam(this, "positionY", m_y);
    bindParam(this, "positionZ", m_z);
    bindParam(this, "targetX", m_targetX);
    bindParam(this, "targetY", m_targetY);
    bindParam(this, "targetZ", m_targetZ);
    bindParam(this, "intensity", m_intensity);
    bindParam(this, "color", m_color);
    bindParam(this, "falloffRadius", m_radius);
    bindParam(this, "coneAngle", m_cone);
    bindParam(this, "coneSoftness", m_softness);
    bindParam(this, "visible", m_visible);
    bindParam(this, "visibleSize", m_visibleSize);
    m_intensity->setValueRange(0, 1000);
    m_radius->setValueRange(.001, 100000);
    m_cone->setMeasureName("angle");
    m_cone->setValueRange(.01, 179);
    m_softness->setValueRange(0, 100);
    m_visibleSize->setValueRange(.001, 100000);
    enableComputeInFloat(true);
  }
  void set3DInputAffine(int input, const TAffine &a) override {
    if (input >= 0 && input < 2) m_affines[input] = a;
  }
  TFx *clone(bool recursive = true) const override {
    auto *copy = static_cast<PointLightFx *>(TFx::clone(recursive));
    std::copy(std::begin(m_affines), std::end(m_affines), copy->m_affines);
    return copy;
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
      const otglb::LightingRig *downstream                 = nullptr,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *settings                      = nullptr,
      const otam::MaterialBindings *materials = nullptr) const override {
    auto *source = m_source.source();
    if (!source || (canceled && *canceled)) return {};
    auto rig = downstream ? *downstream : otglb::LightingRig();
    rig.ambient *= rig.master;
    for (auto &l : rig.lights) l.intensity *= rig.master;
    rig.master                  = 1;
    rig.preserveDefaultLighting = false;
    rig.lights.push_back(light(frame, canceled, settings));
    return finish(source->get3DRenderScene(frame, canceled, &rig, transforms,
                                           settings, materials),
                  frame);
  }
  std::shared_ptr<const otglb::RenderScene> get3DRenderGeometry(
      double frame, const int *canceled,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *settings = nullptr) const override {
    auto *source = m_source.source();
    if (!source || (canceled && *canceled)) return {};
    return finish(
        source->get3DRenderGeometry(frame, canceled, transforms, settings),
        frame);
  }
  bool doGetBBox(double frame, TRectD &bbox,
                 const TRenderSettings &info) override {
    bbox = TRectD();
    if (!m_source.isConnected()) return false;
    try {
      const auto rendered =
          get3DRenderGeometry(frame, info.m_isCanceled, nullptr, &info);
      if (!rendered || rendered->empty()) return false;
      const auto &b = rendered->bounds;
      bbox          = TRectD(b[0], b[1], b[2], b[3]);
      return true;
    } catch (const std::exception &) {
      bbox = TRectD(-500, -500, 500, 500);
      return true;
    }
  }

  bool canHandle(const TRenderSettings &, double) override { return true; }

  int getMemoryRequirement(const TRectD &rect, double,
                           const TRenderSettings &) override {
    if (rect.isEmpty()) return 0;
    const double megabytes = std::ceil(rect.getLx()) * std::ceil(rect.getLy()) *
                             112.0 / (1024.0 * 1024.0);
    return int(std::min(double(std::numeric_limits<int>::max()),
                        std::ceil(megabytes)));
  }

  bool toBeComputedInLinearColorSpace(bool, bool tileIsLinear) const override {
    return tileIsLinear;
  }

  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &info) override {
    tile.getRaster()->clear();
    if (!m_source.isConnected()) return;
    try {
      const auto rendered =
          get3DRenderScene(frame, info.m_isCanceled, nullptr, nullptr, &info);
      if (!rendered || rendered->empty()) return;

      otglb::RenderTile request;
      request.width  = tile.getRaster()->getLx();
      request.height = tile.getRaster()->getLy();
      request.x      = tile.m_pos.x;
      request.y      = tile.m_pos.y;
      const auto &a  = info.m_affine;
      request.affine = {{a.a11, a.a12, a.a13, a.a21, a.a22, a.a23}};
      if (!request.width || !request.height) return;

      const int band =
          std::max(1, std::min(128, 2 * 1024 * 1024 / request.width));
      const int height = request.height;
      for (int y = 0; y < height; y += band) {
        request.height = std::min(band, height - y);
        request.y      = tile.m_pos.y + y;
        const auto pixels =
            otglb::renderTile(*rendered, request, info.m_isCanceled);
        if (pixels.empty()) {
          tile.getRaster()->clear();
          return;
        }
        if (TRasterFP raster = tile.getRaster())
          copyPointLightPixels<TPixelF>(raster, pixels, y);
        else if (TRaster64P raster = tile.getRaster())
          copyPointLightPixels<TPixel64>(raster, pixels, y);
        else if (TRaster32P raster = tile.getRaster())
          copyPointLightPixels<TPixel32>(raster, pixels, y);
        else
          throw std::runtime_error("Unsupported 3D output pixel type.");
      }
    } catch (const std::exception &error) {
      throw TException(QString("Point Light [%1]: %2")
                           .arg(QString::fromStdWString(getFxId()),
                                QString::fromUtf8(error.what()))
                           .toStdWString());
    }
  }
};

FX_PLUGIN_IDENTIFIER(PointLightFx, "pointLightFx")

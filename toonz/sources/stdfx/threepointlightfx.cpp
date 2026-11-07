#include "stdfx.h"
#include "t3dsource.h"
#include "tfxparam.h"
#include "tnotanimatableparam.h"
#include "tparamset.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
constexpr double Pi = 3.14159265358979323846;

template <class PIXEL>
void copy3DPixels(TRasterPT<PIXEL> raster,
                  const std::vector<otglb::ColorPixel> &pixels, int yOffset) {
  using Channel = typename PIXEL::Channel;
  const double maximum = PIXEL::maxChannelValue;
  raster->lock();
  const int rows = int(pixels.size() / raster->getLx());
  for (int y = 0; y < rows; ++y) {
    auto *row = raster->pixels(y + yOffset);
    for (int x = 0; x < raster->getLx(); ++x) {
      const auto &p = pixels[std::size_t(y) * raster->getLx() + x];
      row[x]        = PIXEL(otglb::displayChannel<Channel>(p.r, maximum),
                            otglb::displayChannel<Channel>(p.g, maximum),
                            otglb::displayChannel<Channel>(p.b, maximum),
                            otglb::displayChannel<Channel>(p.alpha, maximum));
    }
  }
  raster->unlock();
}

std::array<double, 3> lightDirection(double azimuth, double elevation) {
  const double a = azimuth * Pi / 180.0;
  const double e = elevation * Pi / 180.0;
  const double ce = std::cos(e);
  return {{std::sin(a) * ce, std::sin(e), std::cos(a) * ce}};
}

std::array<float, 3> lightColor(const TPixelD &color) {
  return {{float(std::clamp(color.r, 0.0, 1.0)),
           float(std::clamp(color.g, 0.0, 1.0)),
           float(std::clamp(color.b, 0.0, 1.0))}};
}
}  // namespace

// Accumulate diffuse lighting before the 3D source is shaded and projected.
class ThreePointLightFx final : public TStandardRasterFx,
                                public T3DRenderSource {
  FX_PLUGIN_DECLARATION(ThreePointLightFx)

  T3DSourcePort m_source;
  TIntEnumParamP m_lightMode;
  TDoubleParamP m_rigX, m_rigY, m_rigZ, m_rigRadius;
  TDoubleParamP m_masterIntensity, m_ambient;
  TDoubleParamP m_keyAzimuth, m_keyElevation, m_keyIntensity;
  TPixelParamP m_keyColor;
  TDoubleParamP m_fillAzimuth, m_fillElevation, m_fillIntensity;
  TPixelParamP m_fillColor;
  TDoubleParamP m_rimAzimuth, m_rimElevation, m_rimIntensity;
  TPixelParamP m_rimColor;

  otglb::DiffuseLight light(double frame, const TDoubleParamP &azimuth,
                            const TDoubleParamP &elevation,
                            const TDoubleParamP &intensity,
                            const TPixelParamP &color) const {
    otglb::DiffuseLight result;
    result.direction = lightDirection(azimuth->getValue(frame),
                                      elevation->getValue(frame));
    result.intensity = intensity->getValue(frame) / 100.0;
    result.color = lightColor(color->getValueD(frame));
    return result;
  }

  otglb::LightingRig lighting(double frame) const {
    otglb::LightingRig result;
    result.master = m_masterIntensity->getValue(frame) / 100.0;
    result.ambient = m_ambient->getValue(frame) / 100.0;
    result.lights.push_back(light(frame, m_keyAzimuth, m_keyElevation,
                                  m_keyIntensity, m_keyColor));
    result.lights.push_back(light(frame, m_fillAzimuth, m_fillElevation,
                                  m_fillIntensity, m_fillColor));
    result.lights.push_back(light(frame, m_rimAzimuth, m_rimElevation,
                                  m_rimIntensity, m_rimColor));
    if (m_lightMode->getValue() == 1) {
      const double radius = m_rigRadius->getValue(frame);
      const std::array<double, 3> center{{m_rigX->getValue(frame),
                                          m_rigY->getValue(frame),
                                          m_rigZ->getValue(frame)}};
      for (auto &light : result.lights) {
        otglb::PointPlacement point;
        point.radius = radius;
        for (int axis = 0; axis < 3; ++axis)
          point.position[axis] = center[axis] + radius * light.direction[axis];
        light.point = point;
      }
    }
    return result;
  }

public:
  ThreePointLightFx()
      : m_lightMode(new TIntEnumParam(0, "Directional"))
      , m_rigX(0.0)
      , m_rigY(0.0)
      , m_rigZ(0.0)
      , m_rigRadius(10.0)
      , m_masterIntensity(100.0)
      , m_ambient(10.0)
      , m_keyAzimuth(-45.0)
      , m_keyElevation(35.0)
      , m_keyIntensity(100.0)
      , m_keyColor(TPixel32::White)
      , m_fillAzimuth(45.0)
      , m_fillElevation(20.0)
      , m_fillIntensity(40.0)
      , m_fillColor(TPixel32::White)
      , m_rimAzimuth(150.0)
      , m_rimElevation(35.0)
      , m_rimIntensity(65.0)
      , m_rimColor(TPixel32::White) {
    // FX scene serialization tokenizes port names at whitespace. The original
    // display-style name "GLB Model" was saved as "GLB", then failed to reload
    // because no port with that serialized name existed. Keep the stable port
    // identifier whitespace-free; the schematic can describe the source type
    // separately from this persistence key.
    addInputPort("GLB", m_source);

    m_lightMode->addItem(1, "Positional");
    m_rigRadius->setValueRange(0.001, 100000.0);
    bindParam(this, "lightMode", m_lightMode);
    bindParam(this, "rigPositionX", m_rigX);
    bindParam(this, "rigPositionY", m_rigY);
    bindParam(this, "rigPositionZ", m_rigZ);
    bindParam(this, "rigRadius", m_rigRadius);

    bindParam(this, "masterIntensity", m_masterIntensity);
    bindParam(this, "ambient", m_ambient);
    bindParam(this, "keyAzimuth", m_keyAzimuth);
    bindParam(this, "keyElevation", m_keyElevation);
    bindParam(this, "keyIntensity", m_keyIntensity);
    bindParam(this, "keyColor", m_keyColor);
    bindParam(this, "fillAzimuth", m_fillAzimuth);
    bindParam(this, "fillElevation", m_fillElevation);
    bindParam(this, "fillIntensity", m_fillIntensity);
    bindParam(this, "fillColor", m_fillColor);
    bindParam(this, "rimAzimuth", m_rimAzimuth);
    bindParam(this, "rimElevation", m_rimElevation);
    bindParam(this, "rimIntensity", m_rimIntensity);
    bindParam(this, "rimColor", m_rimColor);

    for (auto param : {m_keyAzimuth, m_keyElevation, m_fillAzimuth,
                       m_fillElevation, m_rimAzimuth, m_rimElevation})
      param->setMeasureName("angle");
    for (auto param : {m_masterIntensity, m_keyIntensity, m_fillIntensity,
                       m_rimIntensity})
      param->setValueRange(0.0, 1000.0);
    m_ambient->setValueRange(0.0, 100.0);
    for (auto param : {m_keyAzimuth, m_fillAzimuth, m_rimAzimuth})
      param->setValueRange(-360.0, 360.0);
    for (auto param : {m_keyElevation, m_fillElevation, m_rimElevation})
      param->setValueRange(-90.0, 90.0);

    enableComputeInFloat(true);
  }

  std::shared_ptr<const otglb::RenderScene> get3DRenderScene(
      double frame, const int *canceled, const otglb::LightingRig *downstream,
      const std::vector<otglb::ModelTransform> *transforms,
      const TRenderSettings *settings,
      const otam::MaterialBindings *materials = nullptr) const override {
    auto *source = m_source.source();
    if (!source || (canceled && *canceled)) return {};

    auto rig = lighting(frame);
    if (downstream) {
      rig.environments = downstream->environments;
      // Scale each rig by its own master before adding its contribution.
      rig.ambient =
          rig.ambient * rig.master + downstream->ambient * downstream->master;
      for (auto &light : rig.lights) light.intensity *= rig.master;
      rig.master = 1.0;
      for (auto light : downstream->lights) {
        light.intensity *= downstream->master;
        rig.lights.push_back(light);
      }
    }
    return source->get3DRenderScene(frame, canceled, &rig, transforms, settings,
                                    materials);
  }

  std::shared_ptr<const otglb::RenderScene> get3DRenderGeometry(
      double frame, const int *canceled,
      const std::vector<otglb::ModelTransform> *transforms,
      const TRenderSettings *settings) const override {
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
      const auto rendered =
          get3DRenderGeometry(frame, info.m_isCanceled, nullptr, &info);
      if (!rendered || rendered->empty()) return false;
      const auto &b = rendered->bounds;
      bbox = TRectD(b[0], b[1], b[2], b[3]);
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

  bool toBeComputedInLinearColorSpace(bool, bool) const override {
    return false;
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
      request.width = tile.getRaster()->getLx();
      request.height = tile.getRaster()->getLy();
      request.x = tile.m_pos.x;
      request.y = tile.m_pos.y;
      const auto &a = info.m_affine;
      request.affine = {{a.a11, a.a12, a.a13, a.a21, a.a22, a.a23}};
      if (!request.width || !request.height) return;

      const int band = std::max(
          1, std::min(128, 2 * 1024 * 1024 / request.width));
      const int height = request.height;
      for (int y = 0; y < height; y += band) {
        request.height = std::min(band, height - y);
        request.y = tile.m_pos.y + y;
        const auto pixels =
            otglb::renderTile(*rendered, request, info.m_isCanceled);
        if (pixels.empty()) {
          tile.getRaster()->clear();
          return;
        }
        if (TRasterFP raster = tile.getRaster())
          copy3DPixels<TPixelF>(raster, pixels, y);
        else if (TRaster64P raster = tile.getRaster())
          copy3DPixels<TPixel64>(raster, pixels, y);
        else if (TRaster32P raster = tile.getRaster())
          copy3DPixels<TPixel32>(raster, pixels, y);
        else
          throw std::runtime_error("Unsupported 3D output pixel type.");
      }
    } catch (const std::exception &error) {
      throw TException(QString("Three-Point Light [%1]: %2")
                           .arg(QString::fromStdWString(getFxId()),
                                QString::fromUtf8(error.what()))
                           .toStdWString());
    }
  }
};

FX_PLUGIN_IDENTIFIER(ThreePointLightFx, "threePointLightFx")

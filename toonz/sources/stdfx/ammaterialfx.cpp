#include "stdfx.h"
#include "t3dsource.h"
#include "tfxparam.h"
#include "tparamset.h"
#include "tnotanimatableparam.h"
#include "ammaterial.h"
#include "tfxammaterial.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <locale>
#include <type_traits>

namespace {
template <class PIXEL>
void copyAMMaterialPixels(TRasterPT<PIXEL> raster,
                          const std::vector<otglb::ColorPixel> &pixels,
                          int yOffset) {
  using Channel        = typename PIXEL::Channel;
  const double maximum = PIXEL::maxChannelValue;
  raster->lock();
  const int rows = int(pixels.size() / raster->getLx());
  for (int y = 0; y < rows; ++y)
    for (int x = 0; x < raster->getLx(); ++x) {
      const auto p = otglb::outputPixel(
          pixels[std::size_t(y) * raster->getLx() + x], raster->isLinear());
      raster->pixels(y + yOffset)[x] =
          PIXEL(otglb::displayChannel<Channel>(p.r, maximum),
                otglb::displayChannel<Channel>(p.g, maximum),
                otglb::displayChannel<Channel>(p.b, maximum),
                otglb::displayChannel<Channel>(p.alpha, maximum));
    }
  raster->unlock();
}
}  // namespace

class AMMaterialFx final : public TStandardRasterFx,
                           public T3DRenderSource,
                           public TFxAMMaterialSource {
  FX_PLUGIN_DECLARATION(AMMaterialFx)
  T3DSourcePort m_source;
  TStringParamP m_materialFile, m_group;
  TIntEnumParamP m_diffuseSource, m_unsupportedMode;
  TBoolParamP m_overrideSpecular;
  TDoubleParamP m_specularSize, m_specularIntensity;
  TPixelParamP m_specularColor;
  struct Cache {
    QMutex mutex;
    QByteArray digest;
    std::shared_ptr<const otam::Material> material;
  };
  std::shared_ptr<Cache> m_cache = std::make_shared<Cache>();

  QByteArray sourceBytes() const {
    QFile file(QString::fromStdWString(m_materialFile->getValue()));
    if (!file.open(QIODevice::ReadOnly))
      throw std::runtime_error("Cannot read the A:M material file");
    constexpr qint64 limit = 8 * 1024 * 1024;
    if (file.size() <= 0 || file.size() > limit)
      throw std::runtime_error("A:M material is empty or exceeds 8 MiB");
    const auto bytes = file.read(limit + 1);
    if (bytes.size() != file.size() || bytes.size() > limit)
      throw std::runtime_error("A:M material changed while reading");
    return bytes;
  }
  otam::MaterialBinding binding(double frame) const {
    otam::MaterialBinding result;
    if (!m_materialFile->getValue().empty()) {
      const auto bytes = sourceBytes();
      const auto digest =
          QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
      QMutexLocker lock(&m_cache->mutex);
      if (!m_cache->material || m_cache->digest != digest) {
        auto material = std::make_shared<otam::Material>(
            otam::parseMaterial(std::string(bytes.constData(), bytes.size())));
        m_cache->material = std::move(material);
        m_cache->digest   = digest;
      }
      result.material = *m_cache->material;
    }
    if (m_overrideSpecular->getValue()) {
      auto &surface             = result.material.surface;
      surface.specularSize      = m_specularSize->getValue(frame) / 100.0;
      surface.specularIntensity = m_specularIntensity->getValue(frame) / 100.0;
      const auto color          = m_specularColor->getValueD(frame);
      surface.specularColor =
          otam::Color{{float(std::clamp(color.r, 0.0, 1.0)),
                       float(std::clamp(color.g, 0.0, 1.0)),
                       float(std::clamp(color.b, 0.0, 1.0))}};
    }
    result.group =
        QString::fromStdWString(m_group->getValue()).toUtf8().toStdString();
    result.specularOnly       = m_materialFile->getValue().empty();
    result.paletteColor       = m_diffuseSource->getValue() == 1;
    result.previewUnsupported = m_unsupportedMode->getValue() == 1;
    return result;
  }

public:
  AMMaterialFx()
      : m_materialFile(new TStringParam())
      , m_group(new TStringParam())
      , m_diffuseSource(new TIntEnumParam(0, "Material File"))
      , m_unsupportedMode(new TIntEnumParam(0, "Report Error"))
      , m_overrideSpecular(false)
      , m_specularSize(25.0)
      , m_specularIntensity(100.0)
      , m_specularColor(TPixel32::White) {
    addInputPort("Source", m_source);
    m_diffuseSource->addItem(1, "Level Palette");
    m_unsupportedMode->addItem(1, "Supported Attributes Preview");
    bindParam(this, "materialFile", m_materialFile);
    bindParam(this, "group", m_group);
    bindParam(this, "diffuseSource", m_diffuseSource);
    bindParam(this, "unsupportedAttributes", m_unsupportedMode);
    bindParam(this, "overrideSpecular", m_overrideSpecular);
    bindParam(this, "specularSize", m_specularSize);
    bindParam(this, "specularIntensity", m_specularIntensity);
    bindParam(this, "specularColor", m_specularColor);
    m_specularColor->enableMatte(false);
    m_specularSize->setValueRange(0.0, 10000.0);
    m_specularIntensity->setValueRange(0.0, 100000.0);
    enableComputeInFloat(true);
  }
  std::vector<std::wstring> getAMMaterialGroups(double frame) const override {
    auto *source = m_source.source();
    const auto geometry =
        source ? source->get3DRenderGeometry(frame, nullptr) : nullptr;
    std::vector<std::wstring> result;
    if (geometry)
      for (const auto &group : geometry->materialGroups)
        result.push_back(
            QString::fromUtf8(group.data(), int(group.size())).toStdWString());
    return result;
  }
  std::wstring getAMMaterialSummary(double frame) const override {
    const auto evaluated = binding(frame);
    const auto &s        = evaluated.material.surface;
    std::wostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(6);
    out << L"Material: "
        << (m_materialFile->getValue().empty() ? L"Input Surface"
                                               : m_materialFile->getValue())
        << L"\nAssignment: "
        << (evaluated.group.empty()
                ? L"Whole Model"
                : QString::fromUtf8(evaluated.group.c_str()).toStdWString())
        << L"\nSpecular Source: "
        << (m_overrideSpecular->getValue()       ? L"OT3D Override"
            : m_materialFile->getValue().empty() ? L"No Override"
                                                 : L"Material File")
        << L"\nSpecular Size: " << s.specularSize * 100 << L"%"
        << L"\nSpecular Intensity: " << s.specularIntensity * 100 << L"%";
    const auto printColor = [&](const wchar_t *name,
                                const std::optional<otam::Color> &color) {
      out << L"\n" << name << L": ";
      if (color)
        out << (*color)[0] * 255 << L" " << (*color)[1] * 255 << L" "
            << (*color)[2] * 255;
      else
        out << L"Follows Diffuse Color";
    };
    printColor(L"Specular Color (RGB)", s.specularColor);
    if (evaluated.specularOnly) {
      out << L"\nDiffuse / Ambiance / Transparency: Inherited from input"
          << L"\n(Level Palette when no upstream material is assigned.)"
          << L"\nScene reflections: Deferred";
    } else {
      if (evaluated.paletteColor || !s.diffuse)
        out << L"\nDiffuse Color: Level Palette";
      else
        printColor(L"Diffuse Color (RGB)", s.diffuse);
      out << L"\nDiffuse Falloff: " << s.diffuseFalloff * 100 << L"%"
          << L"\nAmbiance: " << s.ambiance * 100 << L"%" << L"\nTransparency: "
          << s.transparency * 100 << L"%" << L"\nReflectivity: "
          << s.reflectivity * 100 << L"%" << L"\nReflective Blend: "
          << s.reflectiveBlend * 100 << L"%"
          << L"\nReflection Falloff Distance: " << s.reflectivityFalloff
          << L" (0 = infinite)"
          << L"\nConnect Environment Reflections FX to evaluate reflections.";
    }
    out << L"\n\nHighlights respond to direct lights and camera position."
        << L"\nExact A:M highlight matching is not yet validated.";
    for (const auto &attribute : evaluated.material.unsupported)
      out << L"\n"
          << (evaluated.previewUnsupported ? L"Preview omits: "
                                           : L"Cannot render: ")
          << QString::fromUtf8(attribute.c_str()).toStdWString();
    return out.str();
  }
  TFx *clone(bool recursive = true) const override {
    auto *result    = static_cast<AMMaterialFx *>(TFx::clone(recursive));
    result->m_cache = m_cache;
    return result;
  }
  std::string getAlias(double frame,
                       const TRenderSettings &info) const override {
    std::string digest = "empty";
    if (!m_materialFile->getValue().empty()) {
      try {
        digest =
            QCryptographicHash::hash(sourceBytes(), QCryptographicHash::Sha256)
                .toHex()
                .toStdString();
      } catch (const std::exception &) {
        digest = "unreadable";
      }
    }
    return TStandardRasterFx::getAlias(frame, info) +
           "[AMMaterial-v2:" + digest + "]";
  }
  std::shared_ptr<const otglb::RenderScene> get3DRenderScene(
      double frame, const int *canceled,
      const otglb::LightingRig *lighting                   = nullptr,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *settings                      = nullptr,
      const otam::MaterialBindings *downstream = nullptr) const override {
    auto *source = m_source.source();
    if (!source || (canceled && *canceled)) return {};
    otam::MaterialBindings materials;
    if (!m_materialFile->getValue().empty() || m_overrideSpecular->getValue())
      materials.push_back(binding(frame));
    if (downstream)
      materials.insert(materials.end(), downstream->begin(), downstream->end());
    if (materials.size() > 64)
      throw std::runtime_error("A:M material chain exceeds 64 assignments");
    return source->get3DRenderScene(frame, canceled, lighting, transforms,
                                    settings,
                                    materials.empty() ? nullptr : &materials);
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
                             256.0 / (1024.0 * 1024.0);
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
          copyAMMaterialPixels<TPixelF>(raster, pixels, y);
        else if (TRaster64P raster = tile.getRaster())
          copyAMMaterialPixels<TPixel64>(raster, pixels, y);
        else if (TRaster32P raster = tile.getRaster())
          copyAMMaterialPixels<TPixel32>(raster, pixels, y);
        else
          throw std::runtime_error("Unsupported 3D output pixel type.");
      }
    } catch (const std::exception &error) {
      throw TException(QString("A:M Material [%1]: %2")
                           .arg(QString::fromStdWString(getFxId()),
                                QString::fromUtf8(error.what()))
                           .toStdWString());
    }
  }
};

FX_PLUGIN_IDENTIFIER(AMMaterialFx, "amMaterialFx")

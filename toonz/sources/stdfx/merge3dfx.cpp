#include "stdfx.h"
#include "t3dsource.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>

namespace {
template <class PIXEL>
void copyMerge3DPixels(TRasterPT<PIXEL> raster,
                       const std::vector<otglb::ColorPixel> &pixels, int y0) {
  using Channel        = typename PIXEL::Channel;
  const double maximum = PIXEL::maxChannelValue;
  raster->lock();
  for (int y = 0; y < int(pixels.size() / raster->getLx()); ++y)
    for (int x = 0; x < raster->getLx(); ++x) {
      const auto &p             = pixels[std::size_t(y) * raster->getLx() + x];
      raster->pixels(y + y0)[x] =
          PIXEL(otglb::displayChannel<Channel>(p.r, maximum),
                otglb::displayChannel<Channel>(p.g, maximum),
                otglb::displayChannel<Channel>(p.b, maximum),
                otglb::displayChannel<Channel>(p.alpha, maximum));
    }
  raster->unlock();
}
}  // namespace

// Keep each source unevaluated until downstream lighting/transforms have been
// forwarded. The raster boundary uses ordinary foreground-over-background.
class Merge3DFx final : public TStandardRasterFx,
                        public T3DRenderSource,
                        public T3DRenderGroup {
  FX_PLUGIN_DECLARATION(Merge3DFx)
  T3DSourcePort m_foreground, m_background;
  TAffine m_affines[2];  // Render-tree-only column placement.

  std::shared_ptr<const otglb::RenderScene> scene(
      double frame, const int *canceled, const otglb::LightingRig *lighting,
      const std::vector<otglb::ModelTransform> *transforms,
      const TRenderSettings *settings, bool geometryOnly,
      const otam::MaterialBindings *materials = nullptr) const {
    auto result                  = std::make_shared<otglb::RenderScene>();
    const T3DSourcePort *ports[] = {&m_foreground, &m_background};
    TRectD bounds;
    for (int i = 1; i >= 0; --i) {
      if (canceled && *canceled) return {};
      auto *source = ports[i]->source();
      if (!source) continue;
      auto child = geometryOnly ? source->get3DRenderGeometry(
                                      frame, canceled, transforms, settings)
                                : source->get3DRenderScene(frame, canceled,
                                                           lighting, transforms,
                                                           settings, materials);
      if (!child || child->empty()) continue;
      const auto &a = m_affines[i];
      const auto &b = child->bounds;
      bounds += a * TRectD(b[0], b[1], b[2], b[3]);
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
  Merge3DFx() {
    addInputPort("Foreground", m_foreground);
    addInputPort("Background", m_background);
    enableComputeInFloat(true);
  }
  void set3DInputAffine(int input, const TAffine &affine) override {
    if (input >= 0 && input < 2) m_affines[input] = affine;
  }
  TFx *clone(bool recursive = true) const override {
    auto *result = static_cast<Merge3DFx *>(TFx::clone(recursive));
    std::copy(std::begin(m_affines), std::end(m_affines), result->m_affines);
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
      const otam::MaterialBindings *materials = nullptr) const override {
    return scene(frame, canceled, lighting, transforms, settings, false,
                 materials);
  }
  std::shared_ptr<const otglb::RenderScene> get3DRenderGeometry(
      double frame, const int *canceled,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *settings = nullptr) const override {
    return scene(frame, canceled, nullptr, transforms, settings, true);
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
  bool toBeComputedInLinearColorSpace(bool, bool) const override {
    return false;
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
      auto raster = tile.getRaster();
      if (!raster->getLx() || !raster->getLy()) return;
      otglb::RenderTile request;
      request.width  = raster->getLx();
      request.x      = tile.m_pos.x;
      const auto &a  = info.m_affine;
      request.affine = {{a.a11, a.a12, a.a13, a.a21, a.a22, a.a23}};
      const int band =
          std::max(1, std::min(128, 2 * 1024 * 1024 / request.width));
      for (int y = 0; y < raster->getLy(); y += band) {
        request.y      = tile.m_pos.y + y;
        request.height = std::min(band, raster->getLy() - y);
        const auto pixels =
            otglb::renderTile(*merged, request, info.m_isCanceled);
        if (pixels.empty()) {
          raster->clear();
          return;
        }
        if (TRasterFP r = raster)
          copyMerge3DPixels<TPixelF>(r, pixels, y);
        else if (TRaster64P r = raster)
          copyMerge3DPixels<TPixel64>(r, pixels, y);
        else if (TRaster32P r = raster)
          copyMerge3DPixels<TPixel32>(r, pixels, y);
        else
          throw std::runtime_error("Unsupported 3D Merge output pixel type.");
      }
    } catch (const std::exception &error) {
      throw TException(QString("3D Merge [%1]: %2")
                           .arg(QString::fromStdWString(getFxId()),
                                QString::fromUtf8(error.what()))
                           .toStdWString());
    }
  }
};

FX_PLUGIN_IDENTIFIER(Merge3DFx, "merge3DFx")

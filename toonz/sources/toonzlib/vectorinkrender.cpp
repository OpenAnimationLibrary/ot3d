#include "toonz/vectorinkrender.h"

#include "t3draster.h"
#include "tcurves.h"
#include "tgl.h"
#include "tregion.h"
#include "tstroke.h"
#include "trop.h"
#include "tvectorimage.h"
#include "toonz/tcolumnfx.h"
#include "toonz/txshcell.h"
#include "toonz/txshlevelcolumn.h"
#include "toonz/txshleveltypes.h"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>

namespace {
double inkPadding(const otam::InkStyle &style) {
  // Includes the largest possible miter and roughness displacement.
  return *std::max_element(style.widths.begin(), style.widths.end()) *
         (style.profile ? 4 : 2);
}

void markPaintedEdges(const TRegion &region, std::vector<bool> &painted,
                      bool parentPainted = false) {
  const bool filled = region.getStyle() != 0;
  if (filled || parentPainted) {
    for (UINT i = 0; i < region.getEdgeCount(); ++i) {
      const int index = region.getEdge(i)->m_index;
      if (index >= 0 && std::size_t(index) < painted.size())
        painted[index] = true;
    }
  }
  for (UINT i = 0; i < region.getSubregionCount(); ++i)
    markPaintedEdges(*region.getSubregion(i), painted, filled);
}

TRect inkRect(const TStroke &stroke, const otam::InkStyle &style,
              const TAffine &affine, const TRect &clip) {
  const double padding = inkPadding(style);
  if (padding == 0) return {};
  const TRectD bounds = affine * stroke.getBBox().enlarge(padding);
  const TRectD clipped =
      bounds * TRectD(clip.x0, clip.y0, clip.x1 + 1, clip.y1 + 1);
  if (clipped.isEmpty()) return {};
  return TRect(tfloor(clipped.x0), tfloor(clipped.y0), tceil(clipped.x1) - 1,
               tceil(clipped.y1) - 1);
}

void renderInkStroke(const TStroke &stroke, UINT index,
                     const otam::InkStyle &style, TTile &tile,
                     const TRenderSettings &info) {
  if (info.m_isCanceled && *info.m_isCanceled) return;

  otam::Mesh mesh;
  auto &spline  = mesh.splines.emplace_back();
  spline.closed = stroke.isSelfLoop();
  spline.curves.reserve(stroke.getChunkCount());
  const auto point = [](const TPointD &p) -> otam::Vec3 {
    return {{p.x, p.y, 0}};
  };
  for (int i = 0; i < stroke.getChunkCount(); ++i) {
    const auto &curve = *stroke.getChunk(i);
    const auto a = curve.getP0(), b = curve.getP1(), c = curve.getP2();
    spline.curves.emplace_back().points = {
        {point(a), point(a + (b - a) * (2.0 / 3)),
         point(c + (b - c) * (2.0 / 3)), point(c)}};
  }
  const auto triangles = otam::strokeSplines(
      mesh, {style}, nullptr, otam::identity(), info.m_isCanceled, true, index);
  if (triangles.empty()) return;

  otglb::RenderScene scene;
  scene.triangles.reserve(triangles.size());
  for (const auto &ink : triangles) {
    auto &triangle = scene.triangles.emplace_back();
    for (int i = 0; i < 3; ++i) {
      const auto &p        = ink.vertices[i];
      triangle.vertices[i] = {p[0], p[1], p[2]};
    }
  }
  scene.sampleShader = [&triangles](std::size_t triangle,
                                    const std::array<double, 3> &bary) {
    const auto &color = triangles[triangle].colors;
    const auto mix    = [&](int channel) {
      return float(bary[0] * color[0][channel] + bary[1] * color[1][channel] +
                   bary[2] * color[2][channel]);
    };
    return otglb::ColorPixel{mix(0), mix(1), mix(2), mix(3)};
  };
  ot3d::renderScene(scene, tile, info);
}
}  // namespace

bool isVectorInkColumn(TFx *fx, std::optional<int> row) {
  const auto *levelFx = dynamic_cast<const TLevelColumnFx *>(fx);
  const auto *column  = levelFx ? levelFx->getColumn() : nullptr;
  if (!column || column->isEmpty()) return false;
  const auto cell = column->getCell(row.value_or(column->getFirstRow()));
  return cell.m_level && cell.m_level->getType() == PLI_XSHLEVEL;
}

TVectorInkRenderData::TVectorInkRenderData(otam::InkStyle style)
    : m_style(std::move(style)) {
  m_style.group.clear();  // Group names are assignments on A:M models.
  std::ostringstream alias;
  alias.imbue(std::locale::classic());
  alias << "vectorInk[" << std::hexfloat << m_style.unfilledOnly << ','
        << m_style.middle << ',' << m_style.roughness << ',' << m_style.fadeIn
        << ',' << m_style.fadeOut;
  for (int i = 0; i < 3; ++i) {
    alias << ',' << m_style.widths[i];
    for (const auto channel : m_style.colors[i]) alias << ',' << channel;
  }
  if (m_style.profile) {
    const auto &profile = *m_style.profile;
    alias << ",profile:" << profile.linear << ',' << profile.perSection;
    for (const auto &span : profile.width)
      for (const auto &point : span)
        alias << ',' << point[0] << ',' << point[1];
    alias << ",ramp:";
    for (const auto &[position, color] : profile.colors) {
      alias << ',' << position;
      for (const auto channel : color) alias << ',' << channel;
    }
  }
  m_alias = alias.str() + "]";
}

TVectorInkRenderData::~TVectorInkRenderData() = default;

bool TVectorInkRenderData::operator==(const TRasterFxRenderData &other) const {
  const auto *ink = dynamic_cast<const TVectorInkRenderData *>(&other);
  return ink && m_alias == ink->m_alias;
}

TRectD TVectorInkRenderData::getBBoxEnlargement(const TRectD &bbox) {
  return bbox.enlarge(inkPadding(m_style));
}

VectorInkRenderer::VectorInkRenderer(const TVectorImage &image,
                                     const TRenderSettings &info) {
  std::vector<const otam::InkStyle *> styles;
  // Downstream FX pass their settings first and take precedence.
  for (const auto &data : info.m_data)
    if (const auto *ink =
            dynamic_cast<const TVectorInkRenderData *>(data.getPointer()))
      styles.push_back(&ink->style());
  if (styles.empty()) return;

  QMutexLocker lock(image.getMutex());
  m_styles.resize(image.getStrokeCount());
  std::vector<bool> painted(m_styles.size(), false);
  if (styles.front()->unfilledOnly)
    for (UINT i = 0; i < image.getRegionCount(); ++i)
      markPaintedEdges(*image.getRegion(i), painted);

  for (UINT i = 0; i < m_styles.size(); ++i) {
    const auto style = std::find_if(
        styles.begin(), styles.end(),
        [&](const auto *ink) { return !ink->unfilledOnly || !painted[i]; });
    if (style != styles.end()) m_styles[i] = *style;
  }
}

void VectorInkRenderer::drawStroke(const TStroke &stroke, UINT index,
                                   const TAffine &affine, const TRect &clip,
                                   int *canceled) const {
  const auto &style = *m_styles[index];
  const TRect rect  = inkRect(stroke, style, affine, clip);
  if (rect.isEmpty()) return;
  TRaster32P raster(rect.getSize());
  raster->clear();
  TTile tile(raster, TPointD(rect.x0, rect.y0));
  TRenderSettings settings;
  settings.m_affine     = affine;
  settings.m_isCanceled = canceled;
  renderInkStroke(stroke, index, style, tile, settings);

  glPushAttrib(GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_PIXEL_MODE_BIT);
  glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, raster->getWrap());
  glRasterPos2i(rect.x0, rect.y0);
  raster->lock();
  glDrawPixels(rect.getLx(), rect.getLy(), TGL_FMT, TGL_TYPE,
               raster->getRawData());
  raster->unlock();
  glPopClientAttrib();
  glPopAttrib();
}

void VectorInkRenderer::overlay(const TVectorImage &image, TTile &tile,
                                const TRenderSettings &info) const {
  const auto output = tile.getRaster();
  auto settings     = info;
  settings.m_affine = TTranslation(-tile.m_pos) * info.m_affine;
  for (UINT i = 0; i < m_styles.size(); ++i) {
    if (info.m_isCanceled && *info.m_isCanceled) return;
    if (!replaces(i)) continue;
    const auto &stroke = *image.getStroke(i);
    const auto &style  = *m_styles[i];
    const auto rect =
        inkRect(stroke, style, settings.m_affine, output->getBounds());
    if (rect.isEmpty()) continue;
    const auto raster = output->create(rect.getLx(), rect.getLy());
    raster->clear();
    TTile inkTile(raster, TPointD(rect.x0, rect.y0));
    renderInkStroke(stroke, i, style, inkTile, settings);
    TRop::over(output, raster, rect.getP00());
  }
}

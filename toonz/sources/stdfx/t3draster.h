#pragma once

#include "glbrenderer.h"
#include "trasterfx.h"
#include "ttile.h"

#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace ot3d {
template <class PIXEL>
void copyRenderPixels(TRasterPT<PIXEL> raster,
                      const std::vector<otglb::ColorPixel> &pixels,
                      int yOffset) {
  using Channel        = typename PIXEL::Channel;
  const double maximum = PIXEL::maxChannelValue;
  const double round   = std::is_floating_point<Channel>::value ? 0.0 : 0.5;
  raster->lock();
  const int rows = int(pixels.size() / raster->getLx());
  for (int y = 0; y < rows; ++y)
    for (int x = 0; x < raster->getLx(); ++x) {
      const auto &p = pixels[std::size_t(y) * raster->getLx() + x];
      raster->pixels(y + yOffset)[x] = PIXEL(
          Channel(p.r * maximum + round), Channel(p.g * maximum + round),
          Channel(p.b * maximum + round), Channel(p.alpha * maximum + round));
    }
  raster->unlock();
}
inline void renderScene(const otglb::RenderScene &scene, TTile &tile,
                        const TRenderSettings &info) {
  auto output = tile.getRaster();
  otglb::RenderTile request;
  request.width  = output->getLx();
  request.height = output->getLy();
  request.x      = tile.m_pos.x;
  request.y      = tile.m_pos.y;
  const auto &a  = info.m_affine;
  request.affine = {{a.a11, a.a12, a.a13, a.a21, a.a22, a.a23}};
  if (!request.width || !request.height) return;

  const int band = std::max(1, std::min(128, 2 * 1024 * 1024 / request.width));
  const int height = request.height;
  for (int y = 0; y < height; y += band) {
    request.height    = std::min(band, height - y);
    request.y         = tile.m_pos.y + y;
    const auto pixels = otglb::renderTile(scene, request, info.m_isCanceled);
    if (pixels.empty()) {
      output->clear();
      return;
    }
    if (TRasterFP raster = output)
      copyRenderPixels<TPixelF>(raster, pixels, y);
    else if (TRaster64P raster = output)
      copyRenderPixels<TPixel64>(raster, pixels, y);
    else if (TRaster32P raster = output)
      copyRenderPixels<TPixel32>(raster, pixels, y);
    else
      throw std::runtime_error("Unsupported 3D output pixel type.");
  }
}
}  // namespace ot3d

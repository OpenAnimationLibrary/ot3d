#pragma once

#include "tgeometry.h"
#include "tvectorimage.h"

#include <QByteArray>

#include <map>
#include <utility>
#include <vector>

#undef DVAPI
#ifdef TOONZLIB_EXPORTS
#define DVAPI DV_EXPORT_API
#else
#define DVAPI DV_IMPORT_API
#endif

class TXsheet;

struct VectorAction {
  using Point = std::pair<int, int>;  // Stroke index, raw control point index.
  using Keys  = std::map<double, TPointD>;
  using Offsets = std::map<Point, TPointD>;

  static constexpr int MaxBytes = 16 * 1024 * 1024;

  // Ordered control point counts. A negative count denotes a closed stroke.
  std::vector<int> topology;
  std::map<Point, Keys> channels;

  DVAPI static VectorAction forDrawing(const TVectorImageP &image);
  DVAPI bool matches(const TVectorImageP &image) const;
  DVAPI QByteArray write() const;
  DVAPI static VectorAction read(const QByteArray &bytes);
  DVAPI TPointD sample(Point point, double seconds) const;
  DVAPI TVectorImageP pose(const TVectorImageP &image, double seconds) const;
  DVAPI void setKeys(double seconds, const Offsets &offsets);
  DVAPI void replaceKeys(Point point, Keys keys);
};

DVAPI double vectorActionTime(const TXsheet *xsheet, int startRow,
                              double frame);
DVAPI double vectorActionFrame(const TXsheet *xsheet, int startRow,
                               double seconds);
DVAPI TVectorImageP offsetVectorDrawing(const TVectorImageP &image,
                                        const VectorAction::Offsets &offsets);

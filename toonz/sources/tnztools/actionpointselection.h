#pragma once

#include "tgeometry.h"

#include <QPolygonF>

#include <optional>
#include <vector>

class ActionPointSelectionRegion {
  TPointD m_start, m_end;
  QPolygonF m_path;
  bool m_freehand;

public:
  ActionPointSelectionRegion(const TPointD &start, bool freehand);
  void update(const TPointD &pos);
  bool contains(const TPointD &point) const;
  void draw() const;
};

struct ActionPointTransform {
  enum class Mode { Rotate, Scale };
  Mode mode;
  TPointD pivot, handle;

  TAffine matrix(const TPointD &drag, bool uniform) const;
};

class ActionPointSelectionBox {
  TRectD m_bounds;
  TPointD m_pivot, m_rotationHandle;
  double m_pixel;

public:
  ActionPointSelectionBox(const std::vector<TPointD> &points, double pixel);
  std::optional<ActionPointTransform> hit(const TPointD &pos) const;
  bool contains(const TPointD &pos) const { return m_bounds.contains(pos); }
  void draw() const;
};

#include "actionpointselection.h"

#include "tgl.h"

#include <algorithm>
#include <cmath>

ActionPointSelectionRegion::ActionPointSelectionRegion(const TPointD &start,
                                                       bool freehand)
    : m_start(start), m_end(start), m_freehand(freehand) {
  if (m_freehand) m_path << QPointF(start.x, start.y);
}

void ActionPointSelectionRegion::update(const TPointD &pos) {
  if (pos == m_end) return;
  m_end = pos;
  if (m_freehand) m_path << QPointF(pos.x, pos.y);
}

bool ActionPointSelectionRegion::contains(const TPointD &point) const {
  if (m_freehand)
    return m_path.size() >= 3 &&
           m_path.containsPoint(QPointF(point.x, point.y), Qt::OddEvenFill);
  return m_end != m_start && TRectD(m_start, m_end).contains(point);
}

void ActionPointSelectionRegion::draw() const {
  if (!m_freehand) {
    tglDrawRect(TRectD(m_start, m_end));
    return;
  }
  glBegin(GL_LINE_LOOP);
  for (const auto &point : m_path) glVertex2d(point.x(), point.y());
  glEnd();
}

TAffine ActionPointTransform::matrix(const TPointD &drag, bool uniform) const {
  if (drag == TPointD()) return TAffine();
  const auto start = handle - pivot;
  const auto end   = start + drag;
  if (mode == Mode::Rotate) {
    const double cross = start.x * end.y - start.y * end.x;
    return TRotation(pivot, rad2degree(std::atan2(cross, start * end)));
  }
  if (uniform) {
    const double scale = (end * start) / norm2(start);
    return TScale(pivot, scale);
  }
  return TScale(pivot, end.x / start.x, end.y / start.y);
}

ActionPointSelectionBox::ActionPointSelectionBox(
    const std::vector<TPointD> &points, double pixel)
    : m_pixel(pixel) {
  const auto &first = points.front();
  m_bounds          = TRectD(first, first);
  for (const auto &point : points) {
    m_bounds.x0 = std::min(m_bounds.x0, point.x);
    m_bounds.y0 = std::min(m_bounds.y0, point.y);
    m_bounds.x1 = std::max(m_bounds.x1, point.x);
    m_bounds.y1 = std::max(m_bounds.y1, point.y);
  }
  const double margin = 8 * pixel;
  m_bounds.x0 -= margin;
  m_bounds.y0 -= margin;
  m_bounds.x1 += margin;
  m_bounds.y1 += margin;
  m_pivot          = (m_bounds.getP00() + m_bounds.getP11()) * 0.5;
  m_rotationHandle = {m_pivot.x, m_bounds.y1 + 16 * pixel};
}

std::optional<ActionPointTransform> ActionPointSelectionBox::hit(
    const TPointD &pos) const {
  const double radius2 = sq(6 * m_pixel);
  if (norm2(pos - m_rotationHandle) <= radius2)
    return ActionPointTransform{ActionPointTransform::Mode::Rotate, m_pivot,
                                m_rotationHandle};
  for (const auto &corner : {m_bounds.getP00(), m_bounds.getP10(),
                             m_bounds.getP01(), m_bounds.getP11()})
    if (norm2(pos - corner) <= radius2)
      return ActionPointTransform{ActionPointTransform::Mode::Scale, m_pivot,
                                  corner};
  return {};
}

void ActionPointSelectionBox::draw() const {
  tglColor(TPixel32(123, 232, 229));
  tglDrawRect(m_bounds);
  tglDrawSegment(TPointD(m_rotationHandle.x, m_bounds.y1), m_rotationHandle);
  const double radius = 3.5 * m_pixel;
  tglDrawCircle(m_rotationHandle, 4.5 * m_pixel);
  for (const auto &corner : {m_bounds.getP00(), m_bounds.getP10(),
                             m_bounds.getP01(), m_bounds.getP11()})
    tglFillRect(corner.x - radius, corner.y - radius, corner.x + radius,
                corner.y + radius);
}

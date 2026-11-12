#include "vectoractioneditor.h"

#include "tools/tool.h"
#include "tools/toolutils.h"
#include "drawutil.h"
#include "texception.h"
#include "tstroke.h"
#include "toonzqt/dvdialog.h"
#include "toonz/tcolumnhandle.h"
#include "toonz/tframehandle.h"
#include "toonz/tobjecthandle.h"
#include "toonz/txshcell.h"
#include "toonz/txsheet.h"
#include "toonz/txsheethandle.h"

#include <QCoreApplication>
#include <QKeyEvent>

namespace {

int editablePointCount(const TStroke &stroke) {
  return stroke.getControlPointCount() - (stroke.isSelfLoop() ? 1 : 0);
}

TRectD selectionRect(const TPointD &a, const TPointD &b) {
  return TRectD(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x),
                std::max(a.y, b.y));
}

TPointD arrowDelta(int key) {
  switch (key) {
  case Qt::Key_Left:
    return TPointD(-1, 0);
  case Qt::Key_Right:
    return TPointD(1, 0);
  case Qt::Key_Up:
    return TPointD(0, 1);
  case Qt::Key_Down:
    return TPointD(0, -1);
  default:
    return {};
  }
}

}  // namespace

bool VectorActionEditor::refresh(bool sourceChanged) {
  auto *app       = TTool::getApplication();
  auto *frame     = app->getCurrentFrame();
  auto *xsheet    = app->getCurrentXsheet()->getXsheet();
  const int index = app->getCurrentColumn()->getColumnIndex();
  const int row   = frame->getFrame();
  if (!xsheet || index < 0 || row < 0 || frame->isEditingLevel() ||
      app->getCurrentObject()->isSpline()) {
    reset();
    return false;
  }

  auto *column       = xsheet->getColumn(index);
  auto *owner        = column ? column->getLevelColumn() : nullptr;
  const auto binding = owner ? owner->getVectorAction() : VectorActionBinding();
  const auto cell    = xsheet->getCell(row, index);
  if (!binding.matches(cell)) {
    reset();
    return false;
  }

  if (!sourceChanged && owner == m_column.getPointer() &&
      binding.revision == m_binding.revision && row == m_row)
    return true;

  clearGesture();
  if (owner != m_column.getPointer() || binding.level != m_binding.level ||
      binding.fid != m_binding.fid || !m_binding.action ||
      binding.action->topology != m_binding.action->topology) {
    m_selected.clear();
    m_hoveredStroke = -1;
  }
  m_column  = owner;
  m_binding = binding;
  m_row     = row;
  const TVectorImageP source(cell.getImage(false));
  if (binding.action->matches(source)) {
    m_pose = binding.action->pose(
        source, vectorActionTime(xsheet, binding.startRow, row));
  } else {
    m_pose = TVectorImageP();
    m_selected.clear();
    m_hoveredStroke = -1;
  }
  return true;
}

void VectorActionEditor::clearGesture() {
  if (m_dragPose && m_column) m_column->setVectorActionPreview(0, 0, TImageP());
  m_dragPose = TVectorImageP();
  m_dragging = m_boxSelecting = false;
  m_boxSelection.clear();
}

void VectorActionEditor::reset() {
  clearGesture();
  m_column  = TXshLevelColumnP();
  m_binding = VectorActionBinding();
  m_pose    = TVectorImageP();
  m_selected.clear();
  m_row = m_hoveredStroke = -1;
}

std::optional<VectorActionEditor::Point> VectorActionEditor::hit(
    const TPointD &pos) const {
  if (!m_pose) return {};
  double best = sq(7 * m_tool.getPixelSize());
  std::optional<Point> nearest;
  for (UINT s = 0; s < m_pose->getStrokeCount(); ++s) {
    const auto &stroke = *m_pose->getStroke(s);
    for (int i = 0; i < editablePointCount(stroke); ++i) {
      const double distance = norm2(stroke.getControlPoint(i) - pos);
      if (distance < best ||
          (nearest && distance == best && i % 2 == 0 && nearest->second % 2)) {
        nearest = Point(int(s), i);
        best    = distance;
      }
    }
  }
  return nearest;
}

void VectorActionEditor::mouseMove(const TPointD &pos) {
  m_hoveredStroke = -1;
  if (const auto point = hit(pos)) {
    m_hoveredStroke = point->first;
  } else if (m_pose && m_pose->getStrokeCount()) {
    double parameter, distance;
    UINT stroke;
    if (m_pose->getNearestStroke(pos, parameter, stroke, distance) &&
        distance < sq(8 * m_tool.getPixelSize()))
      m_hoveredStroke = int(stroke);
  }
  m_tool.invalidate();
}

void VectorActionEditor::draw() {
  if (!m_pose || TTool::getApplication()->getCurrentFrame()->isPlaying())
    return;
  const auto &pose = m_dragPose ? m_dragPose : m_pose;
  std::set<int> strokes;
  if (m_hoveredStroke >= 0) strokes.insert(m_hoveredStroke);
  for (const auto &point : m_selected) strokes.insert(point.first);

  const double pixel = m_tool.getPixelSize();
  for (int s : strokes) {
    const auto &stroke = *pose->getStroke(s);
    tglColor(TPixel32(156, 130, 255));
    for (int i = 1; i < editablePointCount(stroke); i += 2) {
      tglDrawSegment(stroke.getControlPoint(i - 1), stroke.getControlPoint(i));
      tglDrawSegment(stroke.getControlPoint(i), stroke.getControlPoint(i + 1));
    }
    for (int i = 0; i < editablePointCount(stroke); ++i) {
      const auto point    = stroke.getControlPoint(i);
      const double radius = (i % 2 ? 3.0 : 4.5) * pixel;
      tglColor(m_selected.count({s, i}) ? TPixel32(109, 79, 230)
                                        : TPixel32::White);
      tglFillRect(point.x - radius, point.y - radius, point.x + radius,
                  point.y + radius);
    }
  }
  if (m_boxSelecting)
    ToolUtils::drawRect(selectionRect(m_press, m_cursor), TPixel32::Black,
                        0x3F33, true);
}

bool VectorActionEditor::canKey() const {
  return m_pose && m_row >= m_binding.startRow && !m_column->isLocked() &&
         !TTool::getApplication()->getCurrentFrame()->isPlaying();
}

void VectorActionEditor::press(const TPointD &pos, const TMouseEvent &event) {
  if (!m_pose) return;
  clearGesture();
  m_press = m_cursor = pos;
  const auto point   = hit(pos);
  if (!point) {
    if (!event.isCtrlPressed()) m_selected.clear();
    m_boxSelection = m_selected;
    m_boxSelecting = true;
  } else if (event.isCtrlPressed() && m_selected.erase(*point)) {
    m_hoveredStroke = point->first;
  } else {
    if (!event.isCtrlPressed() && !m_selected.count(*point)) m_selected.clear();
    m_selected.insert(*point);
    m_hoveredStroke = point->first;
    m_dragging      = canKey();
  }
  m_tool.invalidate();
}

VectorAction::Offsets VectorActionEditor::movement(const TPointD &delta) const {
  VectorAction::Offsets offsets;
  if (delta == TPointD()) return offsets;
  for (const auto &[stroke, index] : m_selected) {
    offsets[{stroke, index}] = delta;
    if (index % 2) continue;

    const auto &curve = *m_pose->getStroke(stroke);
    const int count   = editablePointCount(curve);
    const int before = index == 0 && curve.isSelfLoop() ? count - 1 : index - 1;
    // Moving both anchors translates their shared handle by the full delta.
    for (int handle : {before, index + 1})
      if (handle >= 0 && handle < count && !m_selected.count({stroke, handle}))
        offsets[{stroke, handle}] += delta * 0.5;
  }
  return offsets;
}

void VectorActionEditor::drag(const TPointD &pos) {
  m_cursor = pos;
  if (m_dragging) {
    m_dragPose = offsetVectorDrawing(m_pose, movement(pos - m_press));
    m_column->setVectorActionPreview(m_binding.revision, m_row, m_dragPose);
  } else if (m_boxSelecting) {
    m_selected      = m_boxSelection;
    const auto rect = selectionRect(m_press, pos);
    for (UINT s = 0; s < m_pose->getStrokeCount(); ++s) {
      const auto &stroke = *m_pose->getStroke(s);
      for (int i = 0; i < editablePointCount(stroke); i += 2)
        if (rect.contains(stroke.getControlPoint(i))) m_selected.emplace(s, i);
    }
  }
  m_tool.invalidate();
}

void VectorActionEditor::commit(const VectorAction::Offsets &deltas) {
  if (deltas.empty() || !canKey()) return;
  auto *app = TTool::getApplication();
  const double time =
      vectorActionTime(m_column->getXsheet(), m_binding.startRow, m_row);
  auto action = std::make_shared<VectorAction>(*m_binding.action);
  auto keys   = deltas;
  for (auto &[point, offset] : keys) offset += action->sample(point, time);
  try {
    action->setKeys(time, keys);
    auto binding   = m_binding;
    binding.action = std::move(action);
    changeVectorActionBinding(m_column.getPointer(), binding,
                              app->getCurrentScene(), app->getCurrentXsheet(),
                              QObject::tr("Key Vector Action Control Points"));
  } catch (const TException &error) {
    DVGui::warning(QString::fromStdWString(error.getMessage()));
  }
  refresh();
}

void VectorActionEditor::release(const TPointD &pos) {
  const auto offsets =
      m_dragging ? movement(pos - m_press) : VectorAction::Offsets();
  clearGesture();
  commit(offsets);
  m_tool.invalidate();
}

bool VectorActionEditor::acceptsKey(const QKeyEvent &event) const {
  return !m_selected.empty() && canKey() &&
         event.modifiers() == Qt::NoModifier &&
         arrowDelta(event.key()) != TPointD();
}

bool VectorActionEditor::keyDown(const QKeyEvent &event) {
  if (!acceptsKey(event)) return false;
  commit(movement(arrowDelta(event.key())));
  m_tool.invalidate();
  return true;
}

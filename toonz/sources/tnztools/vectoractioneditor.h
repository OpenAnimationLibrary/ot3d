#pragma once

#include "toonz/txshlevelcolumn.h"
#include "toonz/vectoraction.h"

#include <optional>
#include <set>

class TTool;
class TMouseEvent;
class QKeyEvent;

class VectorActionEditor {
  using Point = VectorAction::Point;

  TTool &m_tool;
  TXshLevelColumnP m_column;
  VectorActionBinding m_binding;
  TVectorImageP m_pose, m_dragPose;
  std::set<Point> m_selected, m_boxSelection;
  int m_row = -1, m_hoveredStroke = -1;
  bool m_dragging = false, m_boxSelecting = false;
  TPointD m_press, m_cursor;

  std::optional<Point> hit(const TPointD &pos) const;
  bool canKey() const;
  VectorAction::Offsets movement(const TPointD &delta) const;
  void commit(const VectorAction::Offsets &deltas);
  void clearGesture();

public:
  explicit VectorActionEditor(TTool &tool) : m_tool(tool) {}
  bool refresh(bool sourceChanged = false);
  void reset();
  void draw();
  void mouseMove(const TPointD &pos);
  void press(const TPointD &pos, const TMouseEvent &event);
  void drag(const TPointD &pos);
  void release(const TPointD &pos);
  bool acceptsKey(const QKeyEvent &event) const;
  bool keyDown(const QKeyEvent &event);
};

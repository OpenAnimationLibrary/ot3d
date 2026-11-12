#pragma once

#include "actionpointselection.h"
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
  std::set<Point> m_selected;
  std::optional<ActionPointSelectionRegion> m_selectionRegion;
  std::optional<ActionPointTransform> m_transform;
  bool m_addToSelection = false;
  int m_row = -1, m_hoveredStroke = -1;
  bool m_dragging = false;
  TPointD m_press;

  std::optional<Point> hit(const TPointD &pos) const;
  bool canKey() const;
  std::optional<ActionPointSelectionBox> selectionBox(
      const TVectorImageP &pose) const;
  VectorAction::Offsets movement(const TAffine &transform) const;
  VectorAction::Offsets dragOffsets(const TPointD &pos, bool uniform) const;
  void commit(const VectorAction::Offsets &deltas);
  void clearGesture();

public:
  explicit VectorActionEditor(TTool &tool) : m_tool(tool) {}
  bool refresh(bool sourceChanged = false);
  void reset();
  void draw();
  void mouseMove(const TPointD &pos);
  void press(const TPointD &pos, const TMouseEvent &event, bool freehand);
  void drag(const TPointD &pos, bool uniform);
  void release(const TPointD &pos, bool uniform);
  bool acceptsKey(const QKeyEvent &event) const;
  bool keyDown(const QKeyEvent &event);
};

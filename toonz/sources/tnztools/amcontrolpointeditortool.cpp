#include "amaction.h"
#include "tools/tool.h"
#include "tools/toolcommandids.h"
#include "toonz/amlevelrender.h"
#include "toonz/stage2.h"
#include "toonz/tcolumnhandle.h"
#include "toonz/tframehandle.h"
#include "toonz/toonzscene.h"
#include "toonz/tscenehandle.h"
#include "toonz/txshamlevel.h"
#include "toonz/txshlevelcolumn.h"
#include "toonz/txsheethandle.h"
#include "toonzqt/selection.h"
#include "historytypes.h"
#include "tgl.h"
#include "tundo.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMenu>
#include <QOpenGLContext>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace {
using otam::Vec3;
using PointValues     = TXshAMLevel::PointValues;
using HeadSelection   = std::set<std::size_t>;
using PositionOffsets = std::map<otam::Id, Vec3>;

Vec3 offsetFrom(const Vec3 &position, const Vec3 &base) {
  return {
      {position[0] - base[0], position[1] - base[1], position[2] - base[2]}};
}

void notifyModelChanged(const TXshAMLevel &level) {
  auto *app = TTool::getApplication();
  if (app->getCurrentScene()->getScene() != level.getScene()) return;
  app->getCurrentScene()->notifySceneChanged();
  app->getCurrentXsheet()->notifyXsheetChanged();
}

class UndoAMPoints final : public TUndo {
  TXshAMLevelP m_level;
  PointValues m_before, m_after;
  std::string m_revision;
  bool m_bias;

  void apply(const PointValues &values) const {
    if (m_level->setControlPoints(values, m_revision))
      notifyModelChanged(*m_level);
  }

public:
  UndoAMPoints(TXshAMLevelP level, PointValues before, PointValues after,
               std::string revision, bool bias)
      : m_level(level)
      , m_before(std::move(before))
      , m_after(std::move(after))
      , m_revision(std::move(revision))
      , m_bias(bias) {}

  int getSize() const override {
    return int(sizeof(*this) + m_revision.size() +
               (m_before.size() + m_after.size()) *
                   sizeof(PointValues::value_type));
  }
  void undo() const override { apply(m_before); }
  void redo() const override { apply(m_after); }
  QString getHistoryString() override {
    if (m_bias) return QObject::tr("Adjust A:M Spline Bias");
    return m_before.size() > 1 ? QObject::tr("Move A:M Control Points")
                               : QObject::tr("Move A:M Control Point");
  }
  int getHistoryType() override { return HistoryType::ControlPointEditorTool; }
};

bool sameBinding(const AMActionBinding &a, const AMActionBinding &b) {
  return a.level == b.level && a.path == b.path && a.startRow == b.startRow &&
         a.action == b.action;
}

class UndoAMPositionKey final : public TUndo {
  TXshLevelColumnP m_column;
  AMActionBinding m_before, m_after;
  bool m_removed, m_multiple;

  void apply(const AMActionBinding &expected,
             const AMActionBinding &replacement) const {
    auto *level = replacement.level->getAMLevel();
    auto *scene = TTool::getApplication()->getCurrentScene()->getScene();
    if (!level || level->getScene() != scene ||
        !sameBinding(m_column->getAMAction(), expected))
      return;
    m_column->setAMAction(replacement);
    notifyModelChanged(*level);
  }

public:
  UndoAMPositionKey(TXshLevelColumnP column, AMActionBinding before,
                    AMActionBinding after, bool removed, bool multiple)
      : m_column(std::move(column))
      , m_before(std::move(before))
      , m_after(std::move(after))
      , m_removed(removed)
      , m_multiple(multiple) {}

  int getSize() const override {
    std::size_t bytes = sizeof(*this) + m_after.action->defaultModel.size();
    for (const auto &[id, channel] : m_after.action->channels)
      bytes += sizeof(id) + sizeof(channel) +
               channel.keys.size() * (sizeof(double) + sizeof(Vec3));
    return int(bytes);
  }

  void undo() const override { apply(m_after, m_before); }
  void redo() const override { apply(m_before, m_after); }
  QString getHistoryString() override {
    if (m_removed)
      return m_multiple ? QObject::tr("Remove A:M Position Keys")
                        : QObject::tr("Remove A:M Position Key");
    return m_multiple ? QObject::tr("Set A:M Position Keys")
                      : QObject::tr("Set A:M Position Key");
  }
  int getHistoryType() override { return HistoryType::ControlPointEditorTool; }
};

struct ActionEdit {
  TXshLevelColumnP column;
  AMActionBinding before, after;
  std::shared_ptr<const otam::Model> rest;
  double time;

  bool replace(const AMActionBinding &replacement) {
    if (!sameBinding(column->getAMAction(), after)) return false;
    column->setAMAction(replacement);
    after = replacement;
    return true;
  }

  bool setPositions(const PositionOffsets &offsets) {
    try {
      auto action = std::make_shared<otam::Action>(*before.action);
      otam::setPositionKeys(*action, *rest, offsets, time);
      auto updated   = before;
      updated.action = std::move(action);
      return replace(updated);
    } catch (const std::exception &error) {
      qWarning() << "A:M position keys:" << error.what();
      return false;
    }
  }

  bool removeKeys(const std::vector<otam::Id> &ids) {
    auto action = std::make_shared<otam::Action>(*before.action);
    bool changed = false;
    for (const auto id : ids) {
      auto track = action->channels.find(id);
      if (track == action->channels.end() || !track->second.keys.erase(time))
        continue;
      changed = true;
      if (track->second.keys.empty()) action->channels.erase(track);
    }
    if (!changed) return false;
    auto updated   = before;
    updated.action = std::move(action);
    return replace(updated);
  }

  void finish(bool multiple, bool removed = false) const {
    if (before.action == after.action ||
        !sameBinding(column->getAMAction(), after))
      return;
    TUndoManager::manager()->add(
        new UndoAMPositionKey(column, before, after, removed, multiple));
    notifyModelChanged(*after.level->getAMLevel());
  }
};

bool hasPositionKey(const AMActionBinding &binding, otam::Id id, double time) {
  if (!binding.action || time < 0) return false;
  const auto track = binding.action->channels.find(id);
  return track != binding.action->channels.end() &&
         track->second.keys.count(time);
}

// The Viewer uses an orthographic GL viewport, even when the model itself is
// projected through the scene camera. Keep eye Z to drag at the picked depth.
struct Projection {
  otam::Matrix modelToEye, eyeToModel, filmToScreen, screenToFilm;
  bool perspective;

  std::optional<Vec3> project(const Vec3 &point) const {
    auto eye = otam::transform(modelToEye, point);
    if (perspective) {
      if (eye[2] > -1) return {};
      const double scale = -1000.0 / eye[2];
      eye[0] *= scale;
      eye[1] *= scale;
    } else if (std::abs(eye[2]) > 4000.0) {
      return {};
    }
    const auto screen = otam::transform(filmToScreen, eye);
    if (!std::all_of(screen.begin(), screen.end(),
                     [](double n) { return std::isfinite(n); }))
      return {};
    return screen;
  }

  Vec3 unproject(const TPointD &point, double depth) const {
    auto film = otam::transform(screenToFilm, {{point.x, point.y, depth}});
    if (perspective) {
      const double scale = -depth / 1000.0;
      film[0] *= scale;
      film[1] *= scale;
    }
    return otam::transform(eyeToModel, film);
  }
};

Projection modelProjection(TXsheet *xsheet, int column, int row, bool view3D) {
  // Use the exact matrix in effect for the Viewer tool overlay, including
  // camera stand rotation, pan, zoom and device-pixel centering.
  double glMatrix[16];
  glGetDoublev(GL_MODELVIEW_MATRIX, glMatrix);
  otam::Matrix viewer;
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c) viewer[4 * r + c] = glMatrix[4 * c + r];

  auto modelToEye   = amModelPlacement(xsheet, column, row);
  auto filmToScreen = otam::identity();
  if (view3D) {
    modelToEye = otam::multiply(viewer, modelToEye);
  } else {
    const auto camera = amModelCamera(xsheet, row);
    modelToEye        = otam::multiply(otam::inverse(camera.pose), modelToEye);
    filmToScreen      = otam::multiply(viewer, camera.imageToStage);
  }
  return {modelToEye, otam::inverse(modelToEye), filmToScreen,
          otam::inverse(filmToScreen), !view3D};
}

TPointD xy(const Vec3 &p) { return {p[0], p[1]}; }

class AMControlPointEditor final : public TTool {
  struct Target {
    TXshAMLevelP level;
    int row = -1, column = -1;
    TXsheet *xsheet = nullptr;

    bool operator==(const Target &other) const {
      return level.getPointer() == other.level.getPointer() &&
             row == other.row && column == other.column &&
             xsheet == other.xsheet;
    }
  };

  struct Handle {
    otam::BiasHandle geometry;
    Vec3 screen;
    TPointD marker;
    bool incoming;
  };

  struct Drag {
    Target target;
    Projection projection;
    std::string revision;
    PointValues before, after;
    TPointD mouseStart;
    Vec3 screenStart, anchor;
    std::optional<Handle> handle;
    std::optional<ActionEdit> action;
    std::vector<Vec3> restPositions;
  };

  struct SelectionBox {
    Target target;
    TPointD start, end;
    bool add;

    TRectD rect() const {
      return TRectD(std::min(start.x, end.x), std::min(start.y, end.y),
                    std::max(start.x, end.x), std::max(start.y, end.y));
    }
  };

  Target m_drawTarget;
  const TToolViewer *m_drawViewer = nullptr;
  std::string m_revision;
  std::optional<Projection> m_projection;
  HeadSelection m_selectedHeads;
  std::optional<std::size_t> m_occurrence;
  std::optional<SelectionBox> m_selectionBox;
  std::vector<Handle> m_handles;
  std::optional<Drag> m_drag;
  QElapsedTimer m_updateTimer;

  Target target(int row, int column) const {
    auto *app = getApplication();
    if (column < 0 || app->getCurrentFrame()->isEditingLevel() ||
        app->getCurrentFrame()->isPlaying() ||
        CameraTestCheck::instance()->isEnabled())
      return {};
    auto *xsheet = app->getCurrentXsheet()->getXsheet();
    auto *scene  = app->getCurrentScene()->getScene();
    if (!scene || !xsheet || xsheet != scene->getTopXsheet()) return {};
    const auto *xcol = xsheet->getColumn(column);
    if (!xcol || xcol->isLocked() || !xcol->isCamstandVisible() ||
        !xcol->getOpacity())
      return {};
    const auto cell = xsheet->getCell(row, column);
    auto *level     = cell.isEmpty() ? nullptr : cell.m_level->getAMLevel();
    return {TXshAMLevelP(level), row, column, xsheet};
  }

  Target currentTarget() const {
    auto *app = getApplication();
    return target(app->getCurrentFrame()->getFrame(),
                  app->getCurrentColumn()->getColumnIndex());
  }

  static TXshLevelColumn *levelColumn(const Target &target) {
    auto *column =
        target.xsheet ? target.xsheet->getColumn(target.column) : nullptr;
    return column ? column->getLevelColumn() : nullptr;
  }

  static AMActionBinding actionBinding(const Target &target) {
    auto *column = levelColumn(target);
    if (!column) return {};
    const auto binding = column->getAMAction();
    return binding.level.getPointer() == target.level.getPointer()
               ? binding
               : AMActionBinding{};
  }

  static double keyTime(const Target &target, const AMActionBinding &binding) {
    if (!binding.action || target.row < binding.startRow) return -1;
    try {
      return otam::canonicalActionTime(
          amActionTime(target.xsheet, binding.startRow, target.row));
    } catch (const std::exception &) {
      return -1;
    }
  }

  static std::optional<ActionEdit> beginActionEdit(const Target &target) {
    const auto binding = actionBinding(target);
    const double time  = keyTime(target, binding);
    if (time < 0) return {};
    auto rest = target.level->editableModel();
    if (!rest) return {};
    try {
      otam::validateAction(*binding.action, *rest);
      return ActionEdit{TXshLevelColumnP(levelColumn(target)), binding, binding,
                        std::move(rest), time};
    } catch (const std::exception &) {
      return {};
    }
  }

  void editSelectedKeys(bool remove) {
    const auto active = currentTarget();
    if (m_selectedHeads.empty() || !active.level || !(m_drawTarget == active) ||
        m_revision != active.level->revision())
      return;
    auto edit = beginActionEdit(active);
    if (!edit) return;
    const auto model = amModelAt(active.xsheet, active.column, active.row);
    if (!model || *m_selectedHeads.rbegin() >= model->points.size()) return;

    bool changed;
    if (remove) {
      std::vector<otam::Id> ids;
      for (auto head : m_selectedHeads) ids.push_back(model->points[head].id);
      changed = edit->removeKeys(ids);
    } else {
      PositionOffsets offsets;
      for (auto head : m_selectedHeads) {
        const auto &point = model->points[head];
        if (hasPositionKey(edit->before, point.id, edit->time)) continue;
        offsets.emplace(
            point.id,
            offsetFrom(point.position, edit->rest->points[head].position));
      }
      changed = !offsets.empty() && edit->setPositions(offsets);
    }
    if (changed) {
      edit->finish(m_selectedHeads.size() > 1, remove);
      invalidate();
    }
  }

  void setSelection(HeadSelection heads) {
    if (m_selectedHeads == heads) return;
    m_selectedHeads = std::move(heads);
    m_occurrence.reset();
    if (m_selectedHeads.size() == 1) m_occurrence = *m_selectedHeads.begin();
    m_handles.clear();
  }

  void finishBox(const TPointD &pos) {
    auto box = std::move(*m_selectionBox);
    m_selectionBox.reset();
    const auto active = currentTarget();
    if (m_projection && box.target == active && m_drawTarget == active &&
        m_drawViewer == getViewer() && !getViewer()->isPreviewEnabled() &&
        active.level->revision() == m_revision) {
      const auto model = amModelAt(active.xsheet, active.column, active.row);
      if (model) {
        box.end         = pos;
        const auto rect = box.rect();
        auto selected   = box.add ? m_selectedHeads : HeadSelection{};
        for (std::size_t i = 0; i < model->points.size(); ++i) {
          const auto &point = model->points[i];
          if (point.head != i) continue;
          const auto screen = m_projection->project(point.position);
          if (screen && rect.contains(xy(*screen))) selected.insert(i);
        }
        setSelection(std::move(selected));
      }
    }
    invalidate();
  }

  void finishDrag() {
    if (!m_drag) return;
    auto drag = std::move(*m_drag);
    m_drag.reset();
    if (drag.before == drag.after ||
        drag.target.level->getScene() !=
            getApplication()->getCurrentScene()->getScene() ||
        drag.revision != drag.target.level->revision())
      return;
    if (drag.action) {
      drag.action->finish(drag.before.size() > 1);
      return;
    }
    TUndoManager::manager()->add(new UndoAMPoints(
        drag.target.level, std::move(drag.before), std::move(drag.after),
        std::move(drag.revision), drag.handle.has_value()));
    notifyModelChanged(*drag.target.level);
  }

  std::optional<std::size_t> nextOccurrence(const otam::Model &model) const {
    if (!m_occurrence || m_selectedHeads.size() != 1) return {};
    const auto head = *m_selectedHeads.begin();
    const auto size = model.points.size();
    for (std::size_t offset = 1; offset < size; ++offset) {
      const auto i = (*m_occurrence + offset) % size;
      if (model.points[i].head == head) return i;
    }
    return {};
  }

  void selectOccurrence(std::size_t occurrence) {
    m_occurrence = occurrence;
    m_handles.clear();
    invalidate();
  }

  void addHandle(const otam::BiasHandle &handle, bool incoming, double dpi) {
    if (handle.length <= 1e-12) return;
    const auto origin = m_projection->project(handle.origin);
    const auto end    = m_projection->project(handle.point());
    if (!origin || !end) return;
    TPointD marker    = xy(*end);
    TPointD direction = marker - xy(*origin);
    if (norm2(direction) < 144 * dpi * dpi) {
      // Give collapsed handles a screen-sized grab point without changing
      // the real tangent or jumping to the marker on the first drag.
      if (norm2(direction) < 1e-6) {
        auto reference    = handle;
        reference.bias[2] = 1;
        if (const auto p = m_projection->project(reference.point()))
          direction = xy(*p) - xy(*origin);
      }
      if (norm2(direction) < 1e-6) direction = {incoming ? -1.0 : 1.0, 0};
      marker = xy(*origin) + direction * (12 * dpi / norm(direction));
    }
    m_handles.push_back({handle, *end, marker, incoming});
    if (incoming)
      glColor3ub(255, 191, 88);
    else
      glColor3ub(207, 150, 255);
    glBegin(GL_LINES);
    glVertex2d((*origin)[0], (*origin)[1]);
    glVertex2d(marker.x, marker.y);
    glEnd();
    const double r = 3.5 * dpi;
    glRectd(marker.x - r, marker.y - r, marker.x + r, marker.y + r);
  }

  void updateDrag(const TMouseEvent &event, bool final) {
    if (!m_drag || !(m_drag->target == currentTarget()) ||
        m_drawViewer != getViewer() || getViewer()->isPreviewEnabled() ||
        m_drag->revision != m_drag->target.level->revision())
      return;
    if (!final && (event.isHighFrequent() ||
                   (m_updateTimer.isValid() && m_updateTimer.elapsed() < 25)))
      return;

    auto &drag       = *m_drag;
    auto values      = drag.before;
    const auto delta = event.m_pos - drag.mouseStart;
    if (norm2(delta) > 0) {
      const auto point = drag.projection.unproject(xy(drag.screenStart) + delta,
                                                   drag.screenStart[2]);
      if (drag.handle) {
        const auto bias =
            drag.handle->geometry.biasAt(point, event.isShiftPressed());
        std::copy(bias.begin(), bias.end(),
                  values.front().second.bias.begin() +
                      (drag.handle->incoming ? 0 : 3));
      } else {
        // Use the grabbed point's depth for a single model-space translation.
        // Applying a screen-space delta to each depth would deform the
        // selection.
        const auto movement = offsetFrom(point, drag.anchor);
        for (auto &[id, value] : values)
          for (int axis = 0; axis < 3; ++axis)
            value.position[axis] += movement[axis];
      }
    }
    if (values == drag.after) return;
    bool changed;
    if (!drag.action) {
      changed = drag.target.level->setControlPoints(values, drag.revision);
    } else if (values == drag.before) {
      changed = drag.action->replace(drag.action->before);
    } else {
      PositionOffsets offsets;
      for (std::size_t i = 0; i < values.size(); ++i)
        offsets.emplace(values[i].first, offsetFrom(values[i].second.position,
                                                    drag.restPositions[i]));
      changed = drag.action->setPositions(offsets);
    }
    if (changed) {
      drag.after = std::move(values);
      m_updateTimer.restart();
      invalidate();
    }
  }

  void beginDrag(const Target &active, const otam::Model &model,
                 std::size_t hit, const Vec3 &screen, const TMouseEvent &event,
                 std::optional<Handle> handle, bool actionMode) {
    auto action = actionMode ? beginActionEdit(active) : std::nullopt;
    if (actionMode && !action) return;

    PointValues values;
    std::vector<Vec3> rest;
    const auto addPoint = [&](std::size_t index) {
      const auto &point = model.points[index];
      values.emplace_back(point.id,
                          TXshAMLevel::PointValue{point.position, point.bias});
      if (action) rest.push_back(action->rest->points[index].position);
    };
    if (handle)
      addPoint(hit);
    else
      for (auto head : m_selectedHeads) addPoint(head);

    m_drag = Drag{
        active, *m_projection,     m_revision,     values,
        values, event.m_pos,       screen,         model.points[hit].position,
        handle, std::move(action), std::move(rest)};
    m_updateTimer.invalidate();
  }

public:
  AMControlPointEditor() : TTool(T_ControlPointEditor) { bind(AMModel); }

  ToolType getToolType() const override { return ColumnTool; }
  unsigned int getToolHints() const override { return HintNone; }
  bool isDragging() const override { return m_drag || m_selectionBox; }
  int getCursorId() const override { return ToolCursor::SplineEditorCursor; }
  void updateMatrix() override { setMatrix(TAffine()); }

  QString updateEnabled(int row, int column) override {
    const auto active = target(row, column);
    enable(active.level && active.level->editableModel());
    if (isEnabled()) return {};
    if (active.level && active.level->mesh())
      return QObject::tr(
          "Restore the original A:M source to resume control point edits.");
    return QObject::tr("Select an editable A:M model cell in the top Xsheet.");
  }

  void onDeactivate() override {
    finishDrag();
    setSelection({});
    m_selectionBox.reset();
    m_drawTarget = {};
    m_projection.reset();
  }

  void onImageChanged() override {
    if (!(m_drawTarget == currentTarget())) {
      finishDrag();
      m_selectionBox.reset();
    }
  }

  void draw() override {
    const auto *viewer = getViewer();
    if (!viewer || QOpenGLContext::currentContext() != viewer->context())
      return;
    m_projection.reset();
    m_handles.clear();
    if (viewer->isPreviewEnabled()) return;
    auto active = currentTarget();
    if (!active.level) return;
    const auto model = amModelAt(active.xsheet, active.column, active.row);
    if (!model) return;

    const auto binding    = actionBinding(active);
    const bool actionMode = bool(binding.action);
    const double time     = keyTime(active, binding);
    const auto revision = active.level->revision();
    if (active.level.getPointer() != m_drawTarget.level.getPointer() ||
        active.xsheet != m_drawTarget.xsheet ||
        active.column != m_drawTarget.column || revision != m_revision)
      setSelection({});
    m_drawTarget = active;
    m_drawViewer = viewer;
    m_revision   = revision;

    try {
      m_projection = modelProjection(active.xsheet, active.column, active.row,
                                     viewer->is3DView());
    } catch (const std::exception &) {
      return;  // The column or camera placement can be singular.
    }
    const double dpi = viewer->getDevPixRatio();
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_POINT_BIT | GL_LINE_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glPushMatrix();
    glLoadIdentity();

    glLineWidth(float(dpi));
    for (const auto &spline : model->splines) {
      const auto size = spline.points.size();
      for (std::size_t i = 0; i + 1 < size || (spline.closed && i < size);
           ++i) {
        const auto segment = otam::splineSegment(*model, spline, i);
        const bool selected =
            m_occurrence
                ? m_occurrence == segment.from || m_occurrence == segment.to
                : m_selectedHeads.count(model->points[segment.from].head) ||
                      m_selectedHeads.count(model->points[segment.to].head);
        if (selected)
          glColor3ub(123, 232, 229);
        else
          glColor3ub(88, 155, 161);
        const auto bezier = segment.bezier();
        auto previous     = m_projection->project(bezier.front());
        glBegin(GL_LINES);
        for (int sample = 1; sample <= 12; ++sample) {
          const auto p =
              m_projection->project(otam::sampleSpline(bezier, sample / 12.0));
          if (previous && p) {
            glVertex2d((*previous)[0], (*previous)[1]);
            glVertex2d((*p)[0], (*p)[1]);
          }
          previous = p;
        }
        glEnd();
        if (!actionMode && m_occurrence == segment.from)
          addHandle(segment.outgoing, false, dpi);
        if (!actionMode && m_occurrence == segment.to)
          addHandle(segment.incoming, true, dpi);
      }
    }

    glPointSize(float(6 * dpi));
    glBegin(GL_POINTS);
    for (std::size_t i = 0; i < model->points.size(); ++i) {
      const auto &point = model->points[i];
      if (point.head != i) continue;
      const auto p = m_projection->project(point.position);
      if (!p) continue;
      const bool selected = m_selectedHeads.count(i);
      if (hasPositionKey(binding, point.id, time))
        glColor3ub(255, selected ? 119 : 163, 208);
      else if (selected)
        glColor3ub(255, 208, 90);
      else
        glColor3ub(221, 248, 248);
      glVertex2d((*p)[0], (*p)[1]);
    }
    glEnd();

    if (m_selectionBox && m_selectionBox->target == active) {
      const auto rect = m_selectionBox->rect();
      glColor3ub(123, 232, 229);
      glBegin(GL_LINE_LOOP);
      glVertex2d(rect.x0, rect.y0);
      glVertex2d(rect.x1, rect.y0);
      glVertex2d(rect.x1, rect.y1);
      glVertex2d(rect.x0, rect.y1);
      glEnd();
    }
    glPopMatrix();
    glPopAttrib();
  }

  void leftButtonDown(const TPointD &, const TMouseEvent &event) override {
    finishDrag();
    m_selectionBox.reset();
    const auto active = currentTarget();
    if (!m_projection || !active.level || !(m_drawTarget == active) ||
        m_drawViewer != getViewer() || getViewer()->isPreviewEnabled())
      return;
    const auto model = amModelAt(active.xsheet, active.column, active.row);
    if (!model || m_revision != active.level->revision()) return;
    const bool actionMode = bool(actionBinding(active).action);
    const double radius = 7 * getViewer()->getDevPixRatio();
    double nearest      = radius * radius;
    std::optional<Handle> handle;
    std::optional<std::size_t> hit;
    Vec3 screen{};
    for (const auto &candidate : m_handles) {
      if (actionMode) break;
      const double distance = norm2(candidate.marker - event.m_pos);
      if (distance >= nearest) continue;
      nearest = distance;
      handle  = candidate;
      hit     = m_occurrence;
      screen  = candidate.screen;
    }
    if (!hit || event.isShiftPressed()) {
      for (std::size_t i = 0; i < model->points.size(); ++i) {
        const auto &point = model->points[i];
        if (point.head != i) continue;
        const auto p = m_projection->project(point.position);
        if (!p) continue;
        const double distance = norm2(xy(*p) - event.m_pos);
        if (distance > nearest ||
            (hit && distance == nearest && (*p)[2] <= screen[2]))
          continue;
        nearest = distance;
        handle.reset();
        hit    = i;
        screen = *p;
      }
    }

    TSelection::setCurrent(nullptr);
    if (!hit) {
      m_selectionBox = SelectionBox{active, event.m_pos, event.m_pos,
                                    event.isShiftPressed()};
    } else if (handle) {
      beginDrag(active, *model, *hit, screen, event, handle, actionMode);
    } else if (event.isShiftPressed()) {
      auto selected = m_selectedHeads;
      if (!selected.erase(*hit)) selected.insert(*hit);
      setSelection(std::move(selected));
    } else if (event.isAltPressed()) {
      setSelection({*hit});
      if (!actionMode) {
        if (const auto next = nextOccurrence(*model)) selectOccurrence(*next);
      }
    } else {
      if (!m_selectedHeads.count(*hit)) setSelection({*hit});
      beginDrag(active, *model, *hit, screen, event, {}, actionMode);
    }
    invalidate();
  }

  void leftButtonDrag(const TPointD &, const TMouseEvent &event) override {
    if (m_selectionBox) {
      m_selectionBox->end = event.m_pos;
      invalidate();
    } else {
      updateDrag(event, false);
    }
  }

  void leftButtonUp(const TPointD &, const TMouseEvent &event) override {
    if (m_selectionBox) {
      finishBox(event.m_pos);
    } else {
      updateDrag(event, true);
      finishDrag();
    }
  }

  void addContextMenuItems(QMenu *menu) override {
    const auto active = currentTarget();
    const auto *viewer =
        dynamic_cast<const TToolViewer *>(menu->parentWidget());
    if (isDragging() || !active.level || !(m_drawTarget == active) || !viewer ||
        viewer->isPreviewEnabled())
      return;
    const auto model = amModelAt(active.xsheet, active.column, active.row);
    if (!model || m_revision != active.level->revision()) return;
    const auto binding = actionBinding(active);
    if (binding.action) {
      const auto time = keyTime(active, binding);
      if (m_selectedHeads.empty() ||
          *m_selectedHeads.rbegin() >= model->points.size() || time < 0)
        return;
      const auto keyed = std::count_if(
          m_selectedHeads.begin(), m_selectedHeads.end(),
          [&](std::size_t head) {
            return hasPositionKey(binding, model->points[head].id, time);
          });
      const bool multiple = m_selectedHeads.size() > 1;
      if (std::size_t(keyed) < m_selectedHeads.size())
        menu->addAction(multiple ? QObject::tr("Set A:M Position Keys")
                                 : QObject::tr("Set A:M Position Key"),
                        menu, [this] { editSelectedKeys(false); });
      if (keyed)
        menu->addAction(multiple ? QObject::tr("Remove A:M Position Keys")
                                 : QObject::tr("Remove A:M Position Key"),
                        menu, [this] { editSelectedKeys(true); });
      return;
    }
    if (!nextOccurrence(*model)) return;
    menu->addAction(QObject::tr("Next Attached A:M Spline"), menu, [this] {
      const auto active = currentTarget();
      if (!active.level || !(m_drawTarget == active) ||
          m_revision != active.level->revision())
        return;
      const auto model = amModelAt(active.xsheet, active.column, active.row);
      if (model)
        if (const auto next = nextOccurrence(*model)) selectOccurrence(*next);
    });
  }

  bool keyDown(QKeyEvent *event) override {
    if (event->key() != Qt::Key_Escape || !isDragging()) return false;
    if (m_selectionBox) {
      m_selectionBox.reset();
      invalidate();
      return true;
    }
    auto drag = std::move(*m_drag);
    m_drag.reset();
    if (drag.action)
      drag.action->replace(drag.action->before);
    else if (!(drag.before == drag.after))
      drag.target.level->setControlPoints(drag.before, drag.revision);
    invalidate();
    return true;
  }
};

AMControlPointEditor amControlPointEditor;
}  // namespace

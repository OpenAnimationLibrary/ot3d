#include "tools/tool.h"
#include "tools/toolcommandids.h"
#include "toonz/amlevelrender.h"
#include "toonz/stage2.h"
#include "toonz/tcolumnhandle.h"
#include "toonz/tframehandle.h"
#include "toonz/toonzscene.h"
#include "toonz/tscenehandle.h"
#include "toonz/txshamlevel.h"
#include "toonz/txshcolumn.h"
#include "toonz/txsheethandle.h"
#include "toonzqt/selection.h"
#include "historytypes.h"
#include "tgl.h"
#include "tundo.h"

#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMenu>
#include <QOpenGLContext>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace {
using otam::Vec3;

void notifyModelChanged(const TXshAMLevel &level) {
  auto *app = TTool::getApplication();
  if (app->getCurrentScene()->getScene() != level.getScene()) return;
  app->getCurrentScene()->notifySceneChanged();
  app->getCurrentXsheet()->notifyXsheetChanged();
}

class UndoAMPoint final : public TUndo {
  TXshAMLevelP m_level;
  otam::Id m_id;
  TXshAMLevel::PointValue m_before, m_after;
  std::string m_revision;

  void apply(const TXshAMLevel::PointValue &value) const {
    if (m_level->setControlPoint(m_id, value, m_revision))
      notifyModelChanged(*m_level);
  }

public:
  UndoAMPoint(TXshAMLevelP level, otam::Id id,
              const TXshAMLevel::PointValue &before,
              const TXshAMLevel::PointValue &after, std::string revision)
      : m_level(level)
      , m_id(id)
      , m_before(before)
      , m_after(after)
      , m_revision(std::move(revision)) {}

  int getSize() const override {
    return int(sizeof(*this) + m_revision.size());
  }
  void undo() const override { apply(m_before); }
  void redo() const override { apply(m_after); }
  QString getHistoryString() override {
    return m_before.position == m_after.position
               ? QObject::tr("Adjust A:M Spline Bias")
               : QObject::tr("Move A:M Control Point");
  }
  int getHistoryType() override { return HistoryType::ControlPointEditorTool; }
};

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
    otam::Id id;
    TXshAMLevel::PointValue before, after;
    TPointD mouseStart;
    Vec3 screenStart;
    std::optional<Handle> handle;
  };

  Target m_drawTarget;
  const TToolViewer *m_drawViewer = nullptr;
  std::string m_revision;
  std::optional<Projection> m_projection;
  std::optional<std::size_t> m_selectedHead, m_occurrence;
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

  void clearSelection() {
    m_selectedHead.reset();
    m_occurrence.reset();
    m_handles.clear();
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
    TUndoManager::manager()->add(new UndoAMPoint(
        drag.target.level, drag.id, drag.before, drag.after, drag.revision));
    notifyModelChanged(*drag.target.level);
  }

  std::optional<std::size_t> nextOccurrence(const otam::Model &model) const {
    if (!m_occurrence || !m_selectedHead) return {};
    const auto size = model.points.size();
    for (std::size_t offset = 1; offset < size; ++offset) {
      const auto i = (*m_occurrence + offset) % size;
      if (model.points[i].head == *m_selectedHead) return i;
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
        getViewer()->isPreviewEnabled())
      return;
    if (!final && (event.isHighFrequent() ||
                   (m_updateTimer.isValid() && m_updateTimer.elapsed() < 25)))
      return;

    auto &drag       = *m_drag;
    auto value       = drag.before;
    const auto delta = event.m_pos - drag.mouseStart;
    if (norm2(delta) > 0) {
      const auto point = drag.projection.unproject(xy(drag.screenStart) + delta,
                                                   drag.screenStart[2]);
      if (drag.handle) {
        const auto bias =
            drag.handle->geometry.biasAt(point, event.isShiftPressed());
        std::copy(bias.begin(), bias.end(),
                  value.bias.begin() + (drag.handle->incoming ? 0 : 3));
      } else {
        value.position = point;
      }
    }
    if (value == drag.after) return;
    if (drag.target.level->setControlPoint(drag.id, value, drag.revision)) {
      drag.after = value;
      m_updateTimer.restart();
      invalidate();
    }
  }

public:
  AMControlPointEditor() : TTool(T_ControlPointEditor) { bind(AMModel); }

  ToolType getToolType() const override { return ColumnTool; }
  unsigned int getToolHints() const override { return HintNone; }
  bool isDragging() const override { return m_drag.has_value(); }
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
    clearSelection();
    m_drawTarget = {};
    m_projection.reset();
  }

  void onImageChanged() override {
    if (!(m_drawTarget == currentTarget())) finishDrag();
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
    const auto model = active.level->editableModel();
    if (!model) return;

    const auto revision = active.level->revision();
    if (active.level.getPointer() != m_drawTarget.level.getPointer() ||
        revision != m_revision)
      clearSelection();
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
            m_occurrence == segment.from || m_occurrence == segment.to;
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
        if (m_occurrence == segment.from)
          addHandle(segment.outgoing, false, dpi);
        if (m_occurrence == segment.to) addHandle(segment.incoming, true, dpi);
      }
    }

    glPointSize(float(6 * dpi));
    glBegin(GL_POINTS);
    for (std::size_t i = 0; i < model->points.size(); ++i) {
      const auto &point = model->points[i];
      if (point.head != i) continue;
      const auto p = m_projection->project(point.position);
      if (!p) continue;
      if (m_selectedHead == i)
        glColor3ub(255, 208, 90);
      else
        glColor3ub(221, 248, 248);
      glVertex2d((*p)[0], (*p)[1]);
    }
    glEnd();

    glPopMatrix();
    glPopAttrib();
  }

  void leftButtonDown(const TPointD &, const TMouseEvent &event) override {
    finishDrag();
    const auto active = currentTarget();
    if (!m_projection || !active.level || !(m_drawTarget == active) ||
        m_drawViewer != getViewer() || getViewer()->isPreviewEnabled())
      return;
    const auto model = active.level->editableModel();
    if (!model || m_revision != active.level->revision()) return;
    const double radius = 7 * getViewer()->getDevPixRatio();
    double nearest      = radius * radius;
    std::optional<Handle> handle;
    std::optional<std::size_t> hit;
    Vec3 screen{};
    for (const auto &candidate : m_handles) {
      const double distance = norm2(candidate.marker - event.m_pos);
      if (distance >= nearest) continue;
      nearest = distance;
      handle  = candidate;
      hit     = m_occurrence;
      screen  = candidate.screen;
    }
    if (!hit) {
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
        hit     = i;
        screen  = *p;
      }
      if (hit != m_selectedHead) {
        m_selectedHead = hit;
        m_occurrence   = hit;
      }
    }
    if (hit) TSelection::setCurrent(nullptr);
    if (hit && !handle && event.isAltPressed()) {
      if (const auto next = nextOccurrence(*model)) selectOccurrence(*next);
    } else if (hit) {
      const auto &point = model->points[*hit];
      const TXshAMLevel::PointValue value{point.position, point.bias};
      m_drag = Drag{active, *m_projection, m_revision, point.id, value,
                    value,  event.m_pos,   screen,     handle};
      m_updateTimer.invalidate();
    }
    invalidate();
  }

  void leftButtonDrag(const TPointD &, const TMouseEvent &event) override {
    updateDrag(event, false);
  }

  void leftButtonUp(const TPointD &, const TMouseEvent &event) override {
    updateDrag(event, true);
    finishDrag();
  }

  void addContextMenuItems(QMenu *menu) override {
    const auto active = currentTarget();
    if (m_drag || !active.level || !(m_drawTarget == active)) return;
    const auto model = active.level->editableModel();
    if (!model || m_revision != active.level->revision() ||
        !nextOccurrence(*model))
      return;
    menu->addAction(QObject::tr("Next Attached A:M Spline"), menu, [this] {
      const auto active = currentTarget();
      if (!active.level || !(m_drawTarget == active) ||
          m_revision != active.level->revision())
        return;
      const auto model = active.level->editableModel();
      if (model)
        if (const auto next = nextOccurrence(*model)) selectOccurrence(*next);
    });
  }

  bool keyDown(QKeyEvent *event) override {
    if (event->key() != Qt::Key_Escape || !m_drag) return false;
    auto drag = std::move(*m_drag);
    m_drag.reset();
    if (!(drag.before == drag.after))
      drag.target.level->setControlPoint(drag.id, drag.before, drag.revision);
    invalidate();
    return true;
  }
};

AMControlPointEditor amControlPointEditor;
}  // namespace

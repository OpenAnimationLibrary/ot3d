#include "ambones.h"
#include "amtoolprojection.h"
#include "tgl.h"
#include "tools/tool.h"
#include "tools/toolcommandids.h"
#include "toonz/stage2.h"
#include "toonz/tcolumnhandle.h"
#include "toonz/tframehandle.h"
#include "toonz/toonzscene.h"
#include "toonz/tscenehandle.h"
#include "toonz/txshamlevel.h"
#include "toonz/txshlevelcolumn.h"
#include "toonz/txsheethandle.h"
#include "toonz/txsheet.h"
#include "tproperty.h"
#include "tundo.h"

#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMenu>
#include <QOpenGLContext>

#include <algorithm>
#include <cmath>
#include <exception>
#include <optional>
#include <utility>

namespace {
using otam::Vec3;
constexpr double Pi = 3.14159265358979323846;

TPointD xy(const Vec3 &p) { return {p[0], p[1]}; }

Vec3 difference(const Vec3 &a, const Vec3 &b) {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

double dot(const Vec3 &a, const Vec3 &b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 axisOf(const otam::Matrix &frame, int axis) {
  return {frame[axis], frame[4 + axis], frame[8 + axis]};
}

struct Target {
  TXshAMLevelP level;
  TXshLevelColumnP column;
  TXsheet *sheet = nullptr;
  int index = -1, row = -1;

  bool sameColumn(const Target &other) const {
    return level.getPointer() == other.level.getPointer() &&
           column.getPointer() == other.column.getPointer() &&
           sheet == other.sheet && index == other.index;
  }

  bool operator==(const Target &other) const {
    return sameColumn(other) && row == other.row;
  }
};

bool sameBinding(const AMBoneBinding &a, const AMBoneBinding &b) {
  return a.level == b.level && a.animation == b.animation;
}

void notifyBoneChange() {
  auto *app = TTool::getApplication();
  app->getCurrentScene()->notifySceneChanged();
  app->getCurrentXsheet()->notifyXsheetChanged();
}

class UndoBoneKey final : public TUndo {
  TXshLevelColumnP m_column;
  AMBoneBinding m_before, m_after;

  void apply(const AMBoneBinding &expected,
             const AMBoneBinding &replacement) const {
    const auto &level = expected.level ? expected.level : replacement.level;
    if (!level ||
        level->getScene() !=
            TTool::getApplication()->getCurrentScene()->getScene() ||
        !sameBinding(m_column->getAMBones(), expected))
      return;
    m_column->setAMBones(replacement);
    notifyBoneChange();
  }

public:
  UndoBoneKey(const TXshLevelColumnP &column, const AMBoneBinding &before,
              const AMBoneBinding &after)
      : m_column(column), m_before(before), m_after(after) {}

  void undo() const override { apply(m_after, m_before); }
  void redo() const override { apply(m_before, m_after); }
  int getSize() const override {
    std::size_t size = sizeof(*this);
    for (const auto *binding : {&m_before, &m_after})
      if (binding->animation)
        for (const auto &[name, keys] : binding->animation->tracks)
          size += name.size() +
                  keys.size() * (sizeof(int) + sizeof(otam::BoneTransform));
    return int(size);
  }
  QString getHistoryString() override {
    return QObject::tr("Edit A:M Bone Key");
  }
};

struct Segment {
  TPointD from, to;

  double distanceSquared(const TPointD &p) const {
    const auto delta    = to - from;
    const double length = norm2(delta);
    if (length == 0) return norm2(p - from);
    const auto offset = p - from;
    const double t    = std::clamp(
           (offset.x * delta.x + offset.y * delta.y) / length, 0.0, 1.0);
    return norm2(p - (from + delta * t));
  }

  void draw() const {
    glVertex2d(from.x, from.y);
    glVertex2d(to.x, to.y);
  }
};

struct Gizmo {
  AMToolProjection projection;
  otam::Matrix frame;
  Vec3 pivot, screen;
  double radius;
  double pixels;
  bool rotating;
  std::array<std::vector<Segment>, 3> handles{};
  std::array<bool, 3> viewAligned{};

  // The ring has a unique plane intersection unless it is viewed edge-on.
  std::optional<double> ringAngle(const TPointD &position, int axis) const {
    const auto a = projection.unproject(position, screen[2]);
    const auto ray =
        difference(projection.unproject(position, screen[2] - 1), a);
    const auto normal  = axisOf(frame, axis);
    const double along = dot(ray, normal);
    if (std::abs(along) <= 0.25 * std::sqrt(dot(ray, ray))) return {};
    auto point     = difference(a, pivot);
    const double t = -dot(point, normal) / along;
    for (int i = 0; i < 3; ++i) point[i] += ray[i] * t;
    if (dot(point, point) < radius * radius * 0.01) return {};
    return std::atan2(dot(point, axisOf(frame, (axis + 2) % 3)),
                      dot(point, axisOf(frame, (axis + 1) % 3)));
  }
};

struct Drag {
  Target target;
  Gizmo gizmo;
  std::string bone, revision;
  AMBoneBinding before, after;
  otam::BoneTransform start, last;
  TPointD mouse, direction;
  double modelUnitsPerPixel;
  int axis;
  std::optional<double> angle;
  double rotation = 0;
};

class AMBonesTool final : public TTool {
  TPropertyGroup m_properties;
  TEnumProperty m_mode{"Mode:"};
  Target m_drawTarget;
  const TToolViewer *m_drawViewer = nullptr;
  std::string m_selected, m_revision;
  std::vector<std::pair<std::string, Segment>> m_bones;
  std::optional<Gizmo> m_gizmo;
  std::optional<Drag> m_drag;
  QElapsedTimer m_updateTimer;

  Target target(int row, int index) const {
    auto *app = getApplication();
    if (index < 0 || app->getCurrentFrame()->isEditingLevel() ||
        app->getCurrentFrame()->isPlaying() ||
        CameraTestCheck::instance()->isEnabled())
      return {};
    auto *sheet = app->getCurrentXsheet()->getXsheet();
    auto *scene = app->getCurrentScene()->getScene();
    if (!sheet || !scene || sheet != scene->getTopXsheet()) return {};
    auto *xcol   = sheet->getColumn(index);
    auto *column = xcol ? xcol->getLevelColumn() : nullptr;
    if (!column || column->isLocked() || !column->isCamstandVisible() ||
        !column->getOpacity())
      return {};
    const auto cell = sheet->getCell(row, index);
    auto *level     = cell.isEmpty() ? nullptr : cell.m_level->getAMLevel();
    if (!level) return {};
    const auto binding = column->getAMBones();
    if (binding.animation && binding.level != cell.m_level) return {};
    return {TXshAMLevelP(level), TXshLevelColumnP(column), sheet, index, row};
  }

  Target currentTarget() const {
    auto *app = getApplication();
    return target(app->getCurrentFrame()->getFrame(),
                  app->getCurrentColumn()->getColumnIndex());
  }

  bool isLive(const Target &active) const {
    const auto *viewer = getViewer();
    return active.level && m_drawTarget == active && viewer &&
           viewer == m_drawViewer && !viewer->isPreviewEnabled() &&
           active.level->revision() == m_revision;
  }

  static AMBoneBinding makeBinding(
      const Target &active, const AMBoneBinding &before,
      const std::string &bone, const std::optional<otam::BoneTransform> &key) {
    auto animation =
        before.animation
            ? std::make_shared<otam::BoneAnimation>(*before.animation)
            : std::make_shared<otam::BoneAnimation>();
    if (key) {
      animation->tracks[bone][active.row] = *key;
    } else {
      auto track = animation->tracks.find(bone);
      if (track == animation->tracks.end() || !track->second.erase(active.row))
        return before;
      if (track->second.empty()) animation->tracks.erase(track);
    }
    if (animation->tracks.empty()) return {};
    return {TXshLevelP(active.level.getPointer()), std::move(animation)};
  }

  void editKey(bool clear, bool reset = false) {
    const auto active = currentTarget();
    if (!isLive(active) || m_selected.empty()) return;
    const auto before = active.column->getAMBones();
    auto key          = otam::BoneTransform{};
    if (before.animation && !reset)
      key = otam::sampleBone(*before.animation, m_selected, active.row);
    const auto after = makeBinding(active, before, m_selected,
                                   clear ? std::nullopt : std::optional(key));
    if (sameBinding(before, after)) return;
    active.column->setAMBones(after);
    TUndoManager::manager()->add(new UndoBoneKey(active.column, before, after));
    notifyBoneChange();
    invalidate();
  }

  void finishDrag(bool commit) {
    if (!m_drag) return;
    auto drag = std::move(*m_drag);
    m_drag.reset();
    if (sameBinding(drag.before, drag.after) ||
        !sameBinding(drag.target.column->getAMBones(), drag.after))
      return;
    if (commit) {
      TUndoManager::manager()->add(
          new UndoBoneKey(drag.target.column, drag.before, drag.after));
      notifyBoneChange();
    } else {
      drag.target.column->setAMBones(drag.before);
    }
    invalidate();
  }

  void updateDrag(const TMouseEvent &event, bool final) {
    if (!m_drag || !(m_drag->target == currentTarget()) ||
        !isLive(m_drag->target) || m_drag->revision != m_revision)
      return;
    auto &drag       = *m_drag;
    auto pose        = drag.start;
    const auto delta = event.m_pos - drag.mouse;
    const double pixels =
        delta.x * drag.direction.x + delta.y * drag.direction.y;
    if (drag.gizmo.rotating) {
      double radians = pixels / drag.gizmo.pixels;
      if (drag.angle) {
        if (const auto angle = drag.gizmo.ringAngle(event.m_pos, drag.axis)) {
          drag.rotation += std::remainder(*angle - *drag.angle, 2 * Pi);
          drag.angle = angle;
        }
        radians = drag.rotation;
      }
      if (event.isShiftPressed())
        radians = std::round(radians * 12 / Pi) * Pi / 12;
      if (radians != 0) otam::rotateBone(pose, drag.axis, radians);
    } else {
      const double distance = pixels * drag.modelUnitsPerPixel;
      if (distance != 0) otam::translateBone(pose, drag.axis, distance);
    }
    if (pose == drag.last ||
        (!final && (event.isHighFrequent() ||
                    (m_updateTimer.isValid() && m_updateTimer.elapsed() < 25))))
      return;
    if (!sameBinding(drag.target.column->getAMBones(), drag.after)) return;
    auto after = pose == drag.start
                     ? drag.before
                     : makeBinding(drag.target, drag.before, drag.bone, pose);
    drag.target.column->setAMBones(after);
    drag.after = std::move(after);
    drag.last  = pose;
    m_updateTimer.restart();
    invalidate();
  }

  void drawGizmo(const AMToolProjection &projection, const otam::Matrix &frame,
                 const Vec3 &pivot, const Vec3 &screen, double dpi) {
    const double pixels = 52 * dpi;
    const auto edge =
        projection.unproject(xy(screen) + TPointD(pixels, 0), screen[2]);
    const auto diff     = difference(edge, pivot);
    const double radius = std::sqrt(dot(diff, diff));
    if (!(radius > 0)) return;
    const bool rotating = m_mode.getValue() == L"Rotate";
    m_gizmo = Gizmo{projection, frame, pivot, screen, radius, pixels, rotating};
    static constexpr std::array<otam::Color, 3> colors = {{
        {1.f, .35f, .31f},
        {.43f, .92f, .37f},
        {.35f, .65f, 1.f},
    }};
    for (int axis = 0; axis < 3; ++axis) {
      auto &segments = m_gizmo->handles[axis];
      if (rotating) {
        const auto u = axisOf(frame, (axis + 1) % 3);
        const auto v = axisOf(frame, (axis + 2) % 3);
        std::optional<Vec3> previous;
        for (int i = 0; i <= 48; ++i) {
          const double theta = 2 * Pi * i / 48;
          auto p             = pivot;
          for (int j = 0; j < 3; ++j)
            p[j] += radius * (u[j] * std::cos(theta) + v[j] * std::sin(theta));
          const auto current = projection.project(p);
          if (previous && current)
            segments.push_back({xy(*previous), xy(*current)});
          previous = current;
        }
      } else {
        auto p               = pivot;
        const auto direction = axisOf(frame, axis);
        for (int i = 0; i < 3; ++i) p[i] += radius * direction[i];
        const auto projected = projection.project(p);
        const auto start     = xy(screen);
        auto end             = projected ? xy(*projected) : start;
        // Give a view-aligned axis a handle and map it to vertical drag.
        if (norm2(end - start) < 64 * dpi * dpi) {
          m_gizmo->viewAligned[axis] = true;
          end = start + TPointD((axis - 1) * 13 * dpi, -16 * dpi);
        }
        segments.push_back({start, end});
      }

      glColor3fv(colors[axis].data());
      glLineWidth(float(2 * dpi));
      glBegin(GL_LINES);
      for (const auto &segment : segments) segment.draw();
      glEnd();
      if (!rotating) {
        const auto end = segments.front().to;
        glPointSize(float(7 * dpi));
        glBegin(GL_POINTS);
        glVertex2d(end.x, end.y);
        glEnd();
        tglDrawText(end + TPointD(5 * dpi, 3 * dpi),
                    std::string(1, "XYZ"[axis]));
      }
    }
  }

public:
  AMBonesTool() : TTool(T_AMBones) {
    bind(AMModel);
    m_mode.addValue(L"Rotate");
    m_mode.addValue(L"Move");
    m_mode.setValue(L"Rotate");
    m_properties.bind(m_mode);
  }

  TPropertyGroup *getProperties(int) override { return &m_properties; }
  void updateTranslation() override {
    m_mode.setQStringName(QObject::tr("Mode:"));
    m_mode.setItemUIName(L"Rotate", QObject::tr("Rotate"));
    m_mode.setItemUIName(L"Move", QObject::tr("Move"));
  }
  ToolType getToolType() const override { return ColumnTool; }
  unsigned int getToolHints() const override { return HintNone; }
  int getCursorId() const override { return ToolCursor::PenCrosshairCursor; }
  void updateMatrix() override { setMatrix(TAffine()); }
  bool isDragging() const override { return bool(m_drag); }
  bool onPropertyChanged(std::string) override {
    invalidate();
    return true;
  }

  QString updateEnabled(int row, int column) override {
    const auto active = target(row, column);
    const auto model  = active.level ? active.level->editableModel() : nullptr;
    enable(model && !model->bones.empty());
    return isEnabled() ? QString()
                       : QObject::tr(
                             "Select an editable A:M model with a rigid bone "
                             "rig in the top Xsheet.");
  }

  void onDeactivate() override {
    finishDrag(true);
    m_selected.clear();
    m_drawTarget = {};
    m_drawViewer = nullptr;
    m_gizmo.reset();
  }

  void onImageChanged() override {
    if (!(m_drawTarget == currentTarget())) finishDrag(true);
  }

  void draw() override {
    const auto *viewer = getViewer();
    if (!viewer || QOpenGLContext::currentContext() != viewer->context())
      return;
    m_bones.clear();
    m_gizmo.reset();
    if (viewer->isPreviewEnabled()) return;
    const auto active = currentTarget();
    if (!active.level) return;
    const auto model = active.level->editableModel();
    if (!model || model->bones.empty()) return;

    const auto revision = active.level->revision();
    if (!m_drawTarget.sameColumn(active) || revision != m_revision)
      m_selected.clear();
    m_drawTarget       = active;
    m_drawViewer       = viewer;
    m_revision         = revision;
    const auto binding = active.column->getAMBones();
    const auto posed =
        otam::poseBones(*model, binding.animation.get(), active.row);

    std::optional<AMToolProjection> projection;
    try {
      projection = amToolProjection(
          active.sheet, active.row,
          amModelPlacement(active.sheet, active.index, active.row),
          viewer->is3DView());
    } catch (const std::exception &) {
      return;  // A camera or column placement can be singular.
    }

    const double dpi = viewer->getDevPixRatio();
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_POINT_BIT | GL_LINE_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glPushMatrix();
    glLoadIdentity();
    std::optional<std::size_t> selected;
    for (std::size_t i = 0; i < posed.size(); ++i) {
      const auto &bone  = model->bones[i];
      const auto &frame = posed[i].frame;
      const auto root   = projection->project(otam::transform(frame, {}));
      const auto tip    = projection->project(otam::transform(frame, bone.tip));
      if (!root || !tip) continue;
      Segment segment{xy(*root), xy(*tip)};
      m_bones.emplace_back(bone.path, segment);

      glColor3fv(bone.color.data());
      const bool current = bone.path == m_selected;
      if (current) selected = i;
      glLineWidth(float((current ? 4 : 2) * dpi));
      glBegin(GL_LINES);
      segment.draw();
      glEnd();
      glPointSize(float((current ? 10 : 7) * dpi));
      glBegin(GL_POINTS);
      segment.draw();
      glEnd();
      glColor3ub(245, 245, 245);
      tglDrawText(segment.to + TPointD(5 * dpi, 3 * dpi), bone.name);
    }
    if (selected) {
      const auto &frame = posed[*selected].frame;
      const auto pivot  = otam::transform(frame, {});
      if (const auto screen = projection->project(pivot))
        drawGizmo(*projection, frame, pivot, *screen, dpi);
    }
    glPopMatrix();
    glPopAttrib();
  }

  void leftButtonDown(const TPointD &, const TMouseEvent &event) override {
    const auto active = currentTarget();
    if (!isLive(active)) return;
    const double dpi = getViewer()->getDevPixRatio();
    double best      = 64 * dpi * dpi;
    if (m_gizmo && !event.isAltPressed()) {
      int axis = -1;
      Segment hit;
      for (int i = 0; i < 3; ++i)
        for (const auto &segment : m_gizmo->handles[i]) {
          const double d = m_gizmo->rotating
                               ? segment.distanceSquared(event.m_pos)
                               : norm2(event.m_pos - segment.to);
          if (d >= best || norm2(segment.to - segment.from) < 1e-6) continue;
          best = d;
          axis = i;
          hit  = segment;
        }
      if (axis >= 0) {
        const auto binding = active.column->getAMBones();
        const auto key =
            binding.animation
                ? otam::sampleBone(*binding.animation, m_selected, active.row)
                : otam::BoneTransform{};
        auto delta   = hit.to - hit.from;
        double units = m_gizmo->radius / std::sqrt(norm2(delta));
        if (m_gizmo->viewAligned[axis]) {
          delta = {0, 1};
          units = m_gizmo->radius / m_gizmo->pixels;
        }
        m_drag = Drag{active,
                      *m_gizmo,
                      m_selected,
                      m_revision,
                      binding,
                      binding,
                      key,
                      key,
                      event.m_pos,
                      delta * (1 / std::sqrt(norm2(delta))),
                      units,
                      axis,
                      m_gizmo->rotating ? m_gizmo->ringAngle(event.m_pos, axis)
                                        : std::nullopt};
        m_updateTimer.invalidate();
        return;
      }
    }

    m_selected.clear();
    for (const auto &[name, segment] : m_bones) {
      const double d = std::min(norm2(event.m_pos - segment.from),
                                norm2(event.m_pos - segment.to));
      if (d >= best) continue;
      best       = d;
      m_selected = name;
    }
    if (m_selected.empty())
      for (const auto &[name, segment] : m_bones) {
        const double d = segment.distanceSquared(event.m_pos);
        if (d >= best) continue;
        best       = d;
        m_selected = name;
      }
    invalidate();
  }

  void leftButtonDrag(const TPointD &, const TMouseEvent &event) override {
    updateDrag(event, false);
  }

  void leftButtonUp(const TPointD &, const TMouseEvent &event) override {
    updateDrag(event, true);
    finishDrag(true);
  }

  bool keyDown(QKeyEvent *event) override {
    if (event->key() != Qt::Key_Escape || !m_drag) return false;
    finishDrag(false);
    return true;
  }

  void addContextMenuItems(QMenu *menu) override {
    if (m_drag || m_selected.empty() || !isLive(currentTarget())) return;
    menu->addAction(QObject::tr("Set Bone Key"), menu,
                    [this] { editKey(false); });
    menu->addAction(QObject::tr("Set Bone Key at Rest Pose"), menu,
                    [this] { editKey(false, true); });
    const auto active  = currentTarget();
    const auto binding = active.column->getAMBones();
    if (!binding.animation) return;
    const auto track = binding.animation->tracks.find(m_selected);
    if (track != binding.animation->tracks.end() &&
        track->second.count(active.row))
      menu->addAction(QObject::tr("Remove Bone Key"), menu,
                      [this] { editKey(true); });
  }
};

AMBonesTool amBonesTool;
}  // namespace

#include "functioneditortracks.h"

#include "amaction.h"
#include "amactionmenu.h"
#include "vectoractionmenu.h"
#include "texception.h"
#include "floatingpanelcommand.h"
#include "pane.h"
#include "tapp.h"
#include "tsimplecolorstyles.h"
#include "toonz/amlevelrender.h"
#include "toonz/levelset.h"
#include "toonz/palettecontroller.h"
#include "toonz/tpalettehandle.h"
#include "toonz/tframehandle.h"
#include "toonz/toonzscene.h"
#include "toonz/tscenehandle.h"
#include "toonz/txshamlevel.h"
#include "toonz/txshcell.h"
#include "toonz/vectoraction.h"
#include "toonz/txsheet.h"
#include "toonz/txsheethandle.h"
#include "toonz/txshlevelcolumn.h"
#include "toonz/txshlevelhandle.h"
#include "toonz/txshsimplelevel.h"
#include "toonzqt/dvdialog.h"
#include "toonzqt/functionkeytrack.h"
#include "toonzqt/functionviewer.h"
#include "tundo.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

FunctionViewer *openEditor() {
  auto *panel = OpenFloatingPanel::getOrOpenFloatingPanel("FunctionEditor");
  return panel ? qobject_cast<FunctionViewer *>(panel->widget()) : nullptr;
}

QString sourceId(const void *owner, qlonglong id) {
  return QString::number(quintptr(owner), 16) + ':' + QString::number(id);
}

bool validFrame(double frame) {
  return std::isfinite(frame) && frame >= 0 &&
         frame < (std::numeric_limits<int>::max)();
}

// Prepare all removals before inserting any destinations. This allows a group
// of adjacent keys to move together and rejects collisions with other keys.
template <typename Map, typename Frame, typename Sample, typename SetValue>
bool editGroupedKeys(const Map &before, Map &after,
                     const std::vector<FunctionKeyTrack::Edit> &edits,
                     Frame nativeFrame, Sample sample, SetValue setValue) {
  for (const auto &edit : edits) {
    if (!edit.from && !edit.to) return false;
    if ((edit.from && !validFrame(*edit.from)) ||
        (edit.to && !validFrame(*edit.to)) ||
        (edit.value && !std::isfinite(*edit.value)))
      return false;
    if (edit.from && after.erase(nativeFrame(*edit.from)) != 1) return false;
  }

  for (const auto &edit : edits) {
    if (!edit.to) continue;
    const auto time = nativeFrame(*edit.to);
    if (after.count(time)) return false;
    auto value = edit.from ? before.at(nativeFrame(*edit.from)) : sample(time);
    if (edit.value) setValue(value, *edit.value);
    after.emplace(time, std::move(value));
  }
  return true;
}

TPalette *levelPalette(TXshLevel *level) {
  if (!level) return nullptr;
  if (auto *am = level->getAMLevel()) return am->getPalette();
  if (auto *simple = level->getSimpleLevel()) {
    if (simple->getType() == PLI_XSHLEVEL || simple->getType() == TZP_XSHLEVEL)
      return simple->getPalette();
  }
  return nullptr;
}

bool hasStyle(TPalette *palette, int styleId) {
  if (styleId <= 0 || styleId >= palette->getStyleCount() ||
      !palette->getStylePage(styleId))
    return false;
  auto *style = palette->getStyle(styleId);
  return style && typeid(*style) == typeid(TSolidColorStyle);
}

class PaletteKeysUndo final : public TUndo {
  TPaletteP m_palette;
  ToonzScene *m_scene;
  int m_styleId;
  TPalette::StyleAnimation m_before, m_after;
  TColorStyleP m_unkeyedStyle;

  void apply(const TPalette::StyleAnimation &keys) const {
    auto *app = TApp::instance();
    if (app->getCurrentScene()->getScene() != m_scene ||
        !hasStyle(m_palette.getPointer(), m_styleId))
      return;
    m_palette->replaceStyleAnimation(m_styleId, keys, *m_unkeyedStyle);
    app->notifyPaletteKeyframesChanged(m_palette.getPointer());
  }

public:
  PaletteKeysUndo(TPalette *palette, ToonzScene *scene, int styleId,
                  TPalette::StyleAnimation before,
                  TPalette::StyleAnimation after, TColorStyleP unkeyedStyle)
      : m_palette(palette)
      , m_scene(scene)
      , m_styleId(styleId)
      , m_before(std::move(before))
      , m_after(std::move(after))
      , m_unkeyedStyle(std::move(unkeyedStyle)) {}

  void undo() const override { apply(m_before); }
  void redo() const override { apply(m_after); }
  int getSize() const override {
    return int(sizeof(*this) +
               (m_before.size() + m_after.size()) *
                   (sizeof(TSolidColorStyle) + 6 * sizeof(void *)));
  }
  QString getHistoryString() override {
    return QObject::tr("Edit Palette Keys");
  }
};

class PaletteKeyTrack final : public FunctionKeyTrack {
  TXshLevelP m_level;
  ToonzScene *m_scene;
  int m_styleId;
  TPalette *palette() const {
    if (TApp::instance()->getCurrentScene()->getScene() != m_scene ||
        m_scene->getLevelSet()->getLevel(m_level->getName()) !=
            m_level.getPointer())
      return nullptr;
    auto *owner = levelPalette(m_level.getPointer());
    return owner && hasStyle(owner, m_styleId) ? owner : nullptr;
  }

  static Value components(const TColorStyle &style) {
    const auto color = style.getMainColor();
    return {double(color.r), double(color.g), double(color.b), double(color.m)};
  }

public:
  PaletteKeyTrack(TXshLevel *level, ToonzScene *scene, int styleId)
      : m_level(level), m_scene(scene), m_styleId(styleId) {}

  Snapshot read() const override {
    Snapshot result;
    auto *owner      = palette();
    result.available = owner != nullptr;
    if (!owner) return result;
    const auto keys = owner->getStyleAnimation(m_styleId);
    for (const auto &[frame, style] : keys)
      result.keys.emplace(frame, components(*style));
    // At keyed frames the visible style changes while scrubbing. The default
    // matters only for unkeyed tracks.
    if (keys.empty()) result.defaults = components(*owner->getStyle(m_styleId));
    return result;
  }

  QColor swatch() const override {
    auto *owner = palette();
    if (!owner) return {};
    auto *frame      = TApp::instance()->getCurrentFrame();
    const auto style = owner->sampleStyle(m_styleId, frame->getFrame());
    const auto color = style->getMainColor();
    return QColor(color.r, color.g, color.b, color.m);
  }

  bool canEdit() const override {
    auto *owner = palette();
    if (!owner || owner->isLocked() ||
        TApp::instance()->getCurrentFrame()->isPlaying())
      return false;
    auto *levels = m_scene->getLevelSet();
    bool found   = false;
    for (int i = 0; i < levels->getLevelCount(); ++i) {
      auto *level = levels->getLevel(i);
      if (levelPalette(level) != owner) continue;
      if (auto *simple = level->getSimpleLevel())
        if (simple->isReadOnly()) return false;
      found = true;
    }
    return found;
  }

  bool edit(int component, const std::vector<Edit> &edits) override {
    if (!canEdit() || edits.empty()) return false;
    auto *owner = palette();
    if (!owner) return false;
    auto before            = owner->getStyleAnimation(m_styleId);
    auto after             = before;
    const auto nativeFrame = [](double frame) {
      return int(std::round(frame));
    };
    const auto sample = [owner, this](int frame) {
      return owner->sampleStyle(m_styleId, frame);
    };
    const auto setValue = [component](TColorStyleP &style, double value) {
      const auto existing = style->getMainColor();
      TPixel32 color      = existing;
      const std::array channels{&color.r, &color.g, &color.b, &color.m};
      *channels[component] = int(std::round(std::clamp(value, 0.0, 255.0)));
      if (color == existing) return;
      style = style->clone();
      style->setMainColor(color);
    };
    if (!editGroupedKeys(before, after, edits, nativeFrame, sample, setValue) ||
        after == before)
      return false;

    TColorStyleP unkeyed(owner->sampleStyle(m_styleId, owner->getFrame()));
    auto undo = std::make_unique<PaletteKeysUndo>(
        owner, m_scene, m_styleId, std::move(before), std::move(after),
        std::move(unkeyed));
    undo->redo();
    TUndoManager::manager()->add(undo.release());
    return true;
  }
};

class ActionKeyTrack final : public FunctionKeyTrack {
  TXshLevelColumnP m_column;
  TXshLevelP m_level;
  otam::Id m_point;

  AMActionBinding binding() const {
    auto *xsheet = TApp::instance()->getCurrentXsheet()->getXsheet();
    if (!xsheet || m_column->getXsheet() != xsheet ||
        m_column->getIndex() < 0 ||
        xsheet->getColumn(m_column->getIndex()) != m_column.getPointer())
      return {};
    auto current = m_column->getAMAction();
    return current.level == m_level ? current : AMActionBinding{};
  }

public:
  ActionKeyTrack(TXshLevelColumn *column, TXshLevel *level, otam::Id point)
      : m_column(column), m_level(level), m_point(point) {}

  Snapshot read() const override {
    Snapshot result;
    const auto current = binding();
    result.available   = bool(current.action);
    if (!current.action) return result;
    const auto channel = current.action->channels.find(m_point);
    if (channel == current.action->channels.end()) return result;
    const double secondsPerFrame = amActionTime(m_column->getXsheet(), 0, 1.0);
    for (const auto &[seconds, xyz] : channel->second.keys) {
      double frame = current.startRow + seconds / secondsPerFrame;
      if (!validFrame(frame)) continue;
      const double integer = std::round(frame);
      // Canonical time rounds stored seconds to ticks. A scene-frame key can
      // therefore land a fraction of a tick from the integer graph position.
      if (otam::isActionTimeAt(
              seconds,
              amActionTime(m_column->getXsheet(), current.startRow, integer)))
        frame = integer;
      result.keys.emplace(frame, Value{xyz[0], xyz[1], xyz[2], 0});
    }
    return result;
  }

  bool canEdit() const override {
    return !TApp::instance()->getCurrentFrame()->isPlaying() &&
           !m_column->isLocked() && binding().action &&
           bool(m_level->getAMLevel()->editableModel());
  }

  bool canReload() const override {
    return canEdit() && !binding().path.isEmpty();
  }

  void reload(QWidget *parent) override {
    if (canReload()) reloadAMActionFromFile(m_column.getPointer(), parent);
  }

  bool edit(int component, const std::vector<Edit> &edits) override {
    if (!canEdit() || edits.empty()) return false;
    auto current = binding();
    auto model   = m_level->getAMLevel()->editableModel();
    if (!current.action || !model) return false;
    for (const auto &edit : edits)
      if (edit.to && *edit.to < current.startRow) return false;

    auto action  = std::make_shared<otam::Action>(*current.action);
    auto channel = action->channels.find(m_point);
    const std::map<double, otam::Vec3> before =
        channel == action->channels.end() ? std::map<double, otam::Vec3>{}
                                          : channel->second.keys;
    auto after = before;
    try {
      const auto time = [&](double frame) {
        return otam::canonicalActionTime(
            amActionTime(m_column->getXsheet(), current.startRow, frame));
      };
      const auto sample = [&](double seconds) {
        return otam::samplePositionOffset(*action, m_point, seconds);
      };
      const auto setValue = [component](otam::Vec3 &value, double v) {
        value[component] = v;
      };
      if (!editGroupedKeys(before, after, edits, time, sample, setValue) ||
          after == before)
        return false;
      if (after.empty()) {
        action->channels.erase(m_point);
      } else if (channel == action->channels.end()) {
        for (const auto &[seconds, value] : after)
          otam::setPositionKey(*action, *model, m_point, seconds, value);
      } else {
        action->duration = std::max(action->duration, after.rbegin()->first);
        channel->second.keys = std::move(after);
      }
      otam::validateAction(*action, *model);
    } catch (const std::exception &error) {
      DVGui::warning(QString::fromUtf8(error.what()));
      return false;
    }

    current.action = std::move(action);
    changeAMActionBinding(m_column.getPointer(), std::move(current),
                          QObject::tr("Edit A:M Action Keys"));
    return true;
  }
};

class VectorActionKeyTrack final : public FunctionKeyTrack {
  TXshLevelColumnP m_column;
  TXshCell m_cell;
  VectorAction::Point m_point;
  std::vector<int> m_topology;

  VectorActionBinding binding() const {
    auto *xsheet    = TApp::instance()->getCurrentXsheet()->getXsheet();
    const int index = m_column->getIndex();
    if (!xsheet || m_column->getXsheet() != xsheet || index < 0 ||
        xsheet->getColumn(index) != m_column.getPointer())
      return {};
    auto current = m_column->getVectorAction();
    return current.matches(m_cell) && current.action->topology == m_topology
               ? current
               : VectorActionBinding{};
  }

public:
  VectorActionKeyTrack(TXshLevelColumn *column,
                       const VectorActionBinding &binding,
                       VectorAction::Point point)
      : m_column(column)
      , m_cell(binding.level, binding.fid)
      , m_point(point)
      , m_topology(binding.action->topology) {}

  Snapshot read() const override {
    Snapshot result;
    const auto current = binding();
    result.available   = bool(current.action);
    if (!current.action) return result;
    const auto channel = current.action->channels.find(m_point);
    if (channel == current.action->channels.end()) return result;
    auto *xsheet = m_column->getXsheet();
    for (const auto &[seconds, point] : channel->second) {
      double frame = vectorActionFrame(xsheet, current.startRow, seconds);
      if (!validFrame(frame)) continue;
      const double integer = std::round(frame);
      if (vectorActionTime(xsheet, current.startRow, integer) == seconds)
        frame = integer;
      result.keys.emplace(frame, Value{point.x, point.y, 0, 0});
    }
    return result;
  }

  bool canEdit() const override {
    return !TApp::instance()->getCurrentFrame()->isPlaying() &&
           !m_column->isLocked() && bool(binding().action);
  }

  bool canReload() const override {
    return canEdit() && !binding().path.isEmpty();
  }

  void reload(QWidget *parent) override {
    if (canReload()) reloadVectorActionFromFile(m_column.getPointer(), parent);
  }

  bool edit(int component, const std::vector<Edit> &edits) override {
    if (!canEdit() || edits.empty() || component < 0 || component > 1)
      return false;
    auto current       = binding();
    auto action        = std::make_shared<VectorAction>(*current.action);
    const auto channel = action->channels.find(m_point);
    const auto before = channel == action->channels.end() ? VectorAction::Keys()
                                                          : channel->second;
    auto after        = before;
    for (const auto &edit : edits)
      if ((edit.to && *edit.to < current.startRow) ||
          (edit.from && *edit.from < current.startRow))
        return false;
    const auto time = [&](double frame) {
      return vectorActionTime(m_column->getXsheet(), current.startRow, frame);
    };
    const auto sample = [&](double seconds) {
      return action->sample(m_point, seconds);
    };
    const auto setValue = [component](TPointD &value, double coordinate) {
      (component == 0 ? value.x : value.y) = coordinate;
    };
    if (!editGroupedKeys(before, after, edits, time, sample, setValue) ||
        after == before)
      return false;
    try {
      action->replaceKeys(m_point, std::move(after));
    } catch (const TException &error) {
      DVGui::warning(QString::fromStdWString(error.getMessage()));
      return false;
    }
    current.action = std::move(action);
    auto *app      = TApp::instance();
    changeVectorActionBinding(m_column.getPointer(), current,
                              app->getCurrentScene(), app->getCurrentXsheet(),
                              QObject::tr("Edit Vector Action Keys"));
    return true;
  }
};

}  // namespace

void showPaletteStyleInFunctionEditor(TPalette *palette, int styleId) {
  auto *app   = TApp::instance();
  auto *scene = app->getCurrentScene()->getScene();
  auto *level = app->getCurrentLevel()->getLevel();
  if (!scene || !palette || levelPalette(level) != palette ||
      !hasStyle(palette, styleId))
    return;
  if (auto *editor = openEditor()) {
    const QString label =
        QObject::tr("%1 · %2 [%3]")
            .arg(QString::fromStdWString(level->getName()))
            .arg(QString::fromStdWString(palette->getStyle(styleId)->getName()))
            .arg(styleId);
    editor->showKeyTrack(
        "palette:" + sourceId(level, styleId), QObject::tr("Palettes"), label,
        {QObject::tr("Red"), QObject::tr("Green"), QObject::tr("Blue"),
         QObject::tr("Alpha")},
        std::make_shared<PaletteKeyTrack>(level, scene, styleId));
  }
}

void showAMActionInFunctionEditor(TXshLevelColumn *column,
                                  const std::vector<std::int64_t> &pointIds) {
  const auto binding = column->getAMAction();
  if (!binding.action || pointIds.empty()) return;
  auto *editor = openEditor();
  if (!editor) return;
  for (auto id : pointIds) {
    const QString label =
        QObject::tr("%1 · Column %2 · CP %3")
            .arg(QString::fromStdWString(binding.level->getName()))
            .arg(column->getIndex() + 1)
            .arg(qlonglong(id));
    editor->showKeyTrack(
        "action:" + sourceId(column, id) + ':' +
            QString::number(quintptr(binding.level.getPointer()), 16),
        QObject::tr("Actions"), label,
        {QObject::tr("X Offset"), QObject::tr("Y Offset"),
         QObject::tr("Z Offset")},
        std::make_shared<ActionKeyTrack>(column, binding.level.getPointer(),
                                         id));
  }
}

void showVectorActionInFunctionEditor(TXshLevelColumn *column,
                                      std::pair<int, int> point) {
  const auto binding = column->getVectorAction();
  if (!binding.action || !binding.action->channels.count(point)) return;
  auto *editor = openEditor();
  if (!editor) return;
  const QString label =
      QObject::tr("%1 · Column %2 · Stroke %3, Point %4")
          .arg(QString::fromStdWString(binding.level->getName()))
          .arg(column->getIndex() + 1)
          .arg(point.first + 1)
          .arg(point.second + 1);
  const auto id =
      sourceId(column, point.first) + ':' + QString::number(point.second) +
      ':' + sourceId(binding.level.getPointer(), binding.fid.getNumber()) +
      ':' + binding.fid.getLetter();
  editor->showKeyTrack(
      "vectorAction:" + id, QObject::tr("Actions"), label,
      {QObject::tr("X Offset"), QObject::tr("Y Offset")},
      std::make_shared<VectorActionKeyTrack>(column, binding, point));
}

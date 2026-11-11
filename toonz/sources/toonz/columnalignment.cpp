#include "columnselection.h"
#include "tapp.h"

#include "tdoubleparam.h"
#include "tundo.h"
#include "toonz/amlevelrender.h"
#include "toonz/doubleparamcmd.h"
#include "toonz/stage.h"
#include "toonz/tframehandle.h"
#include "toonz/tobjecthandle.h"
#include "toonz/tscenehandle.h"
#include "toonz/tstageobject.h"
#include "toonz/tstageobjecttree.h"
#include "toonz/txshchildlevel.h"
#include "toonz/txsheethandle.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/txshleveltypes.h"

#include <cmath>
#include <map>
#include <optional>

namespace {
using namespace VectorAlignment;

bool nativeModel(const TXshCell &cell) {
  return cell.m_level->getAMLevel() || cell.m_level->getGlbLevel();
}

bool isAlignable(TXsheet *sheet, int row, int col) {
  if (!sheet || row < 0 || col < 0 || col >= sheet->getColumnCount())
    return false;
  auto *column = sheet->getColumn(col);
  if (!column || !column->isCamstandVisible() || column->isLocked() ||
      !column->getOpacity() || !column->getLevelColumn())
    return false;
  const auto cell = sheet->getCell(row, col);
  if (cell.isEmpty()) return false;
  const auto *level = cell.getSimpleLevel();
  if (!nativeModel(cell) && !cell.getChildLevel() &&
      !(level && (level->getType() & LEVELCOLUMN_XSHLEVEL)))
    return false;
  auto *object = sheet->getStageObject(TStageObjectId::ColumnId(col));
  if (object->getStatus() != TStageObject::XY) return false;

  // Expression and file channels can override values assigned to their keys.
  const auto frame = object->paramsTime(row);
  for (auto channel :
       {TStageObject::T_X, TStageObject::T_Y, TStageObject::T_Z}) {
    if (channel == TStageObject::T_Z && !nativeModel(cell)) continue;
    auto *param = object->getParam(channel);
    int key     = param->getClosestKeyframe(frame);
    if (key >= 0 && param->getKeyframe(key).m_frame > frame) --key;
    if (key < 0) continue;
    const auto type = param->getKeyframe(key).m_type;
    if (type == TDoubleKeyframe::Expression || type == TDoubleKeyframe::File)
      return false;
  }
  return true;
}

std::optional<TRectD> stageBounds(TXsheet *sheet, int row, int col,
                                  bool spatial) {
  if (spatial) {
    const auto mesh = prepareNativeModelGeometry(sheet, col, row, false);
    if (!mesh) return {};
    std::optional<TRectD> bounds;
    const auto includePoint = [&](const otam::Vec3 &p) {
      if (!bounds) bounds.emplace(p[0], p[1], p[0], p[1]);
      bounds->x0 = std::min(bounds->x0, p[0]);
      bounds->y0 = std::min(bounds->y0, p[1]);
      bounds->x1 = std::max(bounds->x1, p[0]);
      bounds->y1 = std::max(bounds->y1, p[1]);
    };
    for (const auto &triangle : mesh->triangles)
      for (const auto &p : triangle.vertices) includePoint(p);
    if (!bounds) {
      // An A:M level can contain only splines. Use the posed point heads
      // when there is no patch mesh to measure.
      if (const auto model = amModelAt(sheet, col, row)) {
        const auto placement = amModelPlacement(sheet, col, row);
        for (std::size_t i = 0; i < model->points.size(); ++i)
          if (model->points[i].head == i)
            includePoint(otam::transform(placement, model->points[i].position));
      }
    }
    return bounds;
  }
  const auto cell = sheet->getCell(row, col);
  TRectD bounds;
  if (auto *child = cell.getChildLevel())
    bounds = child->getXsheet()->getBBox(cell.getFrameId().getNumber() - 1);
  else
    bounds =
        TScale(Stage::inch) * cell.getSimpleLevel()->getBBox(cell.getFrameId());
  if (bounds.isEmpty()) return {};
  return sheet->getPlacement(TStageObjectId::ColumnId(col), row) * bounds;
}

struct ColumnTarget {
  TStageObject *object;
  bool spatial;
  TAffine parent2D;
  otam::Matrix parent3D, inverse3D;
  std::vector<TStageObjectId> ancestors;
};

struct StageShift {
  TPointD planar;
  otam::Vec3 spatial{};
};

struct PositionChange {
  TDoubleParam *param;
  double frame, value;
};

void notifyColumnAlignment(TXsheet *sheet) {
  sheet->getStageObjectTree()->invalidateAll();
  auto *app = TApp::instance();
  app->getCurrentObject()->notifyObjectIdChanged(false);
  app->getCurrentScene()->notifySceneChanged();
  app->getCurrentXsheet()->notifyXsheetChanged();
}

// Bracket the standard keyframe undos so every curve is restored before
// refresh.
class ColumnAlignmentRefresh final : public TUndo {
  TXsheetP m_sheet;

public:
  explicit ColumnAlignmentRefresh(TXsheet *sheet) : m_sheet(sheet) {}
  int getSize() const override { return sizeof(*this); }
  void undo() const override {
    if (m_isLastInBlock) notifyColumnAlignment(m_sheet.getPointer());
  }
  void redo() const override {
    if (m_isLastInRedoBlock) notifyColumnAlignment(m_sheet.getPointer());
  }
  QString getHistoryString() override {
    return QObject::tr("Align or Distribute Columns");
  }
};

bool canAlignColumns(TApp *app) {
  return app->getCurrentFrame()->isEditingScene() &&
         !app->getCurrentFrame()->isPlaying();
}
}  // namespace

std::size_t TColumnSelection::alignmentCount() const {
  auto *app = TApp::instance();
  if (!canAlignColumns(app)) return 0;
  auto *sheet   = app->getCurrentXsheet()->getXsheet();
  const int row = app->getCurrentFrame()->getFrame();
  return std::count_if(m_indices.begin(), m_indices.end(),
                       [&](int col) { return isAlignable(sheet, row, col); });
}

void TColumnSelection::align(VectorAlignment::Type type) {
  auto *app = TApp::instance();
  if (!canAlignColumns(app)) return;
  auto *sheet   = app->getCurrentXsheet()->getXsheet();
  const int row = app->getCurrentFrame()->getFrame();

  std::vector<ColumnTarget> targets;
  std::vector<TRectD> boxes;
  for (int col : m_indices) {
    if (!isAlignable(sheet, row, col)) continue;
    const auto id      = TStageObjectId::ColumnId(col);
    const bool spatial = nativeModel(sheet->getCell(row, col));
    try {
      const auto bounds = stageBounds(sheet, row, col, spatial);
      if (!bounds || !std::isfinite(bounds->x0) || !std::isfinite(bounds->x1) ||
          !std::isfinite(bounds->y0) || !std::isfinite(bounds->y1))
        continue;

      auto *object = sheet->getStageObject(id);
      auto planar  = object->getParentPlacement(row);
      planar.a13 = planar.a23 = 0;
      if (!spatial && std::abs(planar.det()) < 1e-12) continue;

      const auto parentId = object->getParent();
      auto parent3D =
          parentId == TStageObjectId::NoneId
              ? otam::identity()
              : sheet->getStageObject(parentId)->getAMPlacement(row);
      parent3D[3] = parent3D[7] = parent3D[11] = 0;
      ColumnTarget target{object,
                          spatial,
                          planar,
                          parent3D,
                          spatial ? otam::inverse(parent3D) : otam::identity(),
                          {}};
      for (auto ancestor = parentId; ancestor != TStageObjectId::NoneId;
           ancestor      = sheet->getStageObject(ancestor)->getParent())
        target.ancestors.push_back(ancestor);
      targets.push_back(std::move(target));
      boxes.push_back(*bounds);
    } catch (const std::exception &) {
      // An unavailable model or singular spatial parent has no usable bounds.
    }
  }

  const auto deltas = offsets(boxes, type, alignmentMethod());
  // Compute parent moves first so selected descendants get their own intended
  // stage displacement after accounting for the motion they already inherit.
  std::vector<std::size_t> order(targets.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
    return targets[a].ancestors.size() < targets[b].ancestors.size();
  });
  std::map<TStageObjectId, StageShift> shifts;
  std::vector<PositionChange> changes;
  for (auto i : order) {
    const auto &target = targets[i];
    const auto &delta  = deltas[i];
    StageShift inherited;
    for (const auto &id : target.ancestors) {
      auto shift = shifts.find(id);
      if (shift == shifts.end()) continue;
      inherited = shift->second;
      break;
    }

    otam::Vec3 local{};
    if (target.spatial) {
      local = otam::transform(
          target.inverse3D,
          {{delta.x - inherited.spatial[0], delta.y - inherited.spatial[1],
            -inherited.spatial[2]}});
    } else {
      const auto point = target.parent2D.inv() * (delta - inherited.planar);
      local            = {{point.x, point.y, 0}};
    }
    const auto spatialShift = otam::transform(target.parent3D, local);
    inherited.planar += target.parent2D * TPointD(local[0], local[1]);
    for (int axis = 0; axis < 3; ++axis)
      inherited.spatial[axis] += spatialShift[axis];
    shifts.emplace(target.object->getId(), inherited);

    const double frame                     = target.object->paramsTime(row);
    const TStageObject::Channel channels[] = {
        TStageObject::T_X, TStageObject::T_Y, TStageObject::T_Z};
    for (int axis = 0; axis < 3; ++axis) {
      if (!std::isfinite(local[axis])) return;
      if (std::abs(local[axis]) < 1e-8) continue;
      auto *param         = target.object->getParam(channels[axis]);
      const double amount = axis == 2 ? local[axis] : local[axis] / Stage::inch;
      const double value  = param->getValue(frame) + amount;
      if (!std::isfinite(value)) return;
      changes.push_back({param, frame, value});
    }
  }
  if (changes.empty()) return;

  {
    TUndoScopedBlock block;
    auto *undo = TUndoManager::manager();
    undo->add(new ColumnAlignmentRefresh(sheet));
    for (const auto &change : changes)
      KeyframeSetter::setValue(change.param, change.frame, change.value);
    undo->add(new ColumnAlignmentRefresh(sheet));
  }
  notifyColumnAlignment(sheet);
}

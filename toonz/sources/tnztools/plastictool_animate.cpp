

// TnzCore includes
#include "tundo.h"

// TnzLib includes
#include "toonz/tobjecthandle.h"
#include "toonz/tscenehandle.h"

#include "plastictool.h"

using namespace PlasticToolLocals;

//****************************************************************************************
//    Undo  definitions
//****************************************************************************************

namespace {

class AnimateValuesUndo final : public TUndo {
  int m_row, m_col;  //!< Xsheet coordinates
  int m_v;           //!< Moved vertex
  double m_frame;

public:
  SkDKey m_oldValues, m_newValues;  //!< Keyframe values

public:
  AnimateValuesUndo(int v)
      : m_row(::row()), m_col(::column()), m_v(v), m_frame(::sdFrame()) {}

  // Again, not accurate. We should get in the details of SkDF... So, let's say
  // around 10 kB - max 10k instances in the standard undos pool.
  int getSize() const override { return 10 << 10; }

  void redo() const override {
    PlasticTool::TemporaryActivation tempActivate(m_row, m_col);

    if (m_v >= 0) l_plasticTool.setSkeletonSelection(m_v);

    l_suspendParamsObservation =
        true;  // Coalesce params change notifications into one

    l_plasticTool.deformation()->deleteKeyframe(m_frame);
    l_plasticTool.deformation()->setKeyframe(m_newValues);

    l_suspendParamsObservation = false;
    l_plasticTool.onChange();
  }

  void undo() const override {
    PlasticTool::TemporaryActivation tempActivate(m_row, m_col);

    if (m_v >= 0) l_plasticTool.setSkeletonSelection(m_v);

    l_suspendParamsObservation =
        true;  // Coalesce params change notifications into one

    l_plasticTool.deformation()->deleteKeyframe(m_frame);
    l_plasticTool.deformation()->setKeyframe(m_oldValues);

    l_suspendParamsObservation = false;
    l_plasticTool.onChange();
  }
};

}  // namespace

//****************************************************************************************
//    PlasticTool  functions
//****************************************************************************************

void PlasticTool::mouseMove_animate(const TPointD &pos, const TMouseEvent &me) {
  // Track mouse position
  m_pos = pos;  // Needs to be done now - ensures m_pos is valid

  m_svHigh = m_seHigh = -1;  // Reset highlighted primitives

  if (m_sd) {
    m_svHigh = closestRigJoint(deformedSkeleton(), pos);

    invalidate();
  }
}

//------------------------------------------------------------------------

void PlasticTool::leftButtonDown_animate(const TPointD &pos,
                                         const TMouseEvent &me) {
  // Track mouse position
  m_pressedPos = m_pos = pos;

  if (isAMTarget()) mouseMove_animate(pos, me);
  setSkeletonSelection(m_svHigh);

  if (m_svSel.hasSingleObject()) {
    // Store original vertex position and keyframe values
    if (isAMTarget()) {
      m_amDragAnchor = deformedSkeleton().vertex(m_svSel).position3D();
      m_amMouseStart = pos;
    }
    m_sd->getKeyframeAt(::sdFrame(), m_pressedSkDF);
  }

  invalidate();
}

//------------------------------------------------------------------------

void PlasticTool::leftButtonDrag_animate(const TPointD &pos,
                                         const TMouseEvent &me) {
  // Track mouse position
  m_pos = pos;

  if (isAMTarget()) {
    if (const auto target = amDragTarget(me)) {
      const double time = ::sdFrame();
      auto *vd          = m_sd->vertexDeformation(::skeletonId(), m_svSel);
      l_suspendParamsObservation = true;
      const bool keepDistance    = m_keepDistance.getValue();
      if (skeleton()->vertex(m_svSel).parent() < 0) {
        for (auto channel : {SkVD::ROOT_X, SkVD::ROOT_Y, SkVD::ROOT_Z})
          ::setKeyframe(vd->m_params[channel], time);
      } else {
        ::setKeyframe(vd->m_params[SkVD::ANGLE], time);
        ::setKeyframe(vd->m_params[SkVD::ELEVATION], time);
        if (!keepDistance) ::setKeyframe(vd->m_params[SkVD::DISTANCE], time);
      }
      m_sd->updateSpatialPosition(::skeletonId(), time, m_svSel, *target,
                                  keepDistance);
      l_suspendParamsObservation = false;
      m_deformedSkeleton.invalidate();
      invalidate();
    }
    return;
  }

  if (m_sd && m_svSel.hasSingleObject() &&
      m_svSel > 0)  // Avoid move if vertex is root
  {
    l_suspendParamsObservation = true;  // Automatic params notification happen
    // twice (1 x param) - dealing with it manually

    double frame = ::sdFrame();

    // First, retrieve selected vertex's deformation
    SkVD *vd = m_sd->vertexDeformation(::skeletonId(), m_svSel);
    assert(vd);

    // Move selected branch
    if (m_keepDistance.getValue()) {
      ::setKeyframe(vd->m_params[SkVD::ANGLE],
                    frame);  // Set a keyframe for it. It must be done
                             // to set the correct function interpolation
                             // type and other stuff.
      m_sd->updateAngle(*skeleton(), deformedSkeleton(), frame, m_svSel, pos);
    } else {
      ::setKeyframe(vd->m_params[SkVD::ANGLE],
                    frame);  // Same here. NOTE: Not setting a frame on
      ::setKeyframe(vd->m_params[SkVD::DISTANCE],
                    frame);  // vd directly due to SkVD::SO

      m_sd->updatePosition(*skeleton(), deformedSkeleton(), frame, m_svSel,
                           pos);
    }

    l_suspendParamsObservation = false;

    // onChange();                                                     // Due to
    // a nasty Function Editor dependency,
    // it's better to call the following directly
    m_deformedSkeleton.invalidate();
    invalidate();
  }
}

//------------------------------------------------------------------------

void PlasticTool::leftButtonUp_animate(const TPointD &pos,
                                       const TMouseEvent &me) {
  // Track mouse position
  m_pos = pos;

  const bool native = isAMTarget();
  if (m_svSel.hasSingleObject() && m_dragged &&
      (native ? bool(m_amDragAnchor) : m_svSel > 0)) {
    if (native) leftButtonDrag_animate(pos, me);
    // Set a keyframe to each skeleton vertex, if that was requested
    if (m_globalKey.getValue())
      ::setKeyframe(m_sd, ::sdFrame());  // Already invokes keyframes rebuild
    else
      stageObject()->updateKeyframes();  // Otherwise, must be explicit

    // Add a corresponding undo
    AnimateValuesUndo *undo = new AnimateValuesUndo(m_svSel);

    undo->m_oldValues = m_pressedSkDF;
    m_sd->getKeyframeAt(::sdFrame(), undo->m_newValues);

    TUndoManager::manager()->add(undo);

    // This is needed to refresh the xsheet (there may be new keyframes)
    TTool::getApplication()->getCurrentObject()->notifyObjectIdChanged(false);
    if (native) TTool::getApplication()->getCurrentScene()->setDirtyFlag(true);
  }

  // In case one of the vertices is attached (as a hook) to an external
  // position,
  // we need to update the whole skeleton according to the updated vertex.
  updateMatrix();
  invalidate();
}

//------------------------------------------------------------------------

void PlasticTool::addContextMenuActions_animate(QMenu *menu) {
  bool ret = true;

  if (m_sd.getPointer() == nullptr) return;
  if (!m_svSel.isEmpty()) {
    QAction *setKey = menu->addAction(tr("Set Key"));
    ret = ret && connect(setKey, SIGNAL(triggered()), &l_plasticTool,
                         SLOT(setKey_undo()));

    QAction *setRestKey = menu->addAction(tr("Set Rest Key"));
    ret = ret && connect(setRestKey, SIGNAL(triggered()), &l_plasticTool,
                         SLOT(setRestKey_undo()));
  }

  QAction *setGlobalKey = menu->addAction(tr("Set Global Key"));
  ret = ret && connect(setGlobalKey, SIGNAL(triggered()), &l_plasticTool,
                       SLOT(setGlobalKey_undo()));

  QAction *setGlobalRestKey = menu->addAction(tr("Set Global Rest Key"));
  ret = ret && connect(setGlobalRestKey, SIGNAL(triggered()), &l_plasticTool,
                       SLOT(setGlobalRestKey_undo()));

  menu->addSeparator();

  assert(ret);
}

//------------------------------------------------------------------------

void PlasticTool::keyFunc_undo(void (PlasticTool::*keyFunc)()) {
  assert(m_svSel.objects().size() <= 1);

  double frame = ::sdFrame();

  AnimateValuesUndo *undo = new AnimateValuesUndo(m_svSel);
  m_sd->getKeyframeAt(frame, undo->m_oldValues);

  (this->*keyFunc)();

  m_sd->getKeyframeAt(frame, undo->m_newValues);

  TUndoManager::manager()->add(undo);
}

//------------------------------------------------------------------------

void PlasticTool::draw_animate() {
  double pixelSize = getPixelSize();

  PlasticSkeleton &deformedSkeleton = this->deformedSkeleton();

  // Draw deformed skeleton
  if (m_sd) {
    drawOnionSkinSkeletons_animate(pixelSize);
    drawSkeleton(deformedSkeleton, pixelSize);
    drawSelections(m_sd, deformedSkeleton, pixelSize);
    if (!isAMTarget()) drawAngleLimits(m_sd, m_skelId, m_svSel, pixelSize);
  }

  drawHighlights(m_sd, &deformedSkeleton, pixelSize);
}

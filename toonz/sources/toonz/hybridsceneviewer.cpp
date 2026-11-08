#include "toonz/amlevelrender.h"
#include "toonz/txshleveltypes.h"
#include "hybridsceneviewer.h"

#include "tapp.h"
#include "liverendercontroller.h"
#include "livegpu.h"
#include "toonz/toonzscene.h"
#include "toonz/sceneproperties.h"
#include "toutputproperties.h"
#include "toonz/scenefx.h"
#include "toonz/fxdag.h"
#include "toonz/tscenehandle.h"
#include "toonz/tfxhandle.h"
#include "toonz/tobjecthandle.h"
#include "toonz/txshlevelhandle.h"
#include "toonz/txshlevel.h"
#include "toonz/childstack.h"
#include "toonz/palettecontroller.h"
#include "toonz/tpalettehandle.h"
#include "menubarcommandids.h"
#include "tools/toolhandle.h"
#include <QLabel>
#include <QTimer>
#include <QAction>
#include "texception.h"

#include "../stdfx/t3dsource.h"
#include "toonz/tframehandle.h"
#include "toonz/tstageobject.h"
#include "toonz/tstageobjecttree.h"
#include "toonz/txsheethandle.h"
#include "toonz/txsheet.h"
#include "toonz/txshzeraryfxcolumn.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions>

#include <algorithm>
#include <exception>
#include <vector>

namespace {
// Compatibility-profile state is required by the inherited Viewer too. Keep
// the incoming shader program, matrix mode, model-view stack and attributes.
class ModelDrawScope {
  GLint m_matrixMode            = GL_MODELVIEW;
  GLint m_program               = 0;
  QOpenGLFunctions *m_functions = nullptr;

public:
  ModelDrawScope() {
    glGetIntegerv(GL_MATRIX_MODE, &m_matrixMode);
    glGetIntegerv(GL_CURRENT_PROGRAM, &m_program);
    m_functions = QOpenGLContext::currentContext()->functions();
    m_functions->glUseProgram(0);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_FOG);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthRange(0.0, 1.0);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glClearDepth(1.0);
    // Initial mesh-to-mesh depth only. The existing 2D painter does not yet
    // supply compatible depth. Shared 2D/3D occlusion is a separate gate.
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }
  ~ModelDrawScope() {
    glPopMatrix();
    glMatrixMode(m_matrixMode);
    glPopAttrib();
    m_functions->glUseProgram(m_program);
  }
};
}  // namespace

HybridSceneViewer::HybridSceneViewer(ImageUtils::FullScreenWidget *parent)
    : SceneViewer(parent) {
  setProperty("ot3dHybridViewer", true);
  m_liveStatus = new QLabel(this);
  m_liveStatus->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_liveStatus->setStyleSheet(
      "QLabel { background: rgba(25,25,25,200); color: #eee; padding: 4px; }");
  m_liveStatus->move(8, 8);
  m_liveStatus->hide();
  m_liveGpu.reset(new otglb::LiveGpu);
  m_liveController.reset(new LiveRenderController(this));
  m_liveController->completed =
      [this](std::shared_ptr<const otglb::LiveFrame> frame,
             const QString &error) {
        if (!m_liveRender) return;
        if (frame) {
          m_liveFrame  = std::move(frame);
          m_liveFailed = false;
        }
        if (!error.isEmpty()) {
          m_liveFailed = true;
          reportDiagnostic(error);
        }
        m_liveStatus->setText(error.isEmpty() ? tr("Live Render — GPU")
                                              : error);
        m_liveStatus->adjustSize();
        update();
      };
  auto *app  = TApp::instance();
  auto dirty = [this] { invalidateLive(); };
  connect(app->getCurrentScene(), &TSceneHandle::sceneChanged, this, dirty);
  connect(app->getCurrentScene(), &TSceneHandle::sceneSwitched, this, [this] {
    m_liveFrame.reset();
    invalidateLive();
  });
  connect(app->getCurrentFrame(), &TFrameHandle::frameSwitched, this, dirty);
  connect(app->getCurrentFx(), &TFxHandle::fxChanged, this, dirty);
  connect(app->getCurrentObject(), &TObjectHandle::objectChanged, this, dirty);
  connect(app->getCurrentXsheet(), &TXsheetHandle::xsheetChanged, this, dirty);
  connect(app->getCurrentXsheet(), &TXsheetHandle::xsheetSwitched, this,
          [this] {
            m_liveFrame.reset();
            invalidateLive();
          });
  connect(app->getCurrentLevel(), &TXshLevelHandle::xshLevelChanged, this,
          dirty);
  connect(app->getPaletteController()->getCurrentLevelPalette(),
          &TPaletteHandle::colorStyleChanged, this, dirty);
}
void HybridSceneViewer::initializeGL() {
  SceneViewer::initializeGL();
  connect(
      context(), &QOpenGLContext::aboutToBeDestroyed, this,
      [this] { releaseLiveGpu(); }, Qt::DirectConnection);
}
HybridSceneViewer::~HybridSceneViewer() { releaseLiveGpu(); }
void HybridSceneViewer::releaseLiveGpu() {
  if (!context()) return;
  makeCurrent();
  m_liveGpu->release();
  doneCurrent();
}
void HybridSceneViewer::invalidateLive() {
  m_liveDirty  = true;
  m_liveFailed = false;
  m_liveController->invalidate();
  if (m_liveRender) update();
}
void HybridSceneViewer::setLiveRenderEnabled(bool enabled) {
  if (m_liveRender == enabled) return;
  m_liveRender = enabled;
  m_liveStatus->setVisible(enabled);
  if (!enabled) {
    m_liveController->invalidate();
    m_liveFrame.reset();
  } else {
    enablePreview(NO_PREVIEW);
    invalidateLive();
  }
  update();
}
bool HybridSceneViewer::drawLive() {
  m_liveStatus->show();
  auto *app = TApp::instance();
  if (isPreviewEnabled() || is3DView() || m_drawEditingLevel ||
      m_drawCameraTest ||
      app->getCurrentScene()->getScene()->getChildStack()->getAncestorCount() >
          0) {
    m_liveStatus->setText(
        tr("Live Render paused — use camera view in scene editing"));
    m_liveStatus->adjustSize();
    return false;
  }
  if (m_liveDirty && !m_liveController->busy()) {
    m_liveDirty = false;
    try {
      auto *scene     = app->getCurrentScene()->getScene();
      auto *xsheet    = app->getCurrentXsheet()->getXsheet();
      const int frame = app->getCurrentFrame()->getFrame();
      auto *output    = xsheet->getFxDag()->getOutputFx(0);
      TFxP root       = output && output->getInputPortCount()
                            ? output->getInputPort(0)->getFx()
                            : nullptr;
      if (!root) root = xsheet->getFxDag()->getXsheetFx();
      // Stage-image coordinates match the ordinary camera-view transform.
      auto snapshot =
          buildSceneFx(scene, frame, xsheet, root, BSFX_COLUMN_TR, false);
      TRenderSettings settings =
          scene->getProperties()->getPreviewProperties()->getRenderSettings();
      settings.m_bpp              = 128;
      settings.m_linearColorSpace = true;
      m_liveController->prepare(snapshot ? snapshot->clone(true) : TFxP(),
                                frame, settings);
      m_liveStatus->setText(tr("Live Render — updating"));
    } catch (const TException &e) {
      m_liveFailed = true;
      m_liveStatus->setText(QString::fromStdWString(e.getMessage()));
    } catch (const std::exception &e) {
      m_liveFailed = true;
      m_liveStatus->setText(QString::fromUtf8(e.what()));
    }
    m_liveStatus->adjustSize();
  }
  if (!m_liveFrame || m_liveFailed) return false;
  QMatrix4x4 matrix;
  const auto a = getViewMatrix();
  matrix(0, 0) = float(2 * a.a11 / width());
  matrix(0, 1) = float(2 * a.a12 / width());
  matrix(0, 3) = float(2 * a.a13 / width());
  matrix(1, 0) = float(2 * a.a21 / height());
  matrix(1, 1) = float(2 * a.a22 / height());
  matrix(1, 3) = float(2 * a.a23 / height());
  QString error;
  ModelDrawScope scope;
  if (m_liveGpu->draw(m_liveFrame, matrix, width(), height(), error)) {
    if (!m_liveDirty && !m_liveController->busy()) {
      m_liveStatus->setText(tr("Live Render — GPU"));
      m_liveStatus->adjustSize();
    }
    return true;
  }
  m_liveFailed = true;
  m_liveStatus->setText(error);
  m_liveStatus->adjustSize();
  reportDiagnostic(error);
  return false;
}

void HybridSceneViewer::reportDiagnostic(const QString &message) {
  if (m_reportedDiagnostics.contains(message)) return;
  if (m_reportedDiagnostics.size() < 64) {
    m_reportedDiagnostics.insert(message);
    qWarning().noquote() << "OT3D Viewer:" << message;
  }
  setToolTip(message);
}

void HybridSceneViewer::drawScene() {
  SceneViewer::drawScene();
  if (m_liveRender && !isScenePicking() && drawLive()) return;
  // Preview already evaluates the final FX tree. Avoid double drawing and
  // leave level editing, picking and the camera-test path with the base class.
  if (m_drawEditingLevel || m_drawCameraTest || isPreviewEnabled() ||
      isScenePicking())
    return;

  auto *app       = TApp::instance();
  TXsheet *xsheet = app->getCurrentXsheet()->getXsheet();
  const int frame = app->getCurrentFrame()->getFrame();
  if (!xsheet) return;

  struct Item {
    std::shared_ptr<const otglb::ViewerGeometry> geometry;
    TAffine placement;
    double z       = 0;
    double opacity = 1;
  };
  std::vector<Item> items;
  std::size_t triangles = 0;
  for (int i = 0; i < xsheet->getColumnCount(); ++i) {
    auto *column = xsheet->getColumn(i);
    if (!column || !column->isCamstandVisible() ||
        xsheet->getCell(frame, i).isEmpty() || column->getOpacity() == 0)
      continue;
    const auto cell = xsheet->getCell(frame, i);
    if (isNativeModelLevel(cell.m_level->getType())) {
      try {
        auto snapshot =
            prepareNativeModelGeometry(xsheet, i, frame, !is3DView());
        if (!snapshot) continue;
        for (const auto &warning : snapshot->warnings)
          reportDiagnostic(QString::fromStdString(warning));
        if (snapshot->triangles.size() > 1000000 - triangles) {
          reportDiagnostic(
              tr("Scene exceeds the interactive triangle budget."));
          break;
        }
        auto geometry = std::make_shared<otglb::ViewerGeometry>();
        geometry->stageUnitsPerModelUnit = 1.0;
        for (const auto &triangle : snapshot->triangles)
          geometry->triangles.push_back({triangle.vertices, triangle.color});
        triangles += geometry->triangles.size();
        // These vertices are already placed. Never apply the stage transform
        // twice.
        items.push_back({geometry, TAffine(), 0, column->getOpacity() / 255.0});
      } catch (const std::exception &error) {
        reportDiagnostic(QString::fromUtf8(error.what()));
      }
      continue;
    }
    auto *zerary = column->getZeraryFxColumn();
    if (!zerary) continue;
    auto *source = dynamic_cast<T3DViewerSource *>(
        zerary->getZeraryColumnFx()->getZeraryFx());
    if (!source) continue;
    try {
      auto geometry = source->get3DViewerGeometry(frame);
      if (!geometry) continue;
      for (const auto &warning : geometry->warnings)
        reportDiagnostic(QString::fromStdString(warning));
      if (geometry->triangles.empty()) continue;
      if (geometry->triangles.size() > 1000000 - triangles) {
        reportDiagnostic(tr("Scene exceeds the interactive triangle budget."));
        break;
      }
      auto *object      = xsheet->getStageObject(TStageObjectId::ColumnId(i));
      TAffine placement = object->getPlacement(frame);
      if (!is3DView()) {
        auto *camera = xsheet->getStageObject(
            xsheet->getStageObjectTree()->getCurrentCameraId());
        if (!TStageObject::perspective(
                placement, camera->getPlacement(frame), camera->getZ(frame),
                object->getPlacement(frame), object->getZ(frame),
                object->getGlobalNoScaleZ()))
          continue;
      }
      triangles += geometry->triangles.size();
      items.push_back({geometry, placement, object->getZ(frame),
                       column->getOpacity() / 255.0});
    } catch (const std::exception &error) {
      reportDiagnostic(QString::fromUtf8(error.what()));
    }
  }
  if (items.empty()) return;

  ModelDrawScope scope;
  for (const auto &item : items) {
    glPushMatrix();
    if (!is3DView()) tglMultMatrix(getViewMatrix());
    if (is3DView()) glTranslated(0, 0, item.z);
    tglMultMatrix(item.placement);
    const double units = item.geometry->stageUnitsPerModelUnit;
    glScaled(units, units, units);
    glPolygonMode(GL_FRONT_AND_BACK,
                  item.geometry->wireframe ? GL_LINE : GL_FILL);
    glBegin(GL_TRIANGLES);
    for (const auto &triangle : item.geometry->triangles) {
      glColor4f(triangle.color[0], triangle.color[1], triangle.color[2],
                float(item.opacity));
      for (const auto &p : triangle.vertices) glVertex3dv(p.data());
    }
    glEnd();
    glPopMatrix();
  }
}

namespace {
class LiveRenderCommand final : public MenuItemHandler {
public:
  LiveRenderCommand() : MenuItemHandler(MI_LiveRender) {}
  void execute() override {
    auto *viewer = dynamic_cast<HybridSceneViewer *>(
        TApp::instance()->getCurrentTool()->getTool()
            ? TApp::instance()->getCurrentTool()->getTool()->getViewer()
            : nullptr);
    if (viewer) viewer->setLiveRenderEnabled(!viewer->isLiveRenderEnabled());
  }
} liveRenderCommand;
}  // namespace

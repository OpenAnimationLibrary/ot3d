#include "toonz/amlevelrender.h"
#include "toonz/txshleveltypes.h"
#include "hybridsceneviewer.h"

#include "tapp.h"
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

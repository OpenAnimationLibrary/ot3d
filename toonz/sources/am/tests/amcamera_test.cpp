// Spatial camera acceptance through native levels, FX, keys and persistence.
#include "../../stdfx/transform3dfx.cpp"
#include "../../stdfx/threepointlightfx.cpp"
#include "amfixture.h"
#include "amorientation.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/levelproperties.h"
#include "toonz/txshleveltypes.h"
#include "trasterimage.h"
#include "liverender.h"
#include "toonz/txshamlevel.h"
#include "toonz/toonzscene.h"
#include "toonz/txsheet.h"
#include "toonz/txshcell.h"
#include "toonz/tstageobject.h"
#include "toonz/tstageobjecttree.h"
#include "toonz/stage.h"
#include "toonz/scenefx.h"
#include "toonz/fxdag.h"
#include "toonz/tcolumnfxset.h"
#include "tparamcontainer.h"
#include "tstream.h"
#include "trenderer.h"
#include "tthread.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QFile>
#include <cmath>
#include <iostream>
#include <memory>

namespace {
void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
void nearlyEqual(double a, double b) {
  if (std::abs(a - b) > 1e-7) {
    std::cerr << "coordinates: " << a << " vs " << b << '\n';
    throw std::runtime_error("camera coordinate mismatch");
  }
}
void set(TStageObject *object, TStageObject::Channel channel, double value,
         int frame = 0) {
  object->getParam(channel)->setValue(frame, value);
}
void set(TFx &fx, const char *name, double value) {
  auto *param = dynamic_cast<TDoubleParam *>(fx.getParams()->getParam(name));
  check(param, "missing double FX parameter");
  param->setValue(0, value);
}
void equal(const otam::Mesh &a, const otam::Mesh &b) {
  check(a.triangles.size() == b.triangles.size(),
        "camera triangle count changed");
  for (std::size_t i = 0; i < a.triangles.size(); ++i)
    for (int v = 0; v < 3; ++v)
      for (int k = 0; k < 3; ++k)
        nearlyEqual(a.triangles[i].vertices[v][k],
                    b.triangles[i].vertices[v][k]);
}
void equal(const otam::Mesh &a, const otglb::RenderScene &b) {
  check(a.triangles.size() == b.triangles.size(),
        "camera FX triangle count changed");
  for (std::size_t i = 0; i < a.triangles.size(); ++i)
    for (int v = 0; v < 3; ++v) {
      nearlyEqual(a.triangles[i].vertices[v][0], b.triangles[i].vertices[v].x);
      nearlyEqual(a.triangles[i].vertices[v][1], b.triangles[i].vertices[v].y);
      nearlyEqual(a.triangles[i].vertices[v][2],
                  b.triangles[i].vertices[v].depth);
    }
}
class RenderSession {
  TRenderer m_renderer;
  unsigned long m_id;

public:
  RenderSession() : m_renderer(1), m_id(TRenderer::buildRenderId()) {
    m_renderer.enablePrecomputing(false);
    m_renderer.install(m_id);
    m_renderer.declareRenderStart(m_id);
    m_renderer.declareFrameStart(0);
  }
  ~RenderSession() {
    m_renderer.declareFrameEnd(0);
    m_renderer.declareRenderEnd(m_id);
    m_renderer.uninstall();
  }
};
void matrixEqual(const otam::Matrix &a, const otam::Matrix &b) {
  for (int i = 0; i < 16; ++i) nearlyEqual(a[i], b[i]);
}
void orientationConstraints(const QString &directory) {
  ToonzScene scene;
  auto *sheet   = scene.getXsheet();
  auto *tree    = sheet->getStageObjectTree();
  auto *model   = sheet->getStageObject(TStageObjectId::ColumnId(0));
  auto *drawing = sheet->getStageObject(TStageObjectId::ColumnId(1));
  model->enableAMTransform(true);
  drawing->setParent(model->getId());
  set(model, TStageObject::T_RotationX, 25);
  set(model, TStageObject::T_RotationY, 60);
  set(model, TStageObject::T_Angle, 13);
  set(drawing, TStageObject::T_X, .2);
  using Link = TStageObject::Constraint;
  Link orient;
  orient.type   = Link::OrientLike;
  orient.target = model->getId();
  check(drawing->addConstraint(orient), "attach Orient Like to parent");
  check(drawing->hasSpatialOrientation(),
        "drawing did not opt into spatial rendering");
  matrixEqual(*otam::orientation(drawing->getAMPlacement(0)),
              *otam::orientation(model->getAMPlacement(0)));
  const auto before = drawing->getAMPlacement(0);
  set(model, TStageObject::T_RotationY, 75);
  const auto after = drawing->getAMPlacement(0);
  check(std::abs(before[0] - after[0]) > .01,
        "same-frame target edit stayed cached");
  check(std::abs(drawing->getPlacement(0).a11 - after[0]) < 1e-7,
        "editing view lost spatial orientation");
  nearlyEqual(drawing->getParam(TStageObject::T_RotationY)->getValue(0), 0);
  // Parent rotation is already present: Orient Like must not apply it twice.
  matrixEqual(*otam::orientation(after),
              *otam::orientation(model->getAMPlacement(0)));
  auto *peer = sheet->getStageObject(TStageObjectId::ColumnId(2));
  set(peer, TStageObject::T_Angle, 37);
  Link keep = orient;
  check(peer->captureConstraintOffset(keep, 0) && peer->addConstraint(keep),
        "capture orientation offset");
  matrixEqual(*otam::orientation(peer->getAMPlacement(0)),
              otam::rotation(0, 0, 37));
  // A different parent must not add its rotation to the matched world frame.
  auto *peg = sheet->getStageObject(TStageObjectId::PegbarId(0));
  set(peg, TStageObject::T_Angle, -22);
  set(peg, TStageObject::T_ScaleX, 2);
  peer->clearConstraints();
  peer->setParent(peg->getId());
  check(peer->addConstraint(orient), "separate-parent orientation");
  matrixEqual(*otam::orientation(peer->getAMPlacement(0)),
              *otam::orientation(model->getAMPlacement(0)));
  Link cycle   = orient;
  cycle.target = drawing->getId();
  check(!model->addConstraint(cycle), "parent/constraint cycle accepted");
  cycle.target = peer->getId();
  check(!model->addConstraint(cycle), "constraint-link cycle accepted");
  // Snapshot restoration is the path used by constraint Undo/Redo.
  const auto snapshot = drawing->getConstraints();
  drawing->clearConstraints();
  check(!drawing->hasSpatialOrientation(),
        "clear did not disable drawing projection");
  drawing->setConstraints(snapshot);
  matrixEqual(drawing->getAMPlacement(0), after);
  std::unique_ptr<TStageObjectParams> params(drawing->getParams());
  auto *copy = sheet->getStageObject(TStageObjectId::ColumnId(3));
  copy->assignParams(params.get());
  check(copy->getConstraints().size() == 1,
        "constraint snapshot copy lost links");
  // Planar Aim At remains live and handles coincident positions safely.
  auto *aim    = sheet->getStageObject(TStageObjectId::PegbarId(1));
  auto *target = sheet->getStageObject(TStageObjectId::PegbarId(2));
  set(target, TStageObject::T_Y, 2);
  Link at;
  at.target = target->getId();
  check(aim->addConstraint(at), "planar Aim At assignment");
  nearlyEqual(aim->getPlacement(0).a21, 1);
  set(target, TStageObject::T_Y, 0);
  set(target, TStageObject::T_X, 2);
  nearlyEqual(aim->getPlacement(0).a11, 1);
  set(target, TStageObject::T_X, 0);
  nearlyEqual(aim->getPlacement(0).a11, 1);
  // Save/reopen retains both the link and target identity.
  const TFilePath saved(directory + "/orientation.xml");
  {
    TOStream out(saved);
    out << sheet;
  }
  TPersist *data = nullptr;
  {
    TIStream in(saved);
    in >> data;
  }
  std::unique_ptr<TPersist> owner(data);
  auto *restored = dynamic_cast<TXsheet *>(data);
  check(restored, "constraint scene reopen");
  auto *restoredDrawing = restored->getStageObject(drawing->getId());
  check(restoredDrawing->getConstraints().size() == 1,
        "saved constraint missing");
  matrixEqual(restoredDrawing->getAMPlacement(0), after);
  // A target keeps its identity across insert/swap/delete and delete Undo.
  std::unique_ptr<TStageObjectParams> modelParams(model->getParams());
  tree->insertColumn(0);
  check(drawing->getConstraints()[0].target == TStageObjectId::ColumnId(1),
        "insert retargeted drawing");
  tree->swapColumns(0, 1);
  check(drawing->getConstraints()[0].target == TStageObjectId::ColumnId(0),
        "swap retargeted drawing");
  tree->removeColumn(0);
  for (double v : drawing->getAMPlacement(0))
    check(std::isfinite(v), "missing target is not safe");
  tree->insertColumn(0);
  auto *replacement = tree->getStageObject(TStageObjectId::ColumnId(0));
  set(replacement, TStageObject::T_Angle, 123);
  check(!drawing->hasSpatialOrientation(),
        "missing target rebound to an unrelated column");
  replacement->assignParams(modelParams.get());
  check(drawing->hasSpatialOrientation(),
        "restored target identity did not reconnect");

  // Evaluate a real drawing column through the expanded production FX tree.
  ToonzScene artworkScene;
  artworkScene.setScenePath(TFilePath(directory + "/artwork.tnz"));
  auto *artSheet         = artworkScene.getXsheet();
  TXshSimpleLevelP level = new TXshSimpleLevel(L"Attached artwork");
  level->setScene(&artworkScene);
  level->setType(OVL_XSHLEVEL);
  level->setPath(TFilePath(directory + "/artwork..png"));
  level->getProperties()->setDpi(Stage::inch);
  for (int f = 1; f <= 2; ++f) {
    TRaster32P raster(80, 40);
    raster->fill(f == 1 ? TPixel32::Red : TPixel32::Blue);
    TRasterImageP image = new TRasterImage(raster);
    image->setDpi(Stage::inch, Stage::inch);
    level->setFrame(TFrameId(f), image);
    check(
        artSheet->setCell(f - 1, 1, TXshCell(level.getPointer(), TFrameId(f))),
        "expose drawing");
  }
  auto *artModel = artSheet->getStageObject(TStageObjectId::ColumnId(0));
  artModel->enableAMTransform(true);
  set(artModel, TStageObject::T_RotationY, 60);
  auto *artObject = artSheet->getStageObject(TStageObjectId::ColumnId(1));
  artObject->setParent(artModel->getId());
  orient.target = artModel->getId();
  orient.targetIdentity.clear();
  check(artObject->addConstraint(orient), "drawing attachment assignment");
  auto *column = artSheet->getColumn(1)->getFx();
  TRenderSettings settings;
  TRasterFxP first =
      buildSceneFx(&artworkScene, 0, artSheet, column, BSFX_NO_TR);
  auto *spatial = dynamic_cast<T3DRenderSource *>(first.getPointer());
  check(spatial, "expanded drawing lost spatial source");
  auto bounds = spatial->get3DRenderGeometry(0, nullptr, nullptr, &settings);
  check(bounds && !bounds->empty(), "spatial drawing geometry missing");
  check(bounds->bounds[2] - bounds->bounds[0] < 60,
        "depth rotation did not foreshorten artwork");
  RenderSession session;
  auto red = spatial->get3DRenderScene(0, nullptr, nullptr, nullptr, &settings);
  check(
      red && red->texturePerspective && red->texture && !red->texture->empty(),
      "drawing texture missing");
  check((*red->texture)[red->texture->size() / 2].r > 0,
        "first exposure lost red artwork");
  TRasterFxP second =
      buildSceneFx(&artworkScene, 1, artSheet, column, BSFX_NO_TR);
  auto *blueSpatial = dynamic_cast<T3DRenderSource *>(second.getPointer());
  check(blueSpatial, "animated drawing lost spatial source");
  auto blue =
      blueSpatial->get3DRenderScene(1, nullptr, nullptr, nullptr, &settings);
  check(blue && blue->texture &&
            (*blue->texture)[blue->texture->size() / 2].b > 0,
        "animated exposure did not update artwork");
  std::unique_ptr<TFx> cloned(first->clone(true));
  auto *clonedSpatial = dynamic_cast<T3DRenderSource *>(cloned.get());
  check(clonedSpatial && clonedSpatial->get3DRenderGeometry(0, nullptr),
        "spatial drawing clone lost placement");
}
}  // namespace
int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  TThread::init();
  TRenderer::initialize();
  try {
    QTemporaryDir temp;
    check(temp.isValid(), "temporary directory");
    orientationConstraints(temp.path());
    const auto path = temp.filePath("model.mdl");
    {
      QFile f(path);
      check(f.open(QIODevice::WriteOnly), "open fixture");
      check(f.write(amtest::source.data(), amtest::source.size()) ==
                qint64(amtest::source.size()),
            "write fixture");
    }
    ToonzScene scene;
    scene.setScenePath(TFilePath(temp.filePath("scene.tnz")));
    auto *level = scene.loadLevel(TFilePath(path));
    auto *sheet = scene.getXsheet();
    check(level && level->getAMLevel(), "native load");
    check(sheet->setCell(0, 0, TXshCell(level, TFrameId(1))), "expose model");
    const auto original = level->getAMLevel()->mesh();
    auto *tree          = sheet->getStageObjectTree();
    const auto cameraId = tree->getCurrentCameraId();
    auto *camera        = sheet->getStageObject(cameraId);
    check(camera->is3DTransformEnabled() && !camera->isAMTransformEnabled(),
          "camera is not a distinct spatial object");
    check(!sheet->getStageObject(TStageObjectId::ColumnId(2))
               ->is3DTransformEnabled(),
          "ordinary 2D object acquired spatial capability");

    // Legacy cameras preserve framing, including pan, roll, scale, shear and
    // a transformed parent; global scale must not implicitly scale optical Z.
    auto *parent = sheet->getStageObject(TStageObjectId::PegbarId(0));
    set(parent, TStageObject::T_X, 0.25);
    set(parent, TStageObject::T_Angle, -9);
    set(parent, TStageObject::T_Z, 25);
    camera->setParent(parent->getId());
    set(camera, TStageObject::T_X, 0.5);
    set(camera, TStageObject::T_Y, -0.4);
    set(camera, TStageObject::T_Z, 100);
    set(camera, TStageObject::T_Angle, 17);
    set(camera, TStageObject::T_Scale, 1.3);
    set(camera, TStageObject::T_ScaleX, 0.9);
    set(camera, TStageObject::T_ScaleY, 1.2);
    set(camera, TStageObject::T_ShearX, 0.2);
    const auto placed = prepareAMLevelGeometry(sheet, 0, 0, false);
    const auto affine = camera->getPlacement(0);
    const auto legacy =
        otam::projected(*placed, affine.a13, affine.a23, camera->getZ(0));
    equal(*prepareAMLevelGeometry(sheet, 0, 0, true), legacy);
    camera->setParent(TStageObjectId::NoneId);
    for (auto c : {TStageObject::T_X, TStageObject::T_Y, TStageObject::T_Z,
                   TStageObject::T_Angle, TStageObject::T_ShearX})
      set(camera, c, 0);
    for (auto c : {TStageObject::T_Scale, TStageObject::T_ScaleX,
                   TStageObject::T_ScaleY})
      set(camera, c, 1);
    const auto baseline = prepareAMLevelGeometry(sheet, 0, 0, true);

    // A model drop keeps the cursor position on empty stage space. Snapping
    // uses the closest visible surface and leaves room for the new model.
    constexpr double halfExtent = 50;
    auto stageDrop =
        nativeModelDropPosition(sheet, 0, {24, -36}, false, halfExtent);
    nearlyEqual(stageDrop[0], 24);
    nearlyEqual(stageDrop[1], -36);
    nearlyEqual(stageDrop[2], 0);
    check(sheet->setCell(0, 1, TXshCell(level, TFrameId(1))),
          "expose foreground snap target");
    auto *foreground = sheet->getStageObject(TStageObjectId::ColumnId(1));
    set(foreground, TStageObject::T_Z, 100);
    auto snapped = nativeModelDropPosition(sheet, 0, {2, -3}, true, halfExtent);
    check(std::abs(snapped[2] - 150) < 1e-3, "closest model snap");
    sheet->getColumn(1)->setCamstandVisible(false);
    snapped = nativeModelDropPosition(sheet, 0, {2, -3}, true, halfExtent);
    check(std::abs(snapped[2] - 50) < 1e-3, "hidden model snap exclusion");
    sheet->removeColumn(1);

    // Legacy camera roll/zoom must not silently change existing native shading.
    auto *modelObject = sheet->getStageObject(TStageObjectId::ColumnId(0));
    set(modelObject, TStageObject::T_RotationX, 25);
    set(modelObject, TStageObject::T_RotationY, -12);
    otglb::LightingRig rig;
    rig.ambient = 0.2;
    otglb::DiffuseLight keyLight;
    keyLight.direction = {{1, 2, 3}};
    rig.lights.push_back(keyLight);
    auto shaded = [&]() {
      TRasterFxP source = makeAMLevelRenderFx(sheet, 0, 0);
      return dynamic_cast<T3DRenderSource *>(source.getPointer())
          ->get3DRenderScene(0, nullptr, &rig);
    };
    auto oldShading = shaded();
    set(camera, TStageObject::T_Angle, 23);
    set(camera, TStageObject::T_ScaleX, 1.5);
    auto rolledShading = shaded();
    check(oldShading->triangles.size() == rolledShading->triangles.size(),
          "legacy roll changed shading topology");
    for (std::size_t i = 0; i < oldShading->triangles.size(); ++i)
      for (int k = 0; k < 3; ++k)
        nearlyEqual(oldShading->triangles[i].color[k],
                    rolledShading->triangles[i].color[k]);
    set(camera, TStageObject::T_Angle, 0);
    set(camera, TStageObject::T_ScaleX, 1);
    // A camera rigged to a spatial model follows the parent's optical eye.
    // With the model and camera moving together their relative view is stable.
    camera->setParent(modelObject->getId());
    equal(*baseline, *prepareAMLevelGeometry(sheet, 0, 0, true));
    camera->setParent(TStageObjectId::NoneId);
    set(modelObject, TStageObject::T_RotationX, 0);
    set(modelObject, TStageObject::T_RotationY, 0);

    // Scene camera and FX camera must agree, while raw model geometry and the
    // camera's ordinary planar transform remain independent of its pitch/yaw.
    TFxP owner = new Transform3DFx;
    auto &fx   = *static_cast<Transform3DFx *>(owner.getPointer());
    fx.getInputPort(0)->setFx(sheet->getColumn(0)->getFx());
    auto *target =
        dynamic_cast<TIntEnumParam *>(fx.getParams()->getParam("target"));
    check(target && target->getValue() == 0, "legacy FX target default");
    target->setValue(1);
    const auto originalAffine = camera->getPlacement(0);
    set(camera, TStageObject::T_RotationX, 1);
    set(camera, TStageObject::T_RotationY, 2);
    auto rotated = prepareAMLevelGeometry(sheet, 0, 0, true);
    check(std::abs(rotated->triangles[0].vertices[0][0] -
                   baseline->triangles[0].vertices[0][0]) > 1,
          "camera yaw did not change the view");
    check(camera->getPlacement(0) == originalAffine,
          "camera tilt changed the legacy 2D affine");
    equal(*placed, *prepareAMLevelGeometry(sheet, 0, 0, false));
    TRasterFxP snapshot = makeAMLevelRenderFx(sheet, 0, 0);
    const auto oldAlias = snapshot->getAlias(0, TRenderSettings());
    set(camera, TStageObject::T_RotationX, 0);
    set(camera, TStageObject::T_RotationY, 0);
    set(fx, "rotationX", 1);
    set(fx, "rotationY", 2);
    equal(*rotated, *fx.get3DRenderScene(0, nullptr));
    nearlyEqual(camera->getParam(TStageObject::T_RotationY)->getValue(0), 0);
    auto *snapshotSource =
        dynamic_cast<T3DRenderSource *>(snapshot.getPointer());
    check(snapshotSource, "snapshot source interface");
    equal(*rotated, *snapshotSource->get3DRenderScene(0, nullptr));
    TRasterFxP current = makeAMLevelRenderFx(sheet, 0, 0);
    check(current->getAlias(0, TRenderSettings()) != oldAlias,
          "camera pose missing from cache key");

    // Transform translation is camera-local; geometry/camera deltas are
    // independent even when the node order interleaves their targets.
    set(fx, "rotationX", 0);
    set(fx, "rotationY", 0);
    set(fx, "positionX", 10);
    const auto shifted = fx.get3DRenderScene(0, nullptr);
    nearlyEqual(shifted->triangles[0].vertices[0].x,
                baseline->triangles[0].vertices[0][0] - 10);
    TFxP geometryOwner = new Transform3DFx;
    auto &geometry = *static_cast<Transform3DFx *>(geometryOwner.getPointer());
    geometry.getInputPort(0)->setFx(&fx);
    set(geometry, "positionX", 10);
    equal(*baseline, *geometry.get3DRenderScene(0, nullptr));
    TFxP clone    = fx.clone(true);
    auto *cloneFx = dynamic_cast<Transform3DFx *>(clone.getPointer());
    check(cloneFx && dynamic_cast<TIntEnumParam *>(
                         cloneFx->getParams()->getParam("target"))
                             ->getValue() == 1,
          "camera target lost on clone");
    set(fx, "positionX", 0);

    // Separate preview/render cameras choose independent, keyed views.
    const auto previewId = TStageObjectId::CameraId(cameraId.getIndex() + 1);
    auto *preview        = sheet->getStageObject(previewId);
    tree->setCurrentPreviewCameraId(previewId);
    set(preview, TStageObject::T_RotationY, 2);
    auto previewMesh = prepareAMLevelGeometry(sheet, 0, 0, true, true);
    check(std::abs(previewMesh->triangles[0].vertices[0][0] -
                   baseline->triangles[0].vertices[0][0]) > 1,
          "preview camera not selected");
    TRasterFxP previewFx = makeAMLevelRenderFx(sheet, 0, 0, true);
    equal(*previewMesh, *dynamic_cast<T3DRenderSource *>(previewFx.getPointer())
                             ->get3DRenderScene(0, nullptr));
    equal(*baseline, *prepareAMLevelGeometry(sheet, 0, 0, true, false));
    tree->setCurrentPreviewCameraId(cameraId);

    set(camera, TStageObject::T_RotationX, 1, 0);
    set(camera, TStageObject::T_RotationX, 3, 10);
    camera->setKeyframeWithoutUndo(0);
    check(camera->isFullKeyframe(0), "global camera key incomplete");
    check(camera->getParam(TStageObject::T_CenterZ)->isKeyframe(0),
          "camera pivot omitted from global key");
    check(camera->getParam(TStageObject::T_ScaleZ)->isKeyframe(0),
          "camera scale Z omitted from global key");
    const auto key = camera->getKeyframe(0);
    camera->removeKeyframeWithoutUndo(0);
    check(!camera->getParam(TStageObject::T_RotationX)->isKeyframe(0),
          "camera key deletion omitted Rx");
    camera->setKeyframeWithoutUndo(0, key);
    nearlyEqual(camera->getParam(TStageObject::T_RotationX)->getValue(0), 1);

    // Keep a real camera-target FX in the persisted graph.
    sheet->getFxDag()->getInternalFxs()->addFx(&fx);
    const TFilePath saved(temp.filePath("sheet.xml"));
    {
      TOStream out(saved);
      out << sheet;
    }
    TPersist *data = nullptr;
    {
      TIStream in(saved);
      in >> data;
    }
    std::unique_ptr<TPersist> restoredOwner(data);
    auto *restored = dynamic_cast<TXsheet *>(data);
    check(restored, "xsheet persistence");
    auto *restoredCamera = restored->getStageObject(cameraId);
    check(restoredCamera->is3DTransformEnabled(),
          "camera capability lost on reopen");
    nearlyEqual(
        restoredCamera->getParam(TStageObject::T_RotationX)->getValue(0), 1);
    nearlyEqual(
        restoredCamera->getParam(TStageObject::T_RotationX)->getValue(10), 3);
    check(restoredCamera->isFullKeyframe(0), "camera keys lost on reopen");
    bool restoredCameraTarget = false;
    auto *restoredFxs         = restored->getFxDag()->getInternalFxs();
    for (int i = 0; i < restoredFxs->getFxCount(); ++i) {
      auto *restoredFx = dynamic_cast<Transform3DFx *>(restoredFxs->getFx(i));
      if (!restoredFx) continue;
      auto *savedTarget = dynamic_cast<TIntEnumParam *>(
          restoredFx->getParams()->getParam("target"));
      check(savedTarget && savedTarget->getValue() == 1,
            "camera FX target lost on reopen");
      check(restoredFx->getInputPort(0)->getFx(),
            "camera FX source connection lost on reopen");
      restoredCameraTarget = true;
    }
    check(restoredCameraTarget, "camera FX missing from reopened scene");

    // Public compute on the built render tree, not only a geometry call.
    set(camera, TStageObject::T_RotationX, 0);
    set(fx, "rotationY", 0.5);
    TFxP lightOwner = new ThreePointLightFx;
    auto &light = *static_cast<ThreePointLightFx *>(lightOwner.getPointer());
    light.getInputPort(0)->setFx(&fx);
    sheet->getFxDag()->getInternalFxs()->addFx(&light);
    TFxP renderTree =
        buildSceneFx(&scene, 0, sheet, lightOwner, BSFX_CAMERA_TR);
    auto *rasterFx = dynamic_cast<TRasterFx *>(renderTree.getPointer());
    check(rasterFx, "camera render tree");
    TRaster32P raster(128, 128);
    TTile tile(raster, TPointD(-64, -64));
    TRenderSettings settings;
    {
      RenderSession session;
      rasterFx->compute(tile, 0, settings);
    }
    bool visible = false;
    for (int y = 0; y < 128; ++y)
      for (int x = 0; x < 128; ++x) visible |= raster->pixels(y)[x].m != 0;
    check(visible, "camera/lighting render tree produced no pixels");
    check(level->getAMLevel()->mesh() == original, "camera changed model data");
    int canceled = 1;
    check(!fx.get3DRenderScene(0, &canceled), "camera cancellation ignored");
    set(fx, "rotationY", 180);
    check(fx.get3DRenderScene(0, nullptr)->triangles.empty(),
          "camera near clipping failed");
    std::cout << "Spatial camera channels, legacy framing, FX, keys, preview "
                 "camera, persistence and render passed\n";
    return 0;
  } catch (const TException &e) {
    std::wcerr << e.getMessage() << '\n';
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
  }
  return 1;
}

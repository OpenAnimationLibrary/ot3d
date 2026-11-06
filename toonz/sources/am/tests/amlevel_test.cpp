// Native integration regression; synthetic fixtures, no A:M host or graphics
// context.
#include "amfixture.h"
#include "../../glb/tests/glbfixture.h"
#include "toonz/txshglblevel.h"
#include "toonz/txshamlevel.h"
#include "toonz/amlevelrender.h"
#include "toonz/toonzscene.h"
#include "toonz/levelset.h"
#include "toonz/sceneresources.h"
#include "toonz/txsheet.h"
#include "toonz/txshcell.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/txshleveltypes.h"
#include "toonz/tstageobject.h"
#include "toonz/stage.h"
#include "tfiletype.h"
#include "toonzqt/gutil.h"
#include "tstream.h"
#include "ttile.h"
#include "traster.h"
#include "trenderer.h"
#include "tthread.h"
#include <QGuiApplication>
#include <QUrl>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
namespace {
void check(bool result, const char *message) {
  if (!result) throw std::runtime_error(message);
}
void nearlyEqual(double a, double b) {
  check(std::abs(a - b) < 1e-7, "coordinate/parameter mismatch");
}
QByteArray readFile(const QString &path) {
  QFile f(path);
  check(f.open(QIODevice::ReadOnly), "read fixture");
  return f.readAll();
}
// TRasterFx::compute requires a renderer installed on the calling thread,
// including its resource-cache and frame lifetime. Match the swatch renderer.
class NativeRenderSession {
  TRenderer m_renderer;
  unsigned long m_id;
  double m_frame;

public:
  explicit NativeRenderSession(double frame = 0)
      : m_renderer(1), m_id(TRenderer::buildRenderId()), m_frame(frame) {
    m_renderer.enablePrecomputing(false);
    m_renderer.install(m_id);
    m_renderer.declareRenderStart(m_id);
    m_renderer.declareFrameStart(m_frame);
  }
  ~NativeRenderSession() {
    m_renderer.declareFrameEnd(m_frame);
    m_renderer.declareRenderEnd(m_id);
    m_renderer.uninstall();
  }
  NativeRenderSession(const NativeRenderSession &)            = delete;
  NativeRenderSession &operator=(const NativeRenderSession &) = delete;
};
}  // namespace
int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  TThread::init();
  TRenderer::initialize();
  try {
    QTemporaryDir temp;
    check(temp.isValid(), "temporary directory");
    const QString source =
        temp.path() + QString::fromUtf8("/model-\xc3\xa9.001.MDL");
    {
      QFile f(source);
      check(f.open(QIODevice::WriteOnly), "write fixture");
      check(f.write(amtest::source.data(), qint64(amtest::source.size())) ==
                qint64(amtest::source.size()),
            "write complete fixture");
    }
    check(TFileType::getInfo(TFilePath(source)) == TFileType::AM_MODEL_LEVEL,
          "MDL is not a native model resource");
    check(!TFileType::isViewable(TFileType::getInfo(TFilePath(source))),
          "model incorrectly routed to an image reader");
    check(TFilePath(source).getLevelNameW() ==
              TFilePath(source).withoutParentDir().getWideString(),
          "numbered model filename collapsed into an image sequence");
    const QList<QUrl> modelDrop{QUrl::fromLocalFile(source)};
    check(isResource(source) && acceptResourceDrop(modelDrop) &&
              acceptResourceOrFolderDrop(modelDrop),
          "native model rejected by drag-and-drop resource checks");
    const QList<QUrl> mixedDrop{
        QUrl::fromLocalFile(source),
        QUrl::fromLocalFile(temp.path() + "/scene.tnz")};
    check(acceptResourceDrop(mixedDrop), "mixed resource drop rejected");
    check(!acceptResourceDrop({QUrl::fromLocalFile(temp.path() + "/bad.xyz")}),
          "unsupported file accepted as a model resource");
    ToonzScene scene;
    scene.setScenePath(TFilePath(temp.path() + "/scene.tnz"));
    TXshLevel *loaded = scene.loadLevel(TFilePath(source));
    check(loaded, "scene loader did not recognize the model");
    auto *level = loaded->getAMLevel();
    check(level && !level->getSimpleLevel(),
          "model masquerades as an image level");
    check(level->getType() == AM_XSHLEVEL && level->getFrameCount() == 1,
          "model level type/exposure");
    check(level->document() && level->document()->source == amtest::source,
          "source not retained");
    check(scene.decodeFilePath(level->getPath()) == TFilePath(source),
          "scene loader changed the physical model path");
    auto *palette = level->getPalette();
    check(palette->getStyleCount() == 3 &&
              palette->getStyle(2)->getName() == L"Red" &&
              palette->getStyle(2)->getMainColor() == TPixel32::Red,
          "A:M group did not initialize a named surface style");
    const auto originalMesh = level->mesh();
    palette->getStyle(2)->setMainColor(TPixel32::Blue);
    check(
        level->updatePaletteColors() &&
            level->renderMesh()->triangles[0].color == otam::Color{{0, 0, 1}} &&
            level->mesh() == originalMesh &&
            originalMesh->triangles[0].color == otam::Color{{1, 0, 0}},
        "palette edit did not produce an independent colored snapshot");
    const int castCount   = scene.getLevelSet()->getLevelCount();
    const QString invalid = temp.path() + "/invalid.mdl";
    {
      QFile f(invalid);
      check(f.open(QIODevice::WriteOnly), "write invalid fixture");
      f.write("not an Animation:Master model");
    }
    for (const auto &path : {invalid, temp.path() + "/missing.mdl"}) {
      bool failed = false;
      try {
        scene.loadLevel(TFilePath(path));
      } catch (const TException &) {
        failed = true;
      }
      check(failed, "invalid or missing model load did not report an error");
      check(scene.getLevelSet()->getLevelCount() == castCount,
            "failed model load left a phantom Scene Cast resource");
    }
    auto sourceMesh = level->mesh();
    auto *xsheet    = scene.getXsheet();
    check(xsheet->setCell(0, 0, TXshCell(level, TFrameId(1))), "expose model");
    check(xsheet->setCell(0, 1, TXshCell(level, TFrameId(1))),
          "expose second model instance");
    auto *first  = xsheet->getStageObject(TStageObjectId::ColumnId(0));
    auto *second = xsheet->getStageObject(TStageObjectId::ColumnId(1));
    check(first->isAMTransformEnabled() && second->isAMTransformEnabled(),
          "model capability not enabled");
    first->getParam(TStageObject::T_RotationX)->setValue(0, 90);
    first->getParam(TStageObject::T_RotationX)->setValue(10, 180);
    first->getParam(TStageObject::T_ScaleZ)->setValue(0, 2);
    first->setKeyframeWithoutUndo(0);
    check(first->isFullKeyframe(0), "AM global key omitted a channel");
    check(first->getParam(TStageObject::T_CenterZ)->isKeyframe(0),
          "pivot missing from global key");
    nearlyEqual(second->getParam(TStageObject::T_RotationX)->getValue(0), 0);
    const auto a = prepareAMLevelGeometry(xsheet, 0, 0, false);
    const auto b = prepareAMLevelGeometry(xsheet, 1, 0, false);
    check(a && b && a->triangles.size() == b->triangles.size(),
          "native viewer geometry missing");
    const double units  = Stage::inch / 2.54;
    const auto original = sourceMesh->triangles[0].vertices[0];
    const auto rotated  = a->triangles[0].vertices[0];
    nearlyEqual(rotated[0], original[0] * units);
    nearlyEqual(rotated[1], 0);
    nearlyEqual(rotated[2], original[1] * units);
    check(level->mesh() == sourceMesh,
          "instance animation modified shared geometry");
    const auto savedKey = first->getKeyframe(0);
    first->removeKeyframeWithoutUndo(0);
    check(!first->getParam(TStageObject::T_RotationX)->isKeyframe(0),
          "key deletion omitted Rx");
    first->setKeyframeWithoutUndo(0, savedKey);
    nearlyEqual(first->getParam(TStageObject::T_RotationX)->getValue(0), 90);
    // Existing 2D levels retain their planar behavior and global key contents.
    TXshSimpleLevelP drawing = new TXshSimpleLevel;
    drawing->setType(PLI_XSHLEVEL);
    check(xsheet->setCell(0, 2, TXshCell(drawing.getPointer(), TFrameId(1))),
          "expose drawing");
    check(!xsheet->setCell(1, 2, TXshCell(level, TFrameId(1))),
          "mixed 2D/model column accepted");
    auto *legacy = xsheet->getStageObject(TStageObjectId::ColumnId(2));
    check(!legacy->isAMTransformEnabled(),
          "ordinary drawing gained spatial transforms");
    const TPointD old = legacy->getPlacement(0) * TPointD(1, 2);
    legacy->getParam(TStageObject::T_RotationX)->setValue(0, 67);
    const TPointD unchanged = legacy->getPlacement(0) * TPointD(1, 2);
    nearlyEqual(old.x, unchanged.x);
    nearlyEqual(old.y, unchanged.y);
    legacy->getParam(TStageObject::T_RotationX)->deleteKeyframe(0);
    legacy->setKeyframeWithoutUndo(0);
    check(!legacy->getParam(TStageObject::T_RotationX)->isKeyframe(0),
          "ordinary global key added AM channels");
    const auto glbPath = temp.filePath("model.001.GLB");
    {
      QFile file(glbPath);
      const auto bytes = triangleGlb();
      check(file.open(QIODevice::WriteOnly), "write GLB fixture");
      check(file.write(reinterpret_cast<const char *>(bytes.data()),
                       qint64(bytes.size())) == qint64(bytes.size()),
            "write complete GLB fixture");
    }
    check(
        TFileType::getInfo(TFilePath(glbPath)) == TFileType::GLB_MODEL_LEVEL &&
            isResource(glbPath),
        "GLB is not recognized as a native resource");
    auto *loadedGlb = scene.loadLevel(TFilePath(glbPath));
    auto *glb       = loadedGlb ? loadedGlb->getGlbLevel() : nullptr;
    check(glb && glb->asset() && glb->getType() == GLB_XSHLEVEL &&
              !glb->getAMLevel() && !glb->getSimpleLevel() &&
              glb->getFrameCount() == 1,
          "GLB did not load as a distinct native level");
    check(
        xsheet->setCell(0, 3, TXshCell(glb, TFrameId(1))) &&
            !xsheet->setCell(1, 3, TXshCell(level, TFrameId(1))) &&
            !xsheet->setCell(1, 3, TXshCell(drawing.getPointer(), TFrameId(1))),
        "GLB column exposure or type isolation failed");
    auto *glbObject = xsheet->getStageObject(TStageObjectId::ColumnId(3));
    check(glbObject->is3DTransformEnabled(), "GLB spatial channels missing");
    const auto glbBase = prepareNativeModelGeometry(xsheet, 3, 0, false);
    check(glbBase && !glbBase->triangles.empty() &&
              !prepareAMLevelGeometry(xsheet, 3, 0, false),
          "GLB render geometry failed or was classified as A:M");
    glbObject->getParam(TStageObject::T_RotationY)->setValue(0, 35);
    glbObject->setKeyframeWithoutUndo(0);
    const auto glbMoved = prepareNativeModelGeometry(xsheet, 3, 0, false);
    check(glbMoved &&
              glbMoved->triangles[0].vertices != glbBase->triangles[0].vertices,
          "GLB stage rotation did not reach render geometry");
    // Serialization includes new native resource and ordinary stage channel
    // keys.
    const TFilePath serialized(temp.path() + "/sheet.xml");
    {
      TOStream out(serialized);
      out << xsheet;
    }
    TPersist *persist = nullptr;
    {
      TIStream in(serialized);
      in >> persist;
    }
    std::unique_ptr<TPersist> owner(persist);
    auto *restored = dynamic_cast<TXsheet *>(persist);
    check(restored, "xsheet persistence failed");
    auto *restoredLevel = restored->getCell(0, 0).m_level->getAMLevel();
    check(restoredLevel, "native level persistence registration failed");
    check(restored->getCell(0, 1).m_level.getPointer() == restoredLevel,
          "shared level identity lost");
    restoredLevel->setScene(&scene);
    restoredLevel->load();
    check(restoredLevel->getPalette()->getStyle(2)->getName() == L"Red" &&
              restoredLevel->renderMesh()->triangles[0].color ==
                  otam::Color{{0, 0, 1}},
          "A:M palette override was lost on reopen");
    auto renamed = amtest::source;
    renamed.replace(renamed.find("Name=Red"), 8, "Name=Coat");
    {
      QFile file(source);
      check(file.open(QIODevice::WriteOnly), "write renamed group fixture");
      check(file.write(renamed.data(), qint64(renamed.size())) ==
                qint64(renamed.size()),
            "write complete renamed fixture");
    }
    restoredLevel->load();
    check(restoredLevel->getPalette()->getStyle(2)->getName() == L"Coat" &&
              restoredLevel->renderMesh()->triangles[0].color ==
                  otam::Color{{0, 0, 1}},
          "refresh did not update group name or preserve color edit");
    restoredLevel->getPalette()->getStyle(2)->setMainColor(
        restoredLevel->sourceStyleColors().at(2));
    check(restoredLevel->updatePaletteColors() &&
              restoredLevel->renderMesh()->triangles[0].color ==
                  otam::Color{{1, 0, 0}},
          "source color was not restored");
    {
      QFile file(source);
      check(file.open(QIODevice::WriteOnly), "restore original fixture");
      check(file.write(amtest::source.data(), qint64(amtest::source.size())) ==
                qint64(amtest::source.size()),
            "restore complete original fixture");
    }
    auto *restoredObject =
        restored->getStageObject(TStageObjectId::ColumnId(0));
    check(restoredObject->isAMTransformEnabled(),
          "spatial capability lost on reopen");
    nearlyEqual(
        restoredObject->getParam(TStageObject::T_RotationX)->getValue(0), 90);
    nearlyEqual(
        restoredObject->getParam(TStageObject::T_RotationX)->getValue(10), 180);
    check(!restored->getStageObject(TStageObjectId::ColumnId(2))
               ->isAMTransformEnabled(),
          "2D level changed on reopen");
    auto *restoredGlb = restored->getCell(0, 3).m_level->getGlbLevel();
    check(restoredGlb && !restoredGlb->getAMLevel(), "GLB type lost on reopen");
    restoredGlb->setScene(&scene);
    restoredGlb->load();
    check(bool(restoredGlb->asset()), "GLB source did not reload");
    nearlyEqual(restored->getStageObject(TStageObjectId::ColumnId(3))
                    ->getParam(TStageObject::T_RotationY)
                    ->getValue(0),
                35);
    // The same evaluated mesh drives the final raster adapter (opaque static
    // subset).
    auto projected = prepareAMLevelGeometry(xsheet, 1, 0, true);
    TRasterFxP fx(makeAMLevelRenderFx(projected));
    TRaster32P raster(128, 128);
    TTile tile(raster, TPointD(-64, -64));
    TRenderSettings settings;
    {
      NativeRenderSession renderSession;
      fx->compute(tile, 0, settings);
    }
    check(raster->pixels(64)[64] == TPixel32::Blue,
          "model final-render adapter did not use the palette color");
    TRasterFxP glbFx(makeNativeModelRenderFx(xsheet, 3, 0, false));
    raster->clear();
    {
      NativeRenderSession renderSession;
      glbFx->compute(tile, 0, settings);
    }
    check(raster->pixels(64)[64].m > 0, "GLB render snapshot did not draw");
    // Hold drawing 1 while palette style 2 changes on Xsheet rows.
    const TPixel32 orange(255, 154, 31, 255);
    for (const auto &[row, color] : {std::pair<int, TPixel32>{0, TPixel32::Red},
                                     {14, orange},
                                     {35, TPixel32::Red}}) {
      palette->getStyle(2)->setMainColor(color);
      palette->setKeyframe(2, row);
    }
    palette->setFrame(14);
    check(level->updatePaletteColors(),
          "palette keys did not refresh the model");
    for (int row = 1; row <= 35; ++row)
      check(xsheet->setCell(row, 1, TXshCell(level, TFrameId(1))),
            "hold native model exposure");

    const auto orangeGeometry =
        prepareNativeModelGeometry(xsheet, 1, 14, false);
    const auto redGeometry   = prepareNativeModelGeometry(xsheet, 1, 0, false);
    const auto tweenGeometry = prepareNativeModelGeometry(xsheet, 1, 7, false);
    const auto endGeometry   = prepareNativeModelGeometry(xsheet, 1, 35, false);
    palette->setFrame(7);
    const auto tween = palette->getStyle(2)->getMainColor();
    check(orangeGeometry->triangles[0].color ==
                  otam::Color{{1, orange.g / 255.f, orange.b / 255.f}} &&
              redGeometry->triangles[0].color == otam::Color{{1, 0, 0}} &&
              tweenGeometry->triangles[0].color ==
                  otam::Color{{1, tween.g / 255.f, tween.b / 255.f}} &&
              endGeometry->triangles[0].color == otam::Color{{1, 0, 0}},
          "held model did not use the keyed and interpolated palette colors");

    TRasterFxP orangeFx(makeNativeModelRenderFx(xsheet, 1, 14, false));
    TRasterFxP redFx(makeNativeModelRenderFx(xsheet, 1, 0, false));
    check(orangeFx->getAlias(14, settings) != redFx->getAlias(0, settings),
          "keyed palette colors share a render cache identity");
    check(palette->getFrame() == 7 &&
              palette->getStyle(2)->getMainColor() == tween,
          "out-of-order render requests changed the editable palette");
    raster->clear();
    settings.m_flatRender = true;
    {
      NativeRenderSession renderSession(14);
      orangeFx->compute(tile, 14, settings);
    }
    check(raster->pixels(64)[64] == orange,
          "final render lost the keyed A:M palette color");

    const TFilePath animatedPath(temp.path() + "/animated-palette.tnz");
    {
      TOStream out(animatedPath);
      out << level;
    }
    TPersist *animated = nullptr;
    {
      TIStream in(animatedPath);
      in >> animated;
    }
    std::unique_ptr<TPersist> animatedOwner(animated);
    auto *animatedLevel = dynamic_cast<TXshAMLevel *>(animated);
    check(animatedLevel, "animated A:M level did not reopen");
    animatedLevel->setScene(&scene);
    animatedLevel->load();
    check(animatedLevel->getPalette()->getKeyframeCount(2) == 3 &&
              animatedLevel->renderMesh(14)->triangles[0].color ==
                  orangeGeometry->triangles[0].color,
          "A:M palette keys were lost on reopen");

    level->save();
    check(readFile(source).toStdString() == amtest::source,
          "source rewritten by save");
    // Preserve physical references and loaded snapshots during Save As.
    level->setPath(
        TFilePath("$scenefolder") + TFilePath(source).withoutParentDir(), true);
    const auto originalPath = scene.decodeFilePath(level->getPath());
    {
      SceneResources resources(&scene, nullptr);
      QDir().mkpath(temp.path() + "/other");
      scene.setScenePath(TFilePath(temp.path() + "/other/new.tnz"));
      resources.updatePaths();
      resources.commit();
    }
    check(scene.decodeFilePath(level->getPath()) == originalPath,
          "Save As changed physical source");
    check(level->mesh() == sourceMesh, "path recoding discarded cached mesh");
    // Failed reload must not silently retain the last valid displayed model.
    level->setPath(TFilePath(temp.path() + "/absent.mdl"));
    try {
      level->load();
    } catch (...) {
    }
    check(!level->mesh() && !level->error().empty(),
          "missing model appears successfully loaded");
    std::cout << "A:M native level, exposure, per-instance transforms, keys, "
                 "persistence, 2D isolation, raster and Save As passed\n";
    return 0;
  } catch (const TException &e) {
    std::wcerr << e.getMessage() << '\n';
    return 1;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

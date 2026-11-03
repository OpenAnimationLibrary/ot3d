// Native integration regression; synthetic fixtures, no A:M host or graphics
// context.
#include "amfixture.h"
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
#include "tstream.h"
#include "ttile.h"
#include "traster.h"
#include <QCoreApplication>
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
void near(double a, double b) {
  check(std::abs(a - b) < 1e-7, "coordinate/parameter mismatch");
}
QByteArray readFile(const QString &path) {
  QFile f(path);
  check(f.open(QIODevice::ReadOnly), "read fixture");
  return f.readAll();
}
}  // namespace
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    QTemporaryDir temp;
    check(temp.isValid(), "temporary directory");
    const QString source =
        temp.path() + QString::fromUtf8("/model-\xc3\xa9.mdl");
    {
      QFile f(source);
      check(f.open(QIODevice::WriteOnly), "write fixture");
      check(f.write(amtest::source.data(), qint64(amtest::source.size())) ==
                qint64(amtest::source.size()),
            "write complete fixture");
    }
    check(TFileType::getInfo(TFilePath(source)) == TFileType::AM_MODEL_LEVEL,
          "MDL is not a native model resource");
    ToonzScene scene;
    scene.setScenePath(TFilePath(temp.path() + "/scene.tnz"));
    auto *level = scene.loadLevel(TFilePath(source))->getAMLevel();
    check(level && !level->getSimpleLevel(),
          "model masquerades as an image level");
    check(level->getType() == AM_XSHLEVEL && level->getFrameCount() == 1,
          "model level type/exposure");
    check(level->document() && level->document()->source == amtest::source,
          "source not retained");
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
    near(second->getParam(TStageObject::T_RotationX)->getValue(0), 0);
    const auto a = prepareAMLevelGeometry(xsheet, 0, 0, false);
    const auto b = prepareAMLevelGeometry(xsheet, 1, 0, false);
    check(a && b && a->triangles.size() == b->triangles.size(),
          "native viewer geometry missing");
    const double units  = Stage::inch / 2.54;
    const auto original = sourceMesh->triangles[0].vertices[0];
    const auto rotated  = a->triangles[0].vertices[0];
    near(rotated[0], original[0] * units);
    near(rotated[1], 0);
    near(rotated[2], original[1] * units);
    check(level->mesh() == sourceMesh,
          "instance animation modified shared geometry");
    const auto savedKey = first->getKeyframe(0);
    first->removeKeyframeWithoutUndo(0);
    check(!first->getParam(TStageObject::T_RotationX)->isKeyframe(0),
          "key deletion omitted Rx");
    first->setKeyframeWithoutUndo(0, savedKey);
    near(first->getParam(TStageObject::T_RotationX)->getValue(0), 90);
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
    near(old.x, unchanged.x);
    near(old.y, unchanged.y);
    legacy->getParam(TStageObject::T_RotationX)->deleteKeyframe(0);
    legacy->setKeyframeWithoutUndo(0);
    check(!legacy->getParam(TStageObject::T_RotationX)->isKeyframe(0),
          "ordinary global key added AM channels");
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
    auto *restoredObject =
        restored->getStageObject(TStageObjectId::ColumnId(0));
    check(restoredObject->isAMTransformEnabled(),
          "spatial capability lost on reopen");
    near(restoredObject->getParam(TStageObject::T_RotationX)->getValue(0), 90);
    near(restoredObject->getParam(TStageObject::T_RotationX)->getValue(10),
         180);
    check(!restored->getStageObject(TStageObjectId::ColumnId(2))
               ->isAMTransformEnabled(),
          "2D level changed on reopen");
    // The same evaluated mesh drives the final raster adapter (opaque static
    // subset).
    auto projected = prepareAMLevelGeometry(xsheet, 1, 0, true);
    TRasterFxP fx(makeAMLevelRenderFx(projected));
    TRaster32P raster(128, 128);
    TTile tile(raster, TPointD(-64, -64));
    TRenderSettings settings;
    fx->compute(tile, 0, settings);
    check(raster->pixels(64)[64].m == 255,
          "model final-render adapter did not draw");
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

// Production FX and native-column regression, without an application or GL
// context.
#include "../../stdfx/transform3dfx.cpp"
#include "../../stdfx/threepointlightfx.cpp"
#include "amfixture.h"
#include "toonz/txshamlevel.h"
#include "toonz/toonzscene.h"
#include "toonz/txsheet.h"
#include "toonz/txshcell.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txsheethandle.h"
#include "toonz/scenefx.h"
#include "toonz/fxcommand.h"
#include "toonz/fxdag.h"
#include "toonz/tcolumnfxset.h"
#include "tparamcontainer.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QFile>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
void set(TFx &fx, const char *name, double value) {
  auto *parameter =
      dynamic_cast<TDoubleParam *>(fx.getParams()->getParam(name));
  require(parameter, "missing FX parameter");
  parameter->setValue(0, value);
}
double coverage(const TRaster32P &raster) {
  double result = 0;
  for (int y = 0; y < raster->getLy(); ++y)
    for (int x = 0; x < raster->getLx(); ++x) result += raster->pixels(y)[x].m;
  return result;
}
}  // namespace
int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  try {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory");
    const QString path = temp.filePath("model.mdl");
    {
      QFile f(path);
      require(f.open(QIODevice::WriteOnly), "write fixture");
      require(f.write(amtest::source.data(), amtest::source.size()) ==
                  qint64(amtest::source.size()),
              "write complete fixture");
    }
    ToonzScene scene;
    scene.setScenePath(TFilePath(temp.filePath("scene.tnz")));
    auto *level = scene.loadLevel(TFilePath(path));
    require(level && level->getAMLevel(), "native model load");
    auto *xsheet = scene.getXsheet();
    require(xsheet->setCell(0, 0, TXshCell(level, TFrameId(1))),
            "expose model");
    auto *column = xsheet->getColumn(0)->getFx();
    require(isAMModelColumn(column), "native column classification");
    const auto original = level->getAMLevel()->mesh();
    TFxP transformOwner = new Transform3DFx;
    auto &transform =
        *static_cast<Transform3DFx *>(transformOwner.getPointer());
    TFxP lightOwner = new ThreePointLightFx;
    auto &light = *static_cast<ThreePointLightFx *>(lightOwner.getPointer());
    auto *port  = transform.getInputPort(0);
    require(canConnectFxPort(port, column), "transform rejects native column");
    require(canConnectFxPort(light.getInputPort(0), column),
            "lighting rejects native column");
    port->setFx(column);
    auto initial = transform.get3DRenderGeometry(0, nullptr);
    require(initial && !initial->triangles.empty(),
            "native column geometry unavailable");
    set(transform, "rotationY", 60);
    auto rotated = transform.get3DRenderGeometry(0, nullptr);
    require(rotated && std::abs(rotated->bounds[2] - initial->bounds[2]) > 1,
            "Y rotation did not change model projection");
    set(transform, "rotationY", 0);
    set(transform, "rotationX", 60);
    rotated = transform.get3DRenderGeometry(0, nullptr);
    require(rotated && std::abs(rotated->bounds[3] - initial->bounds[3]) > 1,
            "X rotation did not change model projection");
    set(transform, "rotationX", 0);
    // Reject unsupported ordinary columns before touching the graph or undo.
    TXshSimpleLevelP drawing = new TXshSimpleLevel;
    drawing->setType(PLI_XSHLEVEL);
    require(xsheet->setCell(0, 1, TXshCell(drawing.getPointer(), TFrameId(1))),
            "expose 2D");
    auto *ordinary = xsheet->getColumn(1)->getFx();
    require(!canConnectFxPort(port, ordinary),
            "ordinary drawing accepted as 3D geometry");
    TXsheetHandle handle;
    handle.setXsheet(xsheet);
    TFxCommand::setParent(ordinary, &transform, 0, &handle);
    require(port->getFx() == column,
            "invalid connection replaced valid source");
    // Model -> Lighting and Model -> Transform -> Lighting both render pixels.
    TRenderSettings info;
    TRaster32P raster(128, 128);
    TTile tile(raster, TPointD(-64, -64));
    light.getInputPort(0)->setFx(column);
    light.doCompute(tile, 0, info);
    require(coverage(raster) > 0, "direct model lighting produced no pixels");
    light.getInputPort(0)->setFx(&transform);
    light.doCompute(tile, 0, info);
    require(coverage(raster) > 0,
            "transformed model lighting produced no pixels");
    set(light, "masterIntensity", 0);
    light.doCompute(tile, 0, info);
    bool hasBlack = false;
    for (int y = 0; y < 128; ++y)
      for (int x = 0; x < 128; ++x) {
        auto p = raster->pixels(y)[x];
        if (p.m && !p.r && !p.g && !p.b) hasBlack = true;
      }
    require(hasBlack, "lighting did not shade native model geometry");
    set(light, "masterIntensity", 100);
    // Immutable render snapshots, cloning and column transparency retain 3D.
    TRasterFxP snapshot = makeAMLevelRenderFx(xsheet, 0, 0);
    require(snapshot && canConnectFxPort(port, snapshot.getPointer()),
            "snapshot is not a 3D source");
    require(
        setAMLevelColorFilter(snapshot.getPointer(), TPixel32(0, 0, 0, 128)),
        "snapshot filter");
    TRasterFxP copy = dynamic_cast<TRasterFx *>(snapshot->clone());
    port->setFx(copy.getPointer());
    transform.doCompute(tile, 0, info);
    const double halfCoverage = coverage(raster);
    require(halfCoverage > 0, "filtered model is empty");
    require(setAMLevelColorFilter(copy.getPointer(), TPixel32::Black),
            "cloned snapshot filter");
    transform.doCompute(tile, 0, info);
    require(coverage(raster) > halfCoverage * 1.9,
            "model opacity was discarded");
    port->setFx(column);
    // Exercise FxBuilder, not just a direct typed-port connection.
    xsheet->getFxDag()->getInternalFxs()->addFx(&transform);
    xsheet->getFxDag()->getInternalFxs()->addFx(&light);
    TFxP tree        = buildSceneFx(&scene, 0, xsheet, lightOwner, BSFX_NO_TR);
    auto *renderRoot = dynamic_cast<TRasterFx *>(tree.getPointer());
    TRectD bounds;
    require(renderRoot && renderRoot->doGetBBox(0, bounds, info) &&
                !bounds.isEmpty(),
            "native model was lost while building the render tree");
    require(level->getAMLevel()->mesh() == original,
            "FX modified the authoritative preview mesh");
    int canceled = 1;
    require(!transform.get3DRenderScene(0, &canceled),
            "cancellation not honored");
    require(!transform.get3DRenderScene(1, nullptr),
            "empty exposure rendered a stale model");
    std::cout << "A:M native FX connections, transforms, lighting, filters and "
                 "render tree passed\n";
    return 0;
  } catch (const TException &e) {
    std::cerr << QString::fromStdWString(e.getMessage()).toStdString() << '\n';
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
  }
  return 1;
}

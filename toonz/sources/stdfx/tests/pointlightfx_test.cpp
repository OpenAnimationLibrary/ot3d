#include "../glbmodelfx.cpp"
#include "../pointlightfx.cpp"
#include "../threepointlightfx.cpp"
#include "../merge3dfx.cpp"
#include "../transform3dfx.cpp"
#include "../../glb/tests/glbfixture.h"
#include "tparamcontainer.h"
#include "toonz/scenefx.h"
#include "toonz/toonzscene.h"
#include "toonz/txsheet.h"
#include "toonz/txshzeraryfxcolumn.h"
#include "toonz/fxdag.h"
#include "toonz/tcolumnfxset.h"
#include "toonz/tstageobject.h"
#include "tstream.h"
#include "trenderer.h"
#include "tthread.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

namespace {
void checkPoint(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
template <class P>
P *pointParam(TFx &fx, const char *name) {
  auto *p = dynamic_cast<P *>(fx.getParams()->getParam(name));
  checkPoint(p, "Point Light parameter missing");
  return p;
}
void setPoint(TFx &fx, const char *name, double value, double frame = 0) {
  pointParam<TDoubleParam>(fx, name)->setValue(frame, value);
}
otglb::ColorPixel pointCenter(T3DRenderSource &fx, double frame = 0) {
  const auto scene = fx.get3DRenderScene(frame, nullptr);
  checkPoint(scene && !scene->empty(), "Point Light scene missing");
  otglb::RenderTile tile;
  tile.width = tile.height = 1;
  tile.x = tile.y = -.5;
  return otglb::renderTile(*scene, tile)[0];
}
}  // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  TThread::init();
  TRenderer::initialize();
  QTemporaryDir directory;
  try {
    checkPoint(directory.isValid(), "Point Light temporary directory missing");
    const auto path  = directory.filePath("point.glb");
    const auto bytes = triangleGlb();
    QFile file(path);
    checkPoint(file.open(QIODevice::WriteOnly), "Point Light fixture open");
    checkPoint(file.write(reinterpret_cast<const char *>(bytes.data()),
                          bytes.size()) == qint64(bytes.size()),
               "Point Light fixture write");
    file.close();
    TFxP modelOwner = new GlbModelFx, targetOwner = new GlbModelFx;
    auto &model  = *static_cast<GlbModelFx *>(modelOwner.getPointer());
    auto &target = *static_cast<GlbModelFx *>(targetOwner.getPointer());
    for (auto *m : {&model, &target}) {
      pointParam<TStringParam>(*m, "modelFile")->setValue(path.toStdWString());
      pointParam<TIntEnumParam>(*m, "colorMode")->setValue(1);
    }
    TFxP owner  = new PointLightFx;
    auto &light = *static_cast<PointLightFx *>(owner.getPointer());
    checkPoint(light.getFxType() == "STD_pointLightFx" &&
                   light.getInputPortName(0) == "Source" &&
                   light.getInputPortName(1) == "Target",
               "Point Light registration/ports");
    checkPoint(!light.get3DRenderScene(0, nullptr),
               "Point Light unconnected source");
    light.getInputPort(0)->setFx(&model);
    auto *type = pointParam<TIntEnumParam>(light, "lightType");
    checkPoint(type->getValue() == 1, "Point Light default must be Bulb");
    checkPoint(pointCenter(light).r > .95, "Bulb did not light model");
    type->setValue(0);
    checkPoint(pointCenter(light).r > .99, "Sun did not light model");
    type->setValue(2);
    setPoint(light, "coneAngle", 20);
    checkPoint(pointCenter(light).r > .95, "Klieg did not light model");
    setPoint(target, "positionX", 20);
    setPoint(target, "positionX", 0, 1);
    light.getInputPort(1)->setFx(&target);
    checkPoint(pointCenter(light).r < .001 && pointCenter(light, 1).r > .95,
               "Klieg did not track animated Target");
    const auto geometry = light.get3DRenderGeometry(0, nullptr);
    checkPoint(geometry && geometry->triangles.size() == 1,
               "Target was rendered as geometry");
    light.getInputPort(1)->setFx(nullptr);
    setPoint(light, "targetX", 20);
    checkPoint(pointCenter(light).r < .001, "Manual aim ignored");
    auto *visible = pointParam<TBoolParam>(light, "visible");
    visible->setValue(true);
    checkPoint(pointCenter(light).r > .99, "Visible source was not rendered");
    const auto visibleGeometry = light.get3DRenderGeometry(0, nullptr);
    checkPoint(visibleGeometry->triangles.size() > geometry->triangles.size(),
               "Visible source absent from bounds geometry");
    setPoint(light, "positionZ", -1);
    checkPoint(pointCenter(light).r < .001,
               "Model did not occlude visible source");
    setPoint(light, "positionZ", 5);
    visible->setValue(false);
    setPoint(light, "targetX", 0);
    type->setValue(0);
    pointParam<TPixelParam>(light, "color")
        ->setValue(0, TPixel32(255, 0, 0, 255));
    setPoint(light, "intensity", 25);
    TFxP secondOwner = new PointLightFx;
    auto &second     = *static_cast<PointLightFx *>(secondOwner.getPointer());
    second.getInputPort(0)->setFx(&light);
    pointParam<TIntEnumParam>(second, "lightType")->setValue(0);
    pointParam<TPixelParam>(second, "color")
        ->setValue(0, TPixel32(0, 255, 0, 255));
    setPoint(second, "intensity", 50);
    const auto pair = pointCenter(second);
    checkPoint(pair.r > .53 && pair.r < .55 && pair.g > .73 && pair.g < .74 &&
                   pair.b == 0,
               "Point Light chain lost independent contributions");
    checkPoint(pointCenter(light).g == 0,
               "Point Light mutated upstream source");
    // Production scene expansion: optional Target must not become a raster
    // affine.
    ToonzScene scene;
    auto *sheet = scene.getXsheet();
    for (int i = 0; i < 2; ++i) {
      TXshZeraryFxColumnP column = new TXshZeraryFxColumn(2);
      column->getZeraryColumnFx()->setZeraryFx(i ? &target : &model);
      sheet->insertColumn(i, column.getPointer());
    }
    light.getInputPort(0)->setFx(sheet->getColumn(0)->getFx());
    light.getInputPort(1)->setFx(sheet->getColumn(1)->getFx());
    sheet->getFxDag()->getInternalFxs()->addFx(&light);
    type->setValue(2);
    visible->setValue(true);
    const TFxP tree = buildSceneFx(&scene, 0.0, owner, false);
    auto *built     = dynamic_cast<PointLightFx *>(tree.getPointer());
    checkPoint(built && pointCenter(*built).r > .53,
               "Point Light scene expansion lost visible light");
    const TFxP copy = tree->clone(true);
    checkPoint(dynamic_cast<PointLightFx *>(copy.getPointer()),
               "Point Light clone lost type");
    const auto clonePixel =
        pointCenter(*static_cast<PointLightFx *>(copy.getPointer()));
    checkPoint(clonePixel.r == pointCenter(*built).r,
               "Point Light clone lost render settings");
    const auto saved = directory.filePath("point-scene.xml");
    {
      TOStream stream{TFilePath(saved)};
      stream << sheet;
    }
    TPersist *persist = nullptr;
    {
      TIStream stream{TFilePath(saved)};
      stream >> persist;
    }
    std::unique_ptr<TPersist> restoredOwner(persist);
    auto *restored = dynamic_cast<TXsheet *>(persist);
    checkPoint(restored, "Point Light scene restore failed");
    bool found = false;
    auto *fxs  = restored->getFxDag()->getInternalFxs();
    for (int i = 0; i < fxs->getFxCount(); ++i)
      if (auto *fx = dynamic_cast<PointLightFx *>(fxs->getFx(i)))
        found = fx->getInputPort("Source")->getFx() &&
                fx->getInputPort("Target")->getFx() &&
                pointParam<TBoolParam>(*fx, "visible")->getValue() &&
                pointParam<TIntEnumParam>(*fx, "lightType")->getValue() == 2;
    checkPoint(found, "Point Light save/reload lost parameters or ports");
    int canceled = 1;
    checkPoint(!light.get3DRenderScene(0, &canceled),
               "Point Light ignored cancellation");
    std::cout << "PASS Point Light modes, targets, visibility, chaining and "
                 "persistence\n";
    return 0;
  } catch (const TException &e) {
    std::cerr << QString::fromStdWString(e.getMessage()).toStdString() << '\n';
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
  }
  return 1;
}

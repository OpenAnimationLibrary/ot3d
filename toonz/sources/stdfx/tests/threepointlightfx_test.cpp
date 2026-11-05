// Exercise the production 3D source contract and Three-Point Light node without
// linking tnzstdfx, matching the GLB Model framework-test pattern.
#include "../glbmodelfx.cpp"
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
#include "tfxutil.h"
#include "tstream.h"
#include "trenderer.h"
#include "tthread.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <stdexcept>

class OrdinaryRasterFx final : public TStandardRasterFx {
  FX_PLUGIN_DECLARATION(OrdinaryRasterFx)
public:
  bool doGetBBox(double, TRectD &box, const TRenderSettings &) override { box = TRectD(); return false; }
  bool canHandle(const TRenderSettings &, double) override { return true; }
  void doCompute(TTile &tile, double, const TRenderSettings &) override { tile.getRaster()->clear(); }
};
FX_PLUGIN_IDENTIFIER(OrdinaryRasterFx, "ordinaryRasterTestFx")

namespace {
void check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
template <class PARAM, class FX> PARAM *param(FX &fx, const char *name) {
  auto *value = dynamic_cast<PARAM *>(fx.getParams()->getParam(name));
  check(value != nullptr, "missing Three-Point Light parameter"); return value;
}
void writeModel(const QString &path) {
  const auto bytes = triangleGlb(); QFile file(path);
  check(file.open(QIODevice::WriteOnly), "cannot create GLB fixture");
  check(file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()) == qint64(bytes.size()), "incomplete GLB fixture write");
}
template <class FX>
TPixel32 center(FX &fx) {
  TRaster32P raster(80, 80); TTile tile; tile.setRaster(raster); tile.m_pos = TPointD(-40, -40);
  TRenderSettings settings; settings.m_affine = TScale(.2); fx.doCompute(tile, 0, settings); return raster->pixels(40)[40];
}
void zeroSecondaryLights(ThreePointLightFx &fx) {
  param<TDoubleParam>(fx, "ambient")->setValue(0, 0.0);
  param<TDoubleParam>(fx, "fillIntensity")->setValue(0, 0.0);
  param<TDoubleParam>(fx, "rimIntensity")->setValue(0, 0.0);
}

TFxP makeKey(TFx *source, const TPixel32 &color, double master) {
  TFxP owner  = new ThreePointLightFx;
  auto &light = *static_cast<ThreePointLightFx *>(owner.getPointer());
  light.getInputPort(0)->setFx(source);
  zeroSecondaryLights(light);
  param<TDoubleParam>(light, "masterIntensity")->setValue(0, master);
  param<TDoubleParam>(light, "keyAzimuth")->setValue(0, 0.0);
  param<TDoubleParam>(light, "keyElevation")->setValue(0, 0.0);
  param<TPixelParam>(light, "keyColor")->setValue(0, color);
  return owner;
}

void testLightingChain(ToonzScene &scene, TFx *column) {
  const auto red   = makeKey(column, TPixel32(255, 0, 0, 255), 25.0);
  const auto green = makeKey(red.getPointer(), TPixel32(0, 255, 0, 255), 50.0);
  const auto blue = makeKey(green.getPointer(), TPixel32(0, 0, 255, 255), 12.5);
  auto &first     = *static_cast<ThreePointLightFx *>(red.getPointer());
  auto &second    = *static_cast<ThreePointLightFx *>(green.getPointer());
  auto &last      = *static_cast<ThreePointLightFx *>(blue.getPointer());

  const auto redOnly = center(first);
  const auto pair    = center(second);
  const auto all     = center(last);
  check(redOnly.r > 130 && redOnly.r < 145 && redOnly.g == 0 &&
            redOnly.b == 0 && pair.m == 255 && pair.r == redOnly.r &&
            pair.g > 180 && pair.g < 195 && pair.b == 0,
        "two rigs did not contribute with independent master intensities");
  check(all.m == 255 && all.r == pair.r && all.g == pair.g && all.b > 90 &&
            all.b < 105 && center(first) == redOnly,
        "third rig changed the other lights or contaminated the shared source");

  param<TDoubleParam>(second, "ambient")->setValue(0, 10.0);
  const auto withAmbient = center(last);
  check(withAmbient.r > all.r && withAmbient.g > all.g && withAmbient.b > all.b,
        "middle rig ambient did not contribute");
  param<TDoubleParam>(second, "masterIntensity")->setValue(0, 0.0);
  const auto muted = center(last);
  check(muted.m == 255 && muted.r == all.r && muted.g == 0 && muted.b == all.b,
        "muting the middle rig affected other rigs or left ambient light");

  TRectD box;
  TRenderSettings settings;
  check(last.doGetBBox(0, box, settings) && !box.isEmpty(),
        "chained light nodes lost model bounds");
  const auto built = buildSceneFx(&scene, 0.0, blue, false);
  check(bool(built), "scene expansion lost the chain");
  const TFxP clone = built->clone(true);
  auto *cloned     = dynamic_cast<ThreePointLightFx *>(clone.getPointer());
  check(cloned && center(*cloned) == muted,
        "scene expansion or cloning changed the chained render");
  check(!buildSceneFx(&scene, 1.0, blue, false),
        "chained lights ignored the GLB exposure range");

  auto *mode = param<TIntEnumParam>(second, "lightMode");
  check(mode->getValue() == 0, "existing lights must default to directional");
  mode->setValue(1);
  param<TDoubleParam>(second, "ambient")->setValue(0, 0.0);
  param<TDoubleParam>(second, "masterIntensity")->setValue(0, 50.0);
  param<TDoubleParam>(second, "rigRadius")->setValue(0, 2.0);
  auto *z = param<TDoubleParam>(second, "rigPositionZ");
  z->setValue(0, 0.0);
  const auto front = center(last);
  z->setValue(0, -4.0);
  const auto rear = center(last);
  check(front.g > 170 && rear == muted && front.r == muted.r &&
            front.b == muted.b && front.m == muted.m,
        "moving one rig did not isolate its contribution in a mixed chain");
}

bool renderMergeOutput(ToonzScene &scene) {
  const TFxP output = buildSceneFx(&scene, scene.getXsheet(), 0, 1, false);
  TRenderSettings settings;
  auto *rasterFx = dynamic_cast<TRasterFx *>(output.getPointer());
  check(rasterFx, "Xsheet rejected lit merge");
  TRaster32P raster(128, 128);
  TTile tile(raster, TPointD(-64, -64));
  TRenderer renderer(1);
  renderer.enablePrecomputing(false);
  const auto id = TRenderer::buildRenderId();
  renderer.install(id);
  renderer.declareRenderStart(id);
  renderer.declareFrameStart(0);
  rasterFx->compute(tile, 0, settings);
  renderer.declareFrameEnd(0);
  renderer.declareRenderEnd(id);
  renderer.uninstall();
  bool red = false;
  for (int y = 0; y < 128; ++y)
    for (int x = 0; x < 128; ++x) {
      const auto p = raster->pixels(y)[x];
      red |= p.m && p.r > 0 && p.g == 0 && p.b == 0;
    }
  return red;
}

void testMerge(const QString &path, const QString &saved) {
  ToonzScene scene;
  TFx *columns[2];
  for (int i = 0; i < 2; ++i) {
    TFxP source = new GlbModelFx;
    auto &model = *static_cast<GlbModelFx *>(source.getPointer());
    param<TStringParam>(model, "modelFile")->setValue(path.toStdWString());
    param<TIntEnumParam>(model, "colorMode")->setValue(1);
    param<TDoubleParam>(model, "positionX")->setValue(0, i ? .8 : -.8);
    TXshZeraryFxColumnP column = new TXshZeraryFxColumn(1);
    column->getZeraryColumnFx()->setZeraryFx(&model);
    scene.getXsheet()->insertColumn(i, column.getPointer());
    columns[i] = column->getFx();
  }
  TFxP mergeOwner = new Merge3DFx;
  auto &merge     = *static_cast<Merge3DFx *>(mergeOwner.getPointer());
  TFxP ordinary   = new OrdinaryRasterFx;
  check(merge.getFxType() == "STD_merge3DFx", "merge registration");
  check(canConnectFxPort(merge.getInputPort(0), columns[0]) &&
            !canConnectFxPort(merge.getInputPort(0), ordinary.getPointer()),
        "merge port policy");
  merge.getInputPort(0)->setFx(columns[0]);
  merge.getInputPort(1)->setFx(columns[1]);
  const auto key = makeKey(&merge, TPixel32(255, 0, 0, 255), 100);
  auto &light    = *static_cast<ThreePointLightFx *>(key.getPointer());
  auto layers = light.get3DRenderScene(0, nullptr, nullptr, nullptr, nullptr);
  check(layers && layers->layers.size() == 2 && !layers->empty(),
        "lighting lost merged sources");
  for (const auto &layer : layers->layers)
    check(layer.scene->triangles[0].color[0] > .99 &&
              layer.scene->triangles[0].color[1] == 0,
          "shared lighting failed to reach both branches");
  const auto branchRig = makeKey(columns[0], TPixel32(255, 0, 0, 255), 25);
  merge.getInputPort(0)->setFx(branchRig.getPointer());
  param<TPixelParam>(light, "keyColor")->setValue(0, TPixel32(0, 255, 0, 255));
  const auto branchLit =
      light.get3DRenderScene(0, nullptr, nullptr, nullptr, nullptr);
  check(branchLit->layers[0].scene->triangles[0].color[0] == 0 &&
            branchLit->layers[1].scene->triangles[0].color[0] > .5 &&
            branchLit->layers[0].scene->triangles[0].color[1] > .99 &&
            branchLit->layers[1].scene->triangles[0].color[1] > .99,
        "branch-specific and shared rigs did not accumulate independently");
  merge.getInputPort(0)->setFx(columns[0]);
  param<TPixelParam>(light, "keyColor")->setValue(0, TPixel32(255, 0, 0, 255));
  TFxP nestedOwner = new Merge3DFx;
  auto &nested     = *static_cast<Merge3DFx *>(nestedOwner.getPointer());
  nested.getInputPort(0)->setFx(&merge);
  light.getInputPort(0)->setFx(&nested);
  const auto nestedLit =
      light.get3DRenderScene(0, nullptr, nullptr, nullptr, nullptr);
  check(nestedLit && nestedLit->layers.size() == 1 &&
            nestedLit->layers[0].scene->layers.size() == 2,
        "nested merge or empty input discarded grouped sources");
  light.getInputPort(0)->setFx(&merge);
  TFxP transformOwner = new Transform3DFx;
  auto &transform = *static_cast<Transform3DFx *>(transformOwner.getPointer());
  transform.getInputPort(0)->setFx(&light);
  param<TDoubleParam>(transform, "positionY")->setValue(0, .25);
  const auto moved = transform.get3DRenderScene(0, nullptr);
  check(moved && moved->layers.size() == 2 &&
            moved->bounds[1] > layers->bounds[1],
        "group transform did not reach both branches");
  TRenderSettings settings;
  TRectD box;
  check(light.doGetBBox(0, box, settings) && !box.isEmpty(),
        "group light bounds");
  auto *sheet = scene.getXsheet();
  sheet->getStageObject(TStageObjectId::ColumnId(1))
      ->getParam(TStageObject::T_X)
      ->setValue(0, .5);
  auto *dag = sheet->getFxDag();
  dag->getInternalFxs()->addFx(&merge);
  dag->getInternalFxs()->addFx(&light);
  for (auto *column : columns) dag->removeFromXsheet(column);
  dag->addToXsheet(&light);
  check(renderMergeOutput(scene),
        "lit merge did not connect directly to Xsheet");
  const TFxP built = buildSceneFx(&scene, 0.0, key, false);
  auto *builtLight = dynamic_cast<ThreePointLightFx *>(built.getPointer());
  check(builtLight, "group render-tree expansion");
  const auto placed =
      builtLight->get3DRenderScene(0, nullptr, nullptr, nullptr, &settings);
  check(placed && placed->layers.size() == 2 &&
            placed->layers[0].affine[2] > 1 && placed->layers[1].affine[2] == 0,
        "independent column placement was lost or flattened");
  const TFxP copy = built->clone(true);
  check(center(*static_cast<ThreePointLightFx *>(copy.getPointer())) ==
            center(*builtLight),
        "cloning discarded group placement");
  // The lit merge is a normal raster source at the 2D boundary: Over -> Xsheet.
  TFxP over = TFx::create("overFx");
  over->connect("Source1", &light);
  over->connect("Source2", columns[0]);
  dag->getInternalFxs()->addFx(over.getPointer());
  dag->removeFromXsheet(&light);
  dag->addToXsheet(over.getPointer());
  check(renderMergeOutput(scene),
        "lit merge did not render through standard Over and Xsheet");
  {
    TOStream stream{TFilePath(saved)};
    stream << sheet;
  }
  TPersist *persist = nullptr;
  {
    TIStream stream{TFilePath(saved)};
    stream >> persist;
  }
  std::unique_ptr<TPersist> owner(persist);
  auto *restored = dynamic_cast<TXsheet *>(persist);
  check(restored, "group scene persistence");
  bool found = false;
  auto *fxs  = restored->getFxDag()->getInternalFxs();
  for (int i = 0; i < fxs->getFxCount(); ++i)
    if (auto *fx = dynamic_cast<Merge3DFx *>(fxs->getFx(i))) {
      found = fx->getInputPort("Foreground")->getFx() &&
              fx->getInputPort("Background")->getFx();
    }
  check(found, "saved merge lost its inputs");
  sheet->getColumn(0)->setPreviewVisible(false);
  const TFxP hidden = buildSceneFx(&scene, 0.0, key, false);
  const auto visible =
      static_cast<ThreePointLightFx *>(hidden.getPointer())
          ->get3DRenderScene(0, nullptr, nullptr, nullptr, &settings);
  check(visible && visible->layers.size() == 1,
        "hidden source survived group expansion");
  const TFxP emptyFrame = buildSceneFx(&scene, 1.0, key, false);
  const auto empty =
      emptyFrame
          ? static_cast<ThreePointLightFx *>(emptyFrame.getPointer())
                ->get3DRenderScene(1, nullptr, nullptr, nullptr, &settings)
          : nullptr;
  check(!empty || empty->empty(), "merge ignored exposure range");
  int canceled = 1;
  check(!merge.get3DRenderScene(0, &canceled), "merge cancellation ignored");
}
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv); QTemporaryDir directory;
  TThread::init();
  TRenderer::initialize();
  try {
    check(directory.isValid(), "temporary directory unavailable");
    const QString path = directory.filePath("three-point.glb"); writeModel(path); ToonzScene scene;
    TFxP modelOwner = new GlbModelFx; auto &model = *static_cast<GlbModelFx *>(modelOwner.getPointer());
    param<TStringParam>(model, "modelFile")->setValue(path.toStdWString());
    param<TIntEnumParam>(model, "colorMode")->setValue(1);
    TXshZeraryFxColumnP modelColumn = new TXshZeraryFxColumn(1); auto *columnFx = modelColumn->getZeraryColumnFx();
    columnFx->setZeraryFx(&model); scene.getXsheet()->insertColumn(0, modelColumn.getPointer());
    TFxP lightOwner = new ThreePointLightFx; auto &light = *static_cast<ThreePointLightFx *>(lightOwner.getPointer());
    check(light.getFxType() == "STD_threePointLightFx", "unexpected Three-Point Light FX type");
    // Port names are persistence keys. They must not contain whitespace: scene
    // streams tokenize the name, which is why the former "GLB Model" saved as
    // "GLB" and then failed to reload with "port 'GLB' is not present".
    check(light.getInputPortCount() == 1 && light.getInputPortName(0) == "GLB" && light.getInputPort("GLB") != nullptr,
          "Three-Point Light persistence port must be GLB");
    auto *port = dynamic_cast<T3DSourcePort *>(light.getInputPort(0));
    check(port && !port->source() && center(light).m == 0, "unconnected lighting node did not render empty");
    bool rejected = false; TFxP ordinary = new OrdinaryRasterFx;
    try { port->setFx(ordinary.getPointer()); } catch (const TException &) { rejected = true; }
    check(rejected && !port->isConnected(), "ordinary raster FX was accepted as a 3D source");
    port->setFx(&model); check(port->source() == static_cast<T3DRenderSource *>(&model), "GLB Model was rejected as a 3D source");
    zeroSecondaryLights(light); param<TDoubleParam>(light, "keyAzimuth")->setValue(0, 0.0);
    param<TDoubleParam>(light, "keyElevation")->setValue(0, 0.0); param<TDoubleParam>(light, "keyIntensity")->setValue(0, 100.0);
    param<TPixelParam>(light, "keyColor")->setValue(0, TPixel32::White);
    TRectD box; TRenderSettings settings; check(light.doGetBBox(0, box, settings) && !box.isEmpty(), "lit model has no bounding box");
    const auto white = center(light); check(white.m == 255 && white.r > 245 && white.g > 245 && white.b > 245, "front white key did not illuminate the model");
    port->setFx(columnFx);
    check(port->getFx() == columnFx && port->source() == static_cast<T3DRenderSource *>(&model), "GLB column was rejected or replaced");
    check(columnFx->getOutputConnectionCount() == 1 && columnFx->getOutputConnection(0) == port && center(light) == white, "column ownership incorrect");
    TFxP built = buildSceneFx(&scene, 0.0, lightOwner, false); check(bool(built), "FX Settings scene expansion lost lighting node");
    TFxP clonedOwner = built->clone(true); auto *cloned = dynamic_cast<ThreePointLightFx *>(clonedOwner.getPointer());
    check(cloned && cloned->getInputPortName(0) == "GLB" && center(*cloned) == white && port->getFx() == columnFx,
          "scene expansion or clone lost persistent GLB source");
    check(!buildSceneFx(&scene, 1.0, lightOwner, false), "lighting ignored GLB column exposure range");
    TFxP emptyColumn = new TZeraryColumnFx;
    for (TFx *invalid : {ordinary.getPointer(), emptyColumn.getPointer()}) {
      rejected = false; try { port->setFx(invalid); } catch (const TException &) { rejected = true; }
      check(rejected && port->getFx() == columnFx && columnFx->getOutputConnectionCount() == 1 && columnFx->getOutputConnection(0) == port,
            "invalid input damaged existing GLB connection");
    }
    param<TPixelParam>(light, "keyColor")->setValue(0, TPixel32(255, 0, 0, 255));
    const auto red = center(light); check(red.m == 255 && red.r > 245 && red.g < 5 && red.b < 5, "colored key did not reach output");
    param<TDoubleParam>(light, "keyAzimuth")->setValue(0, 180.0); const auto dark = center(light);
    check(dark.m == 255 && dark.r < 5 && dark.g < 5 && dark.b < 5, "rear-facing key should not light front triangle");
    check(param<TIntEnumParam>(model, "lighting")->getValue() == 0 && param<TIntEnumParam>(model, "colorMode")->getValue() == 1,
          "downstream light mutated GLB Model settings");
    port->setFx(nullptr); check(!port->source() && columnFx->getOutputConnectionCount() == 0 && center(light).m == 0,
                                "disconnect did not release column or clear output");
    testLightingChain(scene, columnFx);
    testMerge(path, directory.filePath("merge.xml"));
    std::cout << "PASS: Three-Point Light sources, chaining, and rendering\n";
    return 0;
  } catch (const TException &) { std::cerr << "FAIL threepointlightfx: unexpected FX exception\n"; return 1; }
    catch (const std::exception &error) { std::cerr << "FAIL threepointlightfx: " << error.what() << '\n'; return 1; }
}

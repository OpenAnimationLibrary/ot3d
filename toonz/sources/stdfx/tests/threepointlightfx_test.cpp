// Exercise the production 3D source contract and Three-Point Light node without
// linking tnzstdfx, matching the GLB Model framework-test pattern.
#include "../glbmodelfx.cpp"
#include "../threepointlightfx.cpp"
#include "../../glb/tests/glbfixture.h"
#include "tparamcontainer.h"
#include "toonz/scenefx.h"
#include "toonz/toonzscene.h"
#include "toonz/txsheet.h"
#include "toonz/txshzeraryfxcolumn.h"

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
TPixel32 center(ThreePointLightFx &fx) {
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
}
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv); QTemporaryDir directory;
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
    std::cout << "PASS: Three-Point Light sources, chaining, and rendering\n";
    return 0;
  } catch (const TException &) { std::cerr << "FAIL threepointlightfx: unexpected FX exception\n"; return 1; }
    catch (const std::exception &error) { std::cerr << "FAIL threepointlightfx: " << error.what() << '\n'; return 1; }
}

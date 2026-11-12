// Production FX and native-column regression, without an application or GL
// context.
#include "../../stdfx/ammaterialfx.cpp"
#include "../../stdfx/inklinefx.cpp"
#include "../../stdfx/transform3dfx.cpp"
#include "../../stdfx/threepointlightfx.cpp"
#include "../../stdfx/merge3dfx.cpp"
#include "../../stdfx/glbmodelfx.cpp"
#include "../../glb/tests/glbfixture.h"
#include "amfixture.h"
#include "toonz/txshamlevel.h"
#include "toonz/toonzscene.h"
#include "toonz/txsheet.h"
#include "toonz/txshcell.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshlevelcolumn.h"
#include "toonz/vectoraction.h"
#include "toonz/txsheethandle.h"
#include "toonz/scenefx.h"
#include "toonz/fxcommand.h"
#include "toonz/fxdag.h"
#include "toonz/tcolumnfxset.h"
#include "toonz/sceneproperties.h"
#include "toonz/txshchildlevel.h"
#include "toonz/txshzeraryfxcolumn.h"
#include "toonz/tstageobject.h"
#include "toutputproperties.h"
#include "trenderer.h"
#include "tthread.h"
#include "tstream.h"
#include "tparamcontainer.h"
#include "tpalette.h"
#include "tstroke.h"
#include "tvectorimage.h"
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
double brightness(const TRaster32P &raster) {
  double result = 0;
  for (int y = 0; y < raster->getLy(); ++y)
    for (int x = 0; x < raster->getLx(); ++x) {
      const auto p = raster->pixels(y)[x];
      result += p.r + p.g + p.b;
    }
  return result;
}
TRaster32P render(ToonzScene &scene, const TRenderSettings &settings,
                  double frame = 0) {
  const TFxP tree =
      buildSceneFx(&scene, scene.getXsheet(), frame,
                   TOutputProperties::AllLevels, 1, false, settings);
  auto *fx = dynamic_cast<TRasterFx *>(tree.getPointer());
  require(fx, "missing scene render tree");
  TRaster32P raster(128, 128);
  TTile tile(raster, TPointD(-64, -64));
  TRenderer renderer(1);
  renderer.enablePrecomputing(false);
  const auto id = TRenderer::buildRenderId();
  renderer.install(id);
  renderer.declareRenderStart(id);
  renderer.declareFrameStart(frame);
  fx->compute(tile, frame, settings);
  renderer.declareFrameEnd(frame);
  renderer.declareRenderEnd(id);
  renderer.uninstall();
  return raster;
}
void testVectorInkLine(const QTemporaryDir &temp) {
  ToonzScene scene;
  scene.setScenePath(TFilePath(temp.filePath("vector-ink.tnz")));
  scene.getProperties()->getOutputProperties()->setFrameRate(24);
  TXshSimpleLevelP level = new TXshSimpleLevel(L"Vector Ink");
  level->setScene(&scene);
  level->setType(PLI_XSHLEVEL);
  level->setPath(TFilePath(temp.filePath("vector-ink.pli")));
  level->setPalette(new TPalette);
  auto *palette  = level->getPalette();
  const int red  = palette->addStyle(TPixel32::Red);
  const int blue = palette->addStyle(TPixel32::Blue);

  TVectorImageP drawing(new TVectorImage);
  drawing->setPalette(palette);
  auto *loop = new TStroke({{-30, -12, 2},
                            {-20, -12, 2},
                            {-10, -12, 2},
                            {-10, 0, 2},
                            {-10, 12, 2},
                            {-20, 12, 2},
                            {-30, 12, 2},
                            {-30, 0, 2},
                            {-30, -12, 2}});
  loop->setSelfLoop();
  loop->setStyle(red);
  drawing->addStroke(loop, false);
  auto *line = new TStroke({{0, 0, 3}, {15, 0, 3}, {30, 0, 3}});
  line->setStyle(red);
  drawing->addStroke(line, false);
  drawing->findRegions();
  drawing->fill(TPointD(-20, 0), blue);
  const TFrameId fid(1);
  level->setFrame(fid, drawing);

  auto *xsheet = scene.getXsheet();
  require(xsheet->setCell(0, 0, TXshCell(level.getPointer(), fid)) &&
              xsheet->setCell(24, 0, TXshCell(level.getPointer(), fid)),
          "expose vector ink drawing");
  auto *column = xsheet->getColumn(0)->getLevelColumn();
  TFxP owner   = new InkLineFx;
  auto &ink    = *static_cast<InkLineFx *>(owner.getPointer());
  require(canConnectFxPort(ink.getInputPort(0), column->getFx()),
          "Ink Line rejected a vector column");
  ink.getInputPort(0)->setFx(column->getFx());

  TRenderSettings info;
  TRectD originalBounds, inkBounds;
  require(ink.doGetBBox(0, originalBounds, info), "vector ink bounds missing");
  set(ink, "startWidth", 12);
  require(ink.doGetBBox(0, inkBounds, info) &&
              inkBounds.getLx() > originalBounds.getLx(),
          "vector ink bounds did not grow with the profile width");

  auto action = VectorAction::forDrawing(drawing);
  VectorAction::Offsets offsets;
  for (int i = 0; i < line->getControlPointCount(); ++i)
    offsets[{1, i}] = TPointD(0, 24);
  action.setKeys(1, offsets);
  column->setVectorAction(
      {level.getPointer(), fid, TFilePath(temp.filePath("vector-ink.vact")), 0,
       std::make_shared<const VectorAction>(std::move(action))});
  TRectD posedBounds;
  require(
      ink.doGetBBox(24, posedBounds, info) && posedBounds.y1 > inkBounds.y1 + 5,
      "vector ink bounds ignored the current Action pose");

#if defined(LINUX) || defined(FREEBSD)
  // The native vector rasterizer uses an X11 OpenGL context on these platforms.
  if (qEnvironmentVariableIsEmpty("DISPLAY")) return;
#endif
  auto *dag = xsheet->getFxDag();
  dag->getInternalFxs()->addFx(owner.getPointer());
  dag->getTerminalFxs()->removeFx(column->getFx());
  dag->getTerminalFxs()->addFx(owner.getPointer());

  auto *color =
      dynamic_cast<TPixelParam *>(ink.getParams()->getParam("startColor"));
  auto *scope =
      dynamic_cast<TIntEnumParam *>(ink.getParams()->getParam("scope"));
  require(color && scope, "vector Ink Line controls missing");
  color->setValue(0, TPixel32::Green);
  set(ink, "startWidth", 3);
  const auto countColor = [](const TRaster32P &raster, const TPixel32 &color) {
    int count = 0;
    for (int y = 0; y < raster->getLy(); ++y)
      for (int x = 0; x < raster->getLx(); ++x)
        if (raster->pixels(y)[x] == color) ++count;
    return count;
  };
  const auto all = render(scene, info);
  require(
      countColor(all, TPixel32::Green) > 0 &&
          countColor(all, TPixel32::Blue) > 0 &&
          countColor(all, TPixel32::Red) == 0,
      "vector Ink Line did not replace the original ink and preserve paint");
  scope->setValue(1);
  const auto unfilled = render(scene, info);
  require(
      countColor(unfilled, TPixel32::Red) > 0 &&
          countColor(unfilled, TPixel32::Green) > 0 &&
          countColor(unfilled, TPixel32::Green) <
              countColor(all, TPixel32::Green),
      "unfilled ink did not preserve the painted region's original outline");
  const auto firstGreenRow = [](const TRaster32P &raster) {
    for (int y = 0; y < raster->getLy(); ++y)
      for (int x = 0; x < raster->getLx(); ++x)
        if (raster->pixels(y)[x] == TPixel32::Green) return y;
    return -1;
  };
  require(firstGreenRow(render(scene, info, 24)) > firstGreenRow(unfilled) + 5,
          "rendered ink did not follow the Vector Action");
  set(ink, "fadeIn", 100);
  require(countColor(render(scene, info), TPixel32::Green) <
              countColor(unfilled, TPixel32::Green),
          "vector ink fade did not change the rendered stroke");
  require(line->getStyle() == red && loop->getStyle() == red,
          "rendering Ink Line changed the source drawing");
}
void testInkLine(const QString &path) {
  {
    QFile file(path);
    const auto &source = amtest::splineSource;
    require(
        file.open(QIODevice::WriteOnly) &&
            file.write(source.data(), source.size()) == qint64(source.size()),
        "write spline-only model fixture");
  }
  ToonzScene scene;
  scene.setScenePath(TFilePath(path).withType("tnz"));
  auto *level = scene.loadLevel(TFilePath(path));
  require(level && level->getAMLevel() &&
              level->getAMLevel()->mesh()->triangles.empty(),
          "spline-only native model failed to load");
  auto *xsheet = scene.getXsheet();
  require(xsheet->setCell(0, 0, TXshCell(level, TFrameId(1))),
          "expose spline-only model");
  auto *column = xsheet->getColumn(0)->getFx();
  TFxP owner   = new InkLineFx;
  auto &ink    = *static_cast<InkLineFx *>(owner.getPointer());
  require(canConnectFxPort(ink.getInputPort(0), column),
          "ink rejects spline-only column");
  ink.getInputPort(0)->setFx(column);
  require(ink.getAMGroups(0) == std::vector<std::wstring>{L"Lower"},
          "spline-only group unavailable to ink settings");
  TRenderSettings info;
  auto *terminals = xsheet->getFxDag()->getTerminalFxs();
  require(coverage(render(scene, info)) == 0,
          "source spline changed the raster without ink");
  xsheet->getFxDag()->getInternalFxs()->addFx(owner.getPointer());
  terminals->removeFx(column);
  terminals->addFx(owner.getPointer());
  const double whole = coverage(render(scene, info));
  require(whole > 0, "scene renderer dropped the spline-only ink");
  auto *group =
      dynamic_cast<TStringParam *>(ink.getParams()->getParam("group"));
  require(group, "ink group parameter");
  group->setValue(L"Lower");
  const double lower = coverage(render(scene, info));
  require(lower > 0 && lower < whole,
          "spline group did not limit the rendered ink");
  const auto flag = [&](const char *name) {
    return dynamic_cast<TBoolParam *>(ink.getParams()->getParam(name));
  };
  auto *width =
      dynamic_cast<TToneCurveParam *>(ink.getParams()->getParam("widthCurve"));
  auto *ramp =
      dynamic_cast<TSpectrumParam *>(ink.getParams()->getParam("colorRamp"));
  require(flag("useSplineProfile") && flag("useColorRamp") && width && ramp,
          "Ink Line spline profile controls are missing");
  flag("useSplineProfile")->setValue(true);
  require(coverage(render(scene, info)) == lower,
          "default spline profile changed ink coverage");
  auto points = width->getValue(0);
  for (auto &point : points) point.y = 255;
  width->setDefaultValue(points);
  const double wide = coverage(render(scene, info));
  require(wide > lower * 1.5, "spline profile did not widen rendered ink");
  flag("useColorRamp")->setValue(true);
  ramp->setDefaultValue(
      TSpectrum(TPixel32(0, 0, 0, 64), TPixel32(0, 0, 0, 64)));
  require(coverage(render(scene, info)) < wide * 0.4,
          "color ramp did not affect rendered opacity");
  flag("useSplineProfile")->setValue(false);
  set(ink, "fadeIn", 100);
  require(coverage(render(scene, info)) < lower,
          "ink fade did not affect output opacity");
  TFxP transformOwner = new Transform3DFx;
  auto &transform = *static_cast<Transform3DFx *>(transformOwner.getPointer());
  transform.getInputPort(0)->setFx(owner.getPointer());
  const auto before = transform.get3DRenderGeometry(0, nullptr);
  set(transform, "positionX", 20);
  const auto moved = transform.get3DRenderGeometry(0, nullptr);
  require(before && moved && !before->empty() && !moved->empty() &&
              std::abs(moved->bounds[0] - before->bounds[0] - 20) < 1e-6,
          "downstream transform lost or failed to move ink bounds");
}
void testMaterials(ToonzScene &scene, TXshLevel *level, TFx *column,
                   const QString &path) {
  TFxP owner     = new AMMaterialFx;
  auto &material = *static_cast<AMMaterialFx *>(owner.getPointer());
  material.getInputPort(0)->setFx(column);
  auto stringParam = [&](const char *name) {
    auto *p =
        dynamic_cast<TStringParam *>(material.getParams()->getParam(name));
    require(p, "missing material parameter");
    return p;
  };
  const auto write = [&](const QByteArray &attributes) {
    QFile f(path);
    require(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "write material");
    const auto bytes = QByteArray(
                           "<MATERIALFILE>\nProductVersion=19.5\n<MATERIALS><"
                           "MATERIAL><ATTRIBUTE>\n") +
                       attributes +
                       "\n</ATTRIBUTE></MATERIAL></MATERIALS></MATERIALFILE>";
    require(f.write(bytes) == bytes.size(), "complete material write");
  };
  write(
      "DiffuseColor=128 0 "
      "255\nAmbiance=100\nAmbianceBlend=100\nTransparency=50");
  stringParam("materialFile")->setValue(path.toStdWString());
  stringParam("group")->setValue(L"Red");
  const auto discovered = material.getAMGroups(0);
  require(discovered.size() == 1 && discovered[0] == L"Red",
          "material group chooser discovery");
  const auto summary = material.getAMMaterialSummary(0);
  require(summary.find(L"Specular Size:") != std::wstring::npos &&
              summary.find(L"Transparency: 50%") != std::wstring::npos,
          "material inspection omitted evaluated surface attributes");
  const auto original = level->getAMLevel()->mesh();
  const auto prepared = material.get3DRenderScene(0, nullptr);
  require(prepared && bool(prepared->sampleShader),
          "material was flattened before shading");
  otglb::RenderTile tile;
  tile.width = tile.height = 32;
  tile.x = tile.y   = -16;
  const auto pixels = otglb::renderTile(*prepared, tile);
  const auto p      = pixels[16 * 32 + 16];
  require(
      std::abs(p.alpha - .5f) < 1e-5 && p.b > .49 && p.r > .24 && p.g < .001,
      "material color/transparency or tessellation seam mismatch");
  TFxP glossOwner = new AMMaterialFx;
  auto &gloss     = *static_cast<AMMaterialFx *>(glossOwner.getPointer());
  gloss.getInputPort(0)->setFx(&material);
  auto *glossEnabled = dynamic_cast<TBoolParam *>(
      gloss.getParams()->getParam("overrideSpecular"));
  require(glossEnabled, "fileless gloss control");
  glossEnabled->setValue(true);
  set(gloss, "specularIntensity", 0);
  require(gloss.getAMMaterialSummary(0).find(L"Inherited from input") !=
              std::wstring::npos,
          "fileless inspection reported defaults as inherited values");
  const auto overlay   = gloss.get3DRenderScene(0, nullptr);
  const auto preserved = otglb::renderTile(*overlay, tile)[16 * 32 + 16];
  require(std::abs(preserved.alpha - p.alpha) < 1e-6 &&
              std::abs(preserved.b - p.b) < 1e-6,
          "fileless specular controls discarded upstream color/opacity");
  TRaster32P r(32, 32);
  TTile output(r, TPointD(-16, -16));
  TRenderSettings settings;
  material.doCompute(output, 0, settings);
  require(r->pixels(16)[16].m == 128, "material FX raster output");
  TFxP clone = material.clone(false);
  require(clone->getAlias(0, settings) == material.getAlias(0, settings),
          "material clone alias");
  // Production render-tree expansion must forward assignments into snapshots.
  scene.getXsheet()->getFxDag()->getInternalFxs()->addFx(&material);
  const auto tree =
      buildSceneFx(&scene, 0, scene.getXsheet(), owner, BSFX_NO_TR);
  auto *source = dynamic_cast<T3DRenderSource *>(tree.getPointer());
  require(source && bool(source->get3DRenderScene(0, nullptr)->sampleShader),
          "material lost during render-tree expansion");
  const QString saved = path + ".xml";
  {
    TOStream os{TFilePath(saved)};
    os << owner.getPointer();
  }
  TPersist *persist = nullptr;
  {
    TIStream is{TFilePath(saved)};
    is >> persist;
  }
  TFxP reopened = dynamic_cast<TFx *>(persist);
  require(reopened && reopened->getInputPort(0)->getFx(),
          "material scene persistence");
  auto *restored =
      dynamic_cast<TStringParam *>(reopened->getParams()->getParam("group"));
  require(restored && restored->getValue() == L"Red",
          "material group persistence");
  // Diffuse color can follow the already evaluated level palette.
  auto *diffuse = dynamic_cast<TIntEnumParam *>(
      material.getParams()->getParam("diffuseSource"));
  require(diffuse, "diffuse selector");
  diffuse->setValue(1);
  const auto palette = material.get3DRenderScene(0, nullptr);
  const auto q       = otglb::renderTile(*palette, tile)[16 * 32 + 16];
  require(q.r > .49 && q.b < .001, "level palette color was replaced");
  diffuse->setValue(0);
  stringParam("group")->setValue(L"Missing");
  bool rejected = false;
  try {
    material.get3DRenderScene(0, nullptr);
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected, "misspelled material group silently ignored");
  stringParam("group")->setValue(L"");
  const auto alias = material.getAlias(0, settings);
  write("DiffuseColor=0 0 255\nAmbiance=100\nRoughness=50");
  require(material.getAlias(0, settings) != alias,
          "external material reload did not invalidate cache");
  rejected = false;
  try {
    material.get3DRenderScene(0, nullptr);
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected, "unsupported roughness silently rendered");
  auto *mode = dynamic_cast<TIntEnumParam *>(
      material.getParams()->getParam("unsupportedAttributes"));
  require(mode, "preview selector");
  mode->setValue(1);
  const auto preview = material.get3DRenderScene(0, nullptr);
  require(preview && !preview->warnings.empty(), "preview missing diagnostics");
  // Lights on either side of the material node must contribute before shading.
  TFxP lightingOwner = new ThreePointLightFx;
  auto &lighting =
      *static_cast<ThreePointLightFx *>(lightingOwner.getPointer());
  lighting.getInputPort(0)->setFx(&material);
  const auto after =
      lighting.get3DRenderScene(0, nullptr, nullptr, nullptr, &settings);
  lighting.getInputPort(0)->setFx(column);
  material.getInputPort(0)->setFx(&lighting);
  const auto before =
      material.get3DRenderScene(0, nullptr, nullptr, nullptr, &settings);
  const auto a = otglb::renderTile(*after, tile)[16 * 32 + 16];
  const auto b = otglb::renderTile(*before, tile)[16 * 32 + 16];
  require(std::abs(a.b - b.b) < 1e-6,
          "lighting order changed material shading");
  material.getInputPort(0)->setFx(column);
  int canceled = 1;
  require(!material.get3DRenderScene(0, &canceled), "material cancellation");
  require(!material.get3DRenderScene(1, nullptr),
          "material ignored empty exposure");
  require(level->getAMLevel()->mesh() == original,
          "material modified source geometry");
  scene.getXsheet()->getFxDag()->getInternalFxs()->removeFx(&material);
}
void testShine(ToonzScene &scene, const QString &path) {
  TRasterFxP snapshot = makeAMLevelRenderFx(scene.getXsheet(), 0, 0);
  TFxP owner          = new AMMaterialFx;
  auto &fx            = *static_cast<AMMaterialFx *>(owner.getPointer());
  fx.getInputPort(0)->setFx(snapshot.getPointer());
  auto *enabled =
      dynamic_cast<TBoolParam *>(fx.getParams()->getParam("overrideSpecular"));
  auto *color =
      dynamic_cast<TPixelParam *>(fx.getParams()->getParam("specularColor"));
  auto *intensity = dynamic_cast<TDoubleParam *>(
      fx.getParams()->getParam("specularIntensity"));
  auto *file =
      dynamic_cast<TStringParam *>(fx.getParams()->getParam("materialFile"));
  require(enabled && color && intensity && file, "shine controls missing");
  require(!enabled->getValue(), "shine override changed file defaults");
  enabled->setValue(true);
  require(
      fx.getAMMaterialSummary(0).find(L"OT3D Override") != std::wstring::npos,
      "inspection did not identify specular override");
  set(fx, "specularSize", 10);
  set(fx, "specularIntensity", 25);
  otglb::LightingRig rig;
  rig.lights.push_back(otglb::DiffuseLight{});
  const auto sample = [&](double frame, const otglb::LightingRig &lights) {
    auto rendered = fx.get3DRenderScene(frame, nullptr, &lights);
    require(rendered && bool(rendered->sampleShader),
            "no direct-light material shader");
    return rendered->sampleShader(0, {{1.0 / 3, 1.0 / 3, 1.0 / 3}});
  };
  const auto white = sample(0, rig);
  require(white.b > .5,
          "fileless shine did not add a white highlight to red palette");
  color->setValue(0, TPixel32::Red);
  require(sample(0, rig).b < .001, "specular color not applied");
  color->setValue(0, TPixel32::White);
  set(fx, "specularSize", 0);
  require(sample(0, rig).b < .001, "zero-size shine remained visible");
  set(fx, "specularSize", 10);
  intensity->setValue(1, 0.0);
  require(sample(1, rig).b < .001 && sample(0, rig).b > .5,
          "animated shine intensity not sampled");
  TRenderSettings settings;
  require(fx.getAlias(0, settings) != fx.getAlias(1, settings),
          "animated shine shared cache entry");
  const QString saved = path + ".xml";
  {
    TOStream os{TFilePath(saved)};
    os << owner.getPointer();
  }
  TPersist *persist = nullptr;
  {
    TIStream is{TFilePath(saved)};
    is >> persist;
  }
  TFxP restored = dynamic_cast<TFx *>(persist);
  auto *restoredIntensity =
      restored ? dynamic_cast<TDoubleParam *>(
                     restored->getParams()->getParam("specularIntensity"))
               : nullptr;
  require(restoredIntensity && restoredIntensity->getValue(1) == 0 &&
              restoredIntensity->getValue(0) == 25,
          "keyed shine did not survive persistence");
  // Imported reflective materials still provide direct-light highlights.
  {
    QFile f(path);
    require(f.open(QIODevice::WriteOnly), "write shine material");
    f.write(
        "<MATERIALFILE>\nProductVersion=19.5\n<MATERIALS><MATERIAL><ATTRIBUTE>"
        "\nDiffuseColor=0 0 0\nSpecularColor=255 255 "
        "255\nSpecularSize=10\nSpecularIntensity=25\nReflectivity=60\n</"
        "ATTRIBUTE></MATERIAL></MATERIALS></MATERIALFILE>");
  }
  file->setValue(path.toStdWString());
  enabled->setValue(false);
  const auto imported = sample(0, rig);
  require(imported.r > .5 && imported.g > .5 && imported.b > .5,
          "reflectivity blocked imported shine");
  const auto noLights = sample(0, otglb::LightingRig{});
  require(noLights.r < .001, "unlit material produced a specular highlight");
  otglb::LightingRig ambient;
  ambient.ambient = 1;
  require(sample(0, ambient).r < .001, "ambient light produced shine");
  rig.lights[0].color = {{0, 0, 1}};
  const auto blue     = sample(0, rig);
  require(blue.r < .001 && blue.b > .5,
          "direct light color did not tint shine");
  rig.lights[0].color = {{1, 1, 1}};
  const auto result   = fx.get3DRenderScene(0, nullptr, &rig);
  require(!result->warnings.empty(), "deferred reflection diagnostic missing");
  otglb::ModelTransform camera;
  camera.camera      = true;
  camera.position[0] = 1000;
  const std::vector<otglb::ModelTransform> moves{camera};
  const auto moved = fx.get3DRenderScene(0, nullptr, &rig, &moves);
  require(moved && moved->sampleShader(0, {{1.0 / 3, 1.0 / 3, 1.0 / 3}}).r <
                       imported.r * .8,
          "camera movement did not change specular response");
  enabled->setValue(true);
  set(fx, "specularIntensity", 0);
  require(sample(0, rig).r < .001,
          "zero-intensity override did not suppress imported shine");
}
void testRenderModes(ToonzScene &scene, TXshLevel *level, const QString &path) {
  auto *sheet  = scene.getXsheet();
  auto *column = sheet->getColumn(0)->getFx();
  TRenderSettings normal, dark, flat;
  dark.m_disableDefaultLighting = true;
  flat                          = dark;
  flat.m_flatRender             = true;
  require(!normal.m_flatRender && !normal.m_disableDefaultLighting &&
              !(normal == dark) && !(dark == flat) &&
              normal.toString() != dark.toString() &&
              dark.toString() != flat.toString(),
          "render modes share a cache identity or changed legacy defaults");
  const auto base  = render(scene, normal);
  const auto black = render(scene, dark);
  require(coverage(base) > 0 && brightness(base) > 0 &&
              coverage(black) == coverage(base) && brightness(black) == 0,
          "native model without a rig must render opaque black");
  TFxP owner  = new ThreePointLightFx;
  auto &light = *static_cast<ThreePointLightFx *>(owner.getPointer());
  light.getInputPort(0)->setFx(column);
  sheet->getFxDag()->getInternalFxs()->addFx(&light);
  sheet->getFxDag()->removeFromXsheet(column);
  sheet->getFxDag()->addToXsheet(&light);
  require(brightness(render(scene, dark)) > 0,
          "default lighting switch disabled an explicit native rig");
  set(light, "masterIntensity", 0);
  require(brightness(render(scene, normal)) == 0,
          "connected lighting FX did not affect normal output");
  require(brightness(render(scene, flat)) == brightness(base),
          "flat render failed to bypass the FX graph and lighting");
  // An output disconnection must not hide the source canvas in flat mode.
  sheet->getFxDag()->getOutputFx(0)->getInputPort(0)->setFx(nullptr);
  require(brightness(render(scene, flat)) == brightness(base),
          "flat render depended on a connected output FX");
  sheet->getColumn(0)->setPreviewVisible(false);
  require(coverage(render(scene, flat)) == 0,
          "flat render included a hidden source column");
  sheet->getColumn(0)->setPreviewVisible(true);
  auto *object = sheet->getStageObject(TStageObjectId::ColumnId(0));
  object->getParam(TStageObject::T_X)->setValue(0, 10);
  require(coverage(render(scene, flat)) == 0,
          "flat render discarded stage placement");
  object->getParam(TStageObject::T_X)->setValue(0, 0);
  // Settings must be independent for output and preview and survive reopening.
  scene.getProperties()->getOutputProperties()->setRenderSettings(flat);
  scene.getProperties()->getPreviewProperties()->setRenderSettings(dark);
  {
    TOStream out{TFilePath(path)};
    scene.getProperties()->saveData(out);
  }
  TSceneProperties restored;
  {
    TIStream in{TFilePath(path)};
    restored.loadData(in, false);
  }
  const auto &out     = restored.getOutputProperties()->getRenderSettings();
  const auto &preview = restored.getPreviewProperties()->getRenderSettings();
  require(out.m_flatRender && out.m_disableDefaultLighting &&
              !preview.m_flatRender && preview.m_disableDefaultLighting,
          "output/preview render modes did not survive persistence");
  // A nested xsheet inherits the flat composition policy.
  ToonzScene nestedScene;
  TXshChildLevelP child = new TXshChildLevel;
  child->setScene(&nestedScene);
  require(child->getXsheet()->setCell(0, 0, TXshCell(level, TFrameId(1))),
          "nested model exposure");
  child->getXsheet()->getFxDag()->getOutputFx(0)->getInputPort(0)->setFx(
      nullptr);
  require(nestedScene.getXsheet()->setCell(
              0, 0, TXshCell(child.getPointer(), TFrameId(1))),
          "nested exposure");
  require(brightness(render(nestedScene, flat)) > 0,
          "flat policy was lost in a nested xsheet");
  ToonzScene imageScene;
  TFxP image = TFx::create("colorCardFx");
  require(image, "image source unavailable");
  auto *color =
      dynamic_cast<TPixelParam *>(image->getParams()->getParam("color"));
  require(color, "image color parameter unavailable");
  color->setValue(0, TPixel32::Red);
  TXshZeraryFxColumnP imageColumn = new TXshZeraryFxColumn(1);
  imageColumn->getZeraryColumnFx()->setZeraryFx(image.getPointer());
  imageScene.getXsheet()->insertColumn(0, imageColumn.getPointer());
  const auto imageBase = brightness(render(imageScene, normal));
  require(imageBase > 0 && brightness(render(imageScene, dark)) == imageBase &&
              brightness(render(imageScene, flat)) == imageBase,
          "render modes changed unlit image source colors");
  scene.getProperties()->getOutputProperties()->setRenderSettings(normal);
}
}  // namespace
int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  TThread::init();
  TRenderer::initialize();
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
    testMaterials(scene, level, column, temp.filePath("surface.mat"));
    testInkLine(temp.filePath("ink-lines.mdl"));
    testVectorInkLine(temp);
    testShine(scene, temp.filePath("shine.mat"));
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
    auto *lightMode =
        dynamic_cast<TIntEnumParam *>(light.getParams()->getParam("lightMode"));
    require(lightMode && lightMode->getValue() == 0,
            "legacy lighting must default to directional");
    lightMode->setValue(1);
    set(light, "ambient", 0);
    set(light, "fillIntensity", 0);
    set(light, "rimIntensity", 0);
    set(light, "keyAzimuth", 0);
    set(light, "keyElevation", 0);
    set(light, "rigRadius", 100);
    set(light, "rigPositionZ", 100);
    const auto front =
        light.get3DRenderScene(0, nullptr, nullptr, nullptr, &info);
    set(light, "rigPositionZ", -200);
    const auto rear =
        light.get3DRenderScene(0, nullptr, nullptr, nullptr, &info);
    require(front && rear && !front->triangles.empty() &&
                front->triangles.size() == rear->triangles.size() &&
                front->triangles[0].color[0] > rear->triangles[0].color[0] &&
                front->bounds == rear->bounds,
            "moving the rig did not shade the native model independently");
    lightMode->setValue(0);
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
    // A mixed native/GLB group retains both source contracts for shared light.
    const auto glbPath = temp.filePath("model.glb");
    {
      const auto bytes = triangleGlb();
      QFile f(glbPath);
      require(f.open(QIODevice::WriteOnly) &&
                  f.write(reinterpret_cast<const char *>(bytes.data()),
                          bytes.size()) == qint64(bytes.size()),
              "write mixed-group GLB fixture");
    }
    TFxP glbOwner = new GlbModelFx;
    auto &glb     = *static_cast<GlbModelFx *>(glbOwner.getPointer());
    auto *file =
        dynamic_cast<TStringParam *>(glb.getParams()->getParam("modelFile"));
    require(file, "mixed-group GLB file parameter");
    file->setValue(glbPath.toStdWString());
    TFxP mergeOwner = new Merge3DFx;
    auto &merge     = *static_cast<Merge3DFx *>(mergeOwner.getPointer());
    merge.getInputPort(0)->setFx(column);
    merge.getInputPort(1)->setFx(&glb);
    light.getInputPort(0)->setFx(&merge);
    set(light, "masterIntensity", 0);
    const auto darkGroup =
        light.get3DRenderScene(0, nullptr, nullptr, nullptr, &info);
    require(darkGroup && darkGroup->layers.size() == 2,
            "mixed native/GLB sources were not retained");
    for (const auto &layer : darkGroup->layers)
      require(
          layer.scene->triangles[0].color == std::array<float, 3>{{0, 0, 0}},
          "shared light failed to shade a mixed-group branch");
    set(light, "masterIntensity", 100);
    set(light, "ambient", 100);
    const auto litGroup =
        light.get3DRenderScene(0, nullptr, nullptr, nullptr, &info);
    for (const auto &layer : litGroup->layers)
      require(layer.scene->triangles[0].color[0] > 0,
              "shared light failed to illuminate a mixed-group branch");
    light.doCompute(tile, 0, info);
    require(coverage(raster) > 0, "mixed-group raster output is empty");
    light.getInputPort(0)->setFx(&transform);
    int canceled = 1;
    require(!transform.get3DRenderScene(0, &canceled),
            "cancellation not honored");
    require(!transform.get3DRenderScene(1, nullptr),
            "empty exposure rendered a stale model");
    testRenderModes(scene, level, temp.filePath("render-settings.xml"));
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

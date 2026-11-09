#pragma once
#include "ammodel.h"
#include "ammaterial.h"
#include "amink.h"
#include "glbrenderer.h"
#include "tpixel.h"
#include "trasterfx.h"
#include "toonz/tstageobjectid.h"
#include <memory>

#undef DVAPI
#ifdef TOONZLIB_EXPORTS
#define DVAPI DV_EXPORT_API
#else
#define DVAPI DV_IMPORT_API
#endif
class TXsheet;

// Model-space editor and rasterizer share the column and camera transforms.
struct AMModelCamera {
  otam::Matrix pose         = otam::translation(0, 0, 1000);
  otam::Matrix imageToStage = otam::identity();
};
DVAPI AMModelCamera amModelCamera(TXsheet *xsheet, double frame,
                                  bool preview = false);
DVAPI otam::Matrix amModelPlacement(TXsheet *xsheet, int column, double frame);

// Shared by the interactive Viewer and the final-render tree. A snapshot owns
// all geometry and can safely outlive a source reload or scene changes.
DVAPI std::shared_ptr<const otam::Mesh> prepareAMLevelGeometry(
    TXsheet *xsheet, int column, double frame, bool projectToCamera,
    bool previewCamera = false);
DVAPI TRasterFx *makeAMLevelRenderFx(
    std::shared_ptr<const otam::Mesh> geometry);

// Interactive columns and immutable render-tree snapshots share this contract.
DVAPI bool isAMModelColumn(TFx *fx);
DVAPI bool isNativeModelColumn(TFx *fx);
DVAPI std::shared_ptr<const otam::Mesh> prepareNativeModelGeometry(
    TXsheet *xsheet, int column, double frame, bool projectToCamera);
// Convert a camera-image point to a new model's position in table space.
DVAPI otam::Vec3 nativeModelDropPosition(TXsheet *xsheet, int frame,
                                         const TPointD &imagePoint, bool snap,
                                         double halfExtent);
DVAPI TRasterFx *makeNativeModelRenderFx(TXsheet *xsheet, int column,
                                         double frame, bool previewCamera);
DVAPI TRasterFx *makeAMLevelRenderFx(TXsheet *xsheet, int column, double frame,
                                     bool previewCamera = false);
DVAPI bool setAMLevelColorFilter(TRasterFx *fx, const TPixel32 &color);
class TLevelColumnFx;
DVAPI std::shared_ptr<const otglb::RenderScene>
prepareNativeModelColumnRenderScene(
    TLevelColumnFx *column, double frame, const int *canceled,
    const otglb::LightingRig *lighting                   = nullptr,
    const std::vector<otglb::ModelTransform> *transforms = nullptr,
    const TRenderSettings *settings                      = nullptr,
    const otam::MaterialBindings *materials              = nullptr,
    const otam::InkBindings *ink                         = nullptr);

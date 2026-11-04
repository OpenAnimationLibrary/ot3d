#pragma once
#include "ammodel.h"
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

// Shared by the interactive Viewer and the final-render tree. A snapshot owns
// all geometry and can safely outlive a source reload or scene changes.
DVAPI std::shared_ptr<const otam::Mesh> prepareAMLevelGeometry(
    TXsheet *xsheet, int column, double frame, bool projectToCamera,
    bool previewCamera = false);
DVAPI TRasterFx *makeAMLevelRenderFx(
    std::shared_ptr<const otam::Mesh> geometry);

// Interactive columns and immutable render-tree snapshots share this contract.
DVAPI bool isAMModelColumn(TFx *fx);
DVAPI TRasterFx *makeAMLevelRenderFx(TXsheet *xsheet, int column, double frame,
                                     bool previewCamera = false);
DVAPI bool setAMLevelColorFilter(TRasterFx *fx, const TPixel32 &color);
class TLevelColumnFx;
DVAPI std::shared_ptr<const otglb::RenderScene> prepareAMColumnRenderScene(
    TLevelColumnFx *column, double frame, const int *canceled,
    const otglb::LightingRig *lighting                   = nullptr,
    const std::vector<otglb::ModelTransform> *transforms = nullptr,
    const TRenderSettings *settings                      = nullptr);

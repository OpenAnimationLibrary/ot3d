#pragma once

#include "glbrenderer.h"
#include "glbviewer.h"
#include "trasterfx.h"
#include "tfxconnection.h"
#include "toonz/tcolumnfx.h"
#include "toonz/t3drendersource.h"
#include "toonz/amlevelrender.h"

#include <memory>

// The schematic connects a zerary column, whereas the render tree connects the
// underlying FX. Keep the original column connection for ownership, exposure
// and scene persistence; unwrap it only when checking/accessing the 3D source.
class T3DSourcePort final : public TRasterFxPort,
                            public TFxPortConnectionPolicy,
                            private T3DRenderSource {
  static T3DRenderSource *resolve(TFx *fx) {
    if (auto *column = dynamic_cast<TZeraryColumnFx *>(fx))
      fx = column->getZeraryFx();
    return dynamic_cast<T3DRenderSource *>(fx);
  }

  std::shared_ptr<const otglb::RenderScene> get3DRenderScene(
      double frame, const int *canceled, const otglb::LightingRig *lighting,
      const std::vector<otglb::ModelTransform> *transforms,
      const TRenderSettings *settings) const override {
    return prepareNativeModelColumnRenderScene(
        dynamic_cast<TLevelColumnFx *>(getFx()), frame, canceled, lighting,
        transforms, settings);
  }

public:
  bool canConnect(TFx *fx) const override {
    return !fx || resolve(fx) || isNativeModelColumn(fx);
  }
  void setFx(TFx *fx) override {
    // Scene restoration can connect a column before its cells are populated.
    // Interactive connection checks use canConnect(), not this deferred case.
    if (fx && !resolve(fx) && !dynamic_cast<TLevelColumnFx *>(fx))
      throw TException("Fx: 3D source port requires a compatible 3D FX");
    TRasterFxPort::setFx(fx);
  }

  T3DRenderSource *source() const {
    if (auto *source = resolve(getFx())) return source;
    // No mutable frame cache here: each render request returns an owned
    // snapshot.
    return dynamic_cast<TLevelColumnFx *>(getFx())
               ? const_cast<T3DSourcePort *>(this)
               : nullptr;
  }
};

// Interactive geometry is a separate contract from the projected FX raster.
// Returned snapshots own their data and can outlive cache invalidation.
class T3DViewerSource {
public:
  virtual ~T3DViewerSource() = default;
  virtual std::shared_ptr<const otglb::ViewerGeometry> get3DViewerGeometry(
      double frame) const = 0;
};

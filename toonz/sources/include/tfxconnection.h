#pragma once
#include "tfx.h"
#include "trasterfx.h"

// Optional policy separate from TFxPort: do not change existing plugin vtables.
class TFxPortConnectionPolicy {
public:
  virtual ~TFxPortConnectionPolicy()     = default;
  virtual bool canConnect(TFx *fx) const = 0;
};

inline bool canConnectFxPort(const TFxPort *port, TFx *fx) {
  if (!port) return false;
  const auto *policy = dynamic_cast<const TFxPortConnectionPolicy *>(port);
  return !policy || policy->canConnect(fx);
}

// Dynamic groups normally use raster ports. Typed groups must retain their
// connection policy when the schematic grows, or a scene is cloned/loaded.
class TFxDynamicPortFactory {
public:
  virtual ~TFxDynamicPortFactory()                    = default;
  virtual TFxPort *createDynamicPort(int group) const = 0;
};

inline TFxPort *createDynamicFxPort(const TFx *fx, int group) {
  const auto *factory = dynamic_cast<const TFxDynamicPortFactory *>(fx);
  return factory ? factory->createDynamicPort(group) : new TRasterFxPort;
}

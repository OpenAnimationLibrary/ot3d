#pragma once
#include "tfx.h"

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

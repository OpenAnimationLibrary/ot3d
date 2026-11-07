#pragma once

#include <string>
#include <vector>

// Read-only discovery for the material editor. Loading and render-time
// evaluation remain with the FX, rather than being duplicated in UI code.
class TFxAMMaterialSource {
public:
  virtual ~TFxAMMaterialSource() = default;
  virtual std::vector<std::wstring> getAMMaterialGroups(double frame) const = 0;
  virtual std::wstring getAMMaterialSummary(double frame) const             = 0;
};

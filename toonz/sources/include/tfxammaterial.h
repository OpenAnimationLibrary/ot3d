#pragma once

#include <string>
#include <vector>

// Read-only group discovery shared by A:M assignment controls.
class TFxAMGroupSource {
public:
  virtual ~TFxAMGroupSource()                                       = default;
  virtual std::vector<std::wstring> getAMGroups(double frame) const = 0;
};

class TFxAMMaterialSource : public TFxAMGroupSource {
public:
  virtual std::wstring getAMMaterialSummary(double frame) const = 0;
};

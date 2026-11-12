#pragma once

#include <cstdint>
#include <utility>
#include <vector>

class TPalette;
class TXshLevelColumn;

void showPaletteStyleInFunctionEditor(TPalette *palette, int styleId);
void showAMActionInFunctionEditor(TXshLevelColumn *column,
                                  const std::vector<std::int64_t> &pointIds);

void showVectorActionInFunctionEditor(TXshLevelColumn *column,
                                      std::pair<int, int> point);

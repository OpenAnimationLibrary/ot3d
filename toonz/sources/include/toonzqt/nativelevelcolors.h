#pragma once

#include <QColor>

// Keep native A:M and GLB assets consistent in the Xsheet and schematics.
namespace NativeLevelColors {
inline QColor background(bool selected = false) {
  return selected ? QColor(40, 70, 112) : QColor(16, 32, 64);
}
inline QColor border() { return QColor(8, 16, 32); }
inline QColor text() { return QColor(255, 255, 255); }
}  // namespace NativeLevelColors

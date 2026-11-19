#pragma once
#include "ammodel.h"
#include <optional>

namespace otam {
// Extract a proper orthonormal basis, excluding scale and shear. Singular
// transforms have no usable orientation. Reflection remains in the source.
std::optional<Matrix> orientation(const Matrix &placement);
// Aim local +X at destination, with world +Z as up (+Y at the pole).
std::optional<Matrix> aimOrientation(const Vec3 &source,
                                     const Vec3 &destination, bool spatial);
// Rotate about the object's pivot. Preserve translation, scale and shear;
// interpolate along the shortest quaternion arc.
Matrix orientPlacement(const Matrix &placement, const Matrix &desired,
                       const Vec3 &pivot, double influence);
}  // namespace otam

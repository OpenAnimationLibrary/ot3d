#include "amorientation.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace otam;
namespace {
void check(bool v, const char *s) {
  if (!v) throw std::runtime_error(s);
}
void near(double a, double b) {
  check(std::abs(a - b) < 1e-8, "orientation mismatch");
}
void equal(const Matrix &a, const Matrix &b) {
  for (int i = 0; i < 16; ++i) near(a[i], b[i]);
}
}  // namespace
int main() {
  try {
    const auto goal = rotation(33, -74, 129);
    const auto source =
        multiply(translation(10, 20, 30),
                 multiply(rotation(-12, 35, 8), scaling(2, 3, 4)));
    const Vec3 pivot{{10, 20, 30}};
    auto result = orientPlacement(source, goal, pivot, 1);
    equal(*orientation(result), goal);
    near(result[3], 10);
    near(result[7], 20);
    near(result[11], 30);
    for (int i = 0; i < 3; ++i) {
      double square = 0;
      for (int k = 0; k < 3; ++k)
        square += result[4 * k + i] * result[4 * k + i];
      near(square, double((i + 2) * (i + 2)));
    }
    // Parent-scaled/sheared geometry keeps its shape under orientation changes.
    auto shear   = identity();
    shear[1]     = .4;
    shear[6]     = -.3;
    auto shaped  = multiply(source, shear);
    auto changed = orientPlacement(shaped, goal, pivot, .73);
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j) {
        double before = 0, after = 0;
        for (int k = 0; k < 3; ++k) {
          before += shaped[4 * k + i] * shaped[4 * k + j];
          after += changed[4 * k + i] * changed[4 * k + j];
        }
        near(before, after);
      }
    // Rotation goes through 180, not through zero, across the wrap boundary.
    auto halfway = orientPlacement(rotation(0, 0, 170), rotation(0, 0, -170),
                                   {{0, 0, 0}}, .5);
    equal(halfway, rotation(0, 0, 180));
    equal(orientPlacement(source, goal, pivot, 0), source);
    auto singular = scaling(0, 1, 1);
    check(!orientation(singular), "collapsed axis acquired an orientation");
    equal(orientPlacement(singular, goal, pivot, 1), singular);
    auto aim = aimOrientation({{0, 0, 0}}, {{1, 2, 3}}, true);
    check(bool(aim), "spatial aim missing");
    near((*aim)[0], 1 / std::sqrt(14.));
    near((*aim)[4], 2 / std::sqrt(14.));
    near((*aim)[8], 3 / std::sqrt(14.));
    check(bool(aimOrientation({{0, 0, 0}}, {{0, 0, 3}}, true)),
          "pole aim failed");
    check(!aimOrientation({{1, 2, 3}}, {{1, 2, 3}}, true),
          "coincident aim moved");
    auto flat = aimOrientation({{0, 0, 0}}, {{0, 2, 9}}, false);
    equal(*flat, rotation(0, 0, 90));
    // An offset-center object rotates around its pivot, not its image origin.
    const Vec3 localPivot{{2, 3, 4}},
        worldPivot    = transform(source, localPivot);
    const auto offset = orientPlacement(source, goal, worldPivot, 1);
    const auto after  = transform(offset, localPivot);
    for (int k = 0; k < 3; ++k) near(after[k], worldPivot[k]);
    // Cover trace-negative quaternion extraction and reflected source scale.
    for (int x = -180; x <= 180; x += 60)
      for (int y = -180; y <= 180; y += 60)
        for (int z = -180; z <= 180; z += 60) {
          const auto r = rotation(x, y, z);
          equal(*orientation(orientPlacement(source, r, pivot, 1)), r);
        }
    const auto mirror   = multiply(source, scaling(-1, 1, 1));
    const auto mirrored = orientPlacement(mirror, goal, pivot, 1);
    equal(*orientation(mirrored), goal);
    std::cout << "Orientation behavior passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

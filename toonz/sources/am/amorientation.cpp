#include "amorientation.h"
#include <algorithm>
#include <cmath>

namespace otam {
namespace {
double dot(const Vec3 &a, const Vec3 &b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vec3 cross(const Vec3 &a, const Vec3 &b) {
  return {{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
           a[0] * b[1] - a[1] * b[0]}};
}
bool normalize(Vec3 &v) {
  const double n = std::sqrt(dot(v, v));
  if (!std::isfinite(n) || n < 1e-10) return false;
  for (auto &x : v) x /= n;
  return true;
}
Matrix basis(const Vec3 &x, const Vec3 &y, const Vec3 &z) {
  return {{x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0, 0, 0,
           0, 1}};
}
Quaternion quaternion(const Matrix &m) {
  Quaternion q{};
  const double trace = m[0] + m[5] + m[10];
  if (trace > 0) {
    const double s = 2 * std::sqrt(trace + 1);
    q = {{(m[9] - m[6]) / s, (m[2] - m[8]) / s, (m[4] - m[1]) / s, s / 4}};
  } else {
    int i = m[5] > m[0] ? 1 : 0;
    if (m[10] > m[5 * i]) i = 2;
    const int j = (i + 1) % 3, k = (i + 2) % 3;
    const double s =
        2 * std::sqrt(std::max(0.0, 1 + m[5 * i] - m[5 * j] - m[5 * k]));
    q[i] = s / 4;
    q[j] = (m[4 * j + i] + m[4 * i + j]) / s;
    q[k] = (m[4 * k + i] + m[4 * i + k]) / s;
    q[3] = (m[4 * k + j] - m[4 * j + k]) / s;
  }
  return q;
}
}  // namespace
std::optional<Matrix> orientation(const Matrix &m) {
  for (double v : m)
    if (!std::isfinite(v)) return {};
  Vec3 x{{m[0], m[4], m[8]}}, y{{m[1], m[5], m[9]}};
  if (!normalize(x)) return {};
  const double d = dot(x, y);
  for (int i = 0; i < 3; ++i) y[i] -= d * x[i];
  if (!normalize(y)) return {};
  auto z = cross(x, y);
  // A collapsed depth axis cannot define a spatial frame either.
  if (std::abs(dot(z, {{m[2], m[6], m[10]}})) < 1e-10) return {};
  return basis(x, y, z);
}
std::optional<Matrix> aimOrientation(const Vec3 &source,
                                     const Vec3 &destination, bool spatial) {
  Vec3 x{};
  for (int i = 0; i < 3; ++i) x[i] = destination[i] - source[i];
  if (!spatial) x[2] = 0;
  if (!normalize(x)) return {};
  Vec3 up{{0, 0, 1}};
  if (std::abs(dot(x, up)) > .9999) up = {{0, 1, 0}};
  auto y = cross(up, x);
  if (!normalize(y)) return {};
  return basis(x, y, cross(x, y));
}
Matrix orientPlacement(const Matrix &m, const Matrix &desired,
                       const Vec3 &pivot, double influence) {
  const auto current = orientation(m), goal = orientation(desired);
  if (!current || !goal || !std::isfinite(influence) || influence <= 0)
    return m;
  auto a = quaternion(*current), b = quaternion(*goal);
  double d = 0;
  for (int i = 0; i < 4; ++i) d += a[i] * b[i];
  if (d < 0) {
    for (auto &v : b) v = -v;
    d = -d;
  }
  const double t = std::min(1.0, influence);
  double wa = 1 - t, wb = t;
  if (d < .9995) {
    const double theta = std::acos(std::clamp(d, 0.0, 1.0)),
                 s     = std::sin(theta);
    wa                 = std::sin((1 - t) * theta) / s;
    wb                 = std::sin(t * theta) / s;
  }
  Quaternion q;
  for (int i = 0; i < 4; ++i) q[i] = wa * a[i] + wb * b[i];
  const auto delta = multiply(rotation(q), inverse(*current));
  return multiply(
      translation(pivot[0], pivot[1], pivot[2]),
      multiply(delta,
               multiply(translation(-pivot[0], -pivot[1], -pivot[2]), m)));
}
}  // namespace otam

#include "ammodel.h"
#include "amfixture.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <functional>

namespace {
void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
void near(double a, double b) {
  check(std::abs(a - b) < 1e-8, "numeric mismatch");
}
void rejects(const std::function<void()> &f) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, "malformed input was accepted");
}
const auto &source = amtest::source;
std::string replace(std::string s, const std::string &a, const std::string &b) {
  const auto pos = s.find(a);
  check(pos != std::string::npos, "test replacement missing");
  s.replace(pos, a.size(), b);
  return s;
}
}  // namespace
int main() {
  try {
    auto d = otam::parse(source);
    check(d.source == source, "source was modified");
    check(d.models.size() == 1 && d.models[0].points.size() == 8,
          "CP occurrences lost");
    check(d.models[0].points[2].head == 1, "attachment resolution");
    auto rich =
        otam::parse(replace(source, "24 1 2 4 6", "24 1 2 4 6 0 0 3 1 0"));
    check(rich.models[0].patches[0].points.size() == 4,
          "normal indices treated as CPs");
    check(rich.models[0].patches[0].sourceRecord.find("0 0 3 1 0") !=
              std::string::npos,
          "rich patch metadata lost");
    auto mesh = otam::previewMesh(d, 0, 2);
    check(mesh.triangles.size() == 8, "quad tessellation");
    near(mesh.triangles[0].color[0], 1);
    near(mesh.triangles[0].color[1], 0);
    for (const auto &t : mesh.triangles)
      for (const auto &v : t.vertices) near(v[2], 0);
    // Coincidence must not cause an implicit topological attachment.
    auto duplicate = otam::parse(
        replace(source, "262145 1 3 2 . .", "262145 0 3 1 -1 0 . ."));
    check(duplicate.models[0].points[2].head == 2, "coincident CPs welded");
    check(!mesh.warnings.empty(), "approximation not diagnosed");
    rejects(
        [&] { otam::parse(replace(source, "262145 1 3 2", "262145 1 3 9")); });
    rejects([&] {
      otam::parse(
          replace(source, "262145 0 1 -1 -1 0 . .", "262145 1 1 8 . ."));
    });
    rejects([&] { otam::parse(replace(source, "262145 0 2", "262145 0 1")); });
    rejects([&] { otam::parse(replace(source, "24 1 2 4 6", "24 1 2 4 99")); });
    rejects([&] { otam::parse(replace(source, "-1 -1 0", "nan -1 0")); });
    rejects([&] { otam::parse("<!DOCTYPE MODELFILE>" + source); });
    rejects([&] { otam::parse(source.substr(0, source.size() - 12)); });
    rejects([&] { otam::parse(replace(source, "</MESH>", "</BOGUS>")); });
    rejects([&] { otam::parse(source, otam::Limits{16, 1000, 1000, 64}); });
    rejects([&] { otam::parse(source, otam::Limits{100000, 1000, 1000, 2}); });
    rejects([&] {
      otam::previewMesh(d, 0, 2, otam::Limits{100000, 1000, 1, 64});
    });
    auto p = otam::transform(otam::rotation(90, 0, 0), {{0, 1, 0}});
    near(p[0], 0);
    near(p[1], 0);
    near(p[2], 1);
    p = otam::transform(otam::rotation(0, 90, 0), {{1, 0, 0}});
    near(p[0], 0);
    near(p[2], -1);
    p = otam::transform(otam::rotation(0, 0, 90), {{1, 0, 0}});
    near(p[0], 0);
    near(p[1], 1);
    const auto a =
        otam::multiply(otam::translation(4, 5, 6), otam::scaling(2, 3, 4));
    p = otam::transform(a, {{1, 1, 1}});
    near(p[0], 6);
    near(p[1], 8);
    near(p[2], 10);
    auto first  = otam::transformed(mesh, a);
    auto second = otam::transformed(mesh, otam::translation(-10, 0, 0));
    check(first.triangles[0].vertices != second.triangles[0].vertices,
          "instances not independent");
    near(mesh.triangles[0].vertices[0][0], -1);
    auto projected = otam::projected(
        otam::transformed(mesh, otam::translation(0, 0, 500)), 0, 0, 0);
    near(projected.triangles[0].vertices[0][0], -2);
    auto clipped = otam::projected(
        otam::transformed(mesh, otam::translation(0, 0, 1100)), 0, 0, 0);
    check(clipped.triangles.empty(), "near plane failed");
    auto crossing = mesh;
    crossing.triangles.resize(1);
    crossing.triangles[0].vertices[0][2] = 1100;
    check(otam::projected(crossing, 0, 0, 0).triangles.size() == 2,
          "near clipping lost visible part");
    auto pixels = otam::rasterize(
        otam::transformed(mesh, otam::multiply(otam::translation(4, 4, 0),
                                               otam::scaling(3, 3, 1))),
        8, 8);
    check(pixels[4 * 8 + 4][3] == 1 && pixels[0][3] == 0, "raster coverage");
    auto rotated = otam::transformed(mesh, otam::rotation(90, 0, 0));
    pixels       = otam::rasterize(rotated, 8, 8);
    for (const auto &px : pixels) near(px[3], 0);
    auto mirrored = otam::transform(otam::scaling(-2, 0, 3), {{1, 2, 3}});
    near(mirrored[0], -2);
    near(mirrored[1], 0);
    near(mirrored[2], 9);
    rejects([&] { otam::rasterize(mesh, -1, 20); });
    rejects([&] { otam::rasterize(mesh, 100000, 100000); });
    std::cout << "A:M parser, preservation, attachment, limits, transforms, "
                 "clipping and raster tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

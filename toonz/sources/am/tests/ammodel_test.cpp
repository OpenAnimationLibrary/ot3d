#include "ammodel.h"
#include "amink.h"
#include "amfixture.h"
#include <algorithm>
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
void testInk(const otam::Mesh &filled) {
  const auto mesh = otam::previewMesh(otam::parse(amtest::splineSource));
  check(mesh.triangles.empty() && mesh.splines.size() == 2,
        "spline-only model lost its geometry");
  const auto identity = otam::identity();
  otam::InkStyle ink;
  ink.unfilledOnly = true;
  check(otam::strokeSplines(filled, {ink}, nullptr, identity).empty(),
        "unfilled mode included filled patch boundaries");
  const auto sharedEndpoints = otam::previewMesh(
      otam::parse(replace(source, "<PATCHES>",
                          "<SPLINE>\n262145 1 9 1 . .\n262145 1 10 2 . .\n"
                          "</SPLINE>\n<PATCHES>")));
  check(!otam::strokeSplines(sharedEndpoints, {ink}, nullptr, identity).empty(),
        "unfilled mode lost a spline sharing a filled edge's endpoints");
  const auto whole = otam::strokeSplines(mesh, {ink}, nullptr, identity);
  ink.group        = "Lower";
  const auto lower = otam::strokeSplines(mesh, {ink}, nullptr, identity);
  check(!lower.empty() && whole.size() == lower.size() * 2,
        "group ink did not filter the authored spline segments");
  auto duplicate = mesh;
  duplicate.splineGroups.push_back("Lower");
  duplicate.splines.back().curves.front().groups.push_back(1);
  check(otam::strokeSplines(duplicate, {ink}, nullptr, identity).size() ==
            whole.size(),
        "group ink missed a second group with the assigned name");
  ink.group = "Missing";
  check(otam::strokeSplines(mesh, {ink}, nullptr, identity).empty(),
        "unknown group fell back to the whole model");
  ink.group          = "Lower";
  ink.middle         = 0.3;
  ink.widths         = {{2, 8, 4}};
  ink.colors[1]      = {{1, 0, 0, 1}};
  ink.fadeIn         = 0.2;
  ink.fadeOut        = 0.1;
  const auto profile = otam::strokeSplines(mesh, {ink}, nullptr, identity);
  near(profile.front().colors[0][3], 0);
  near(profile.back().colors[2][3], 0);
  bool hitMiddle = false;
  for (const auto &triangle : profile)
    for (int i = 0; i < 3; ++i)
      if (triangle.colors[i][0] == 1) {
        near(triangle.vertices[i][0], -0.4);
        near(std::abs(triangle.vertices[i][1] + 0.5), 4);
        hitMiddle = true;
      }
  check(hitMiddle, "ink profile skipped the requested middle position");
  ink.roughness     = 1;
  const auto rough  = otam::strokeSplines(mesh, {ink}, nullptr, identity);
  const auto repeat = otam::strokeSplines(mesh, {ink}, nullptr, identity);
  check(rough[2].vertices != profile[2].vertices &&
            rough[2].vertices == repeat[2].vertices,
        "ink roughness must change the edge deterministically");
  auto disabled   = ink;
  disabled.widths = {{0, 0, 0}};
  check(otam::strokeSplines(mesh, {ink, disabled}, nullptr, identity).empty(),
        "downstream ink did not replace the group style");

  const auto placed    = otam::transformed(mesh, otam::translation(0, 0, -2));
  const auto nearPlane = otam::rotation(0, 75, 0);
  check(!otam::strokeSplines(placed, {ink}, &nearPlane, identity).empty(),
        "ink crossing the near plane disappeared");
  const auto behind = otam::translation(0, 0, 20);
  check(otam::strokeSplines(placed, {ink}, &behind, identity).empty(),
        "ink behind the camera remained visible");
  const int canceled = 1;
  check(otam::strokeSplines(mesh, {ink}, nullptr, identity, &canceled).empty(),
        "ink ignored cancellation");
}
void testSplineProfile() {
  otam::Mesh mesh;
  mesh.splineGroups     = {"Middle"};
  auto &spline          = mesh.splines.emplace_back();
  const double points[] = {0, 1, 4, 10};
  for (int i = 0; i < 3; ++i) {
    const double start = points[i], end = points[i + 1];
    auto &curve = spline.curves.emplace_back();
    for (int j = 0; j < 4; ++j)
      curve.points[j] = {{start + j * (end - start) / 3, 0, 0}};
  }
  spline.curves[1].groups = {0};
  otam::InkStyle ink;
  ink.widths     = {{4, 4, 4}};
  auto &profile  = ink.profile.emplace();
  profile.width  = {{{{0, 1}, {1.0 / 6, 2}, {1.0 / 3, 2}, {0.5, 1}}},
                    {{{0.5, 1}, {2.0 / 3, 1}, {5.0 / 6, 0}, {1, 0}}}};
  profile.colors = {
      {0, {{1, 0, 0, 1}}}, {0.5, {{0, 1, 0, 0.25f}}}, {1, {{0, 0, 1, 0}}}};
  const auto identity   = otam::identity();
  const auto whole      = otam::strokeSplines(mesh, {ink}, nullptr, identity);
  const auto checkPoint = [](const auto &stroke, double x, double halfWidth,
                             double green, double alpha) {
    for (const auto &tri : stroke)
      for (int i = 0; i < 3; ++i)
        if (std::abs(tri.vertices[i][0] - x) < 1e-8) {
          check(std::abs(std::abs(tri.vertices[i][1]) - halfWidth) < 1e-6,
                "ink width profile value");
          near(tri.colors[i][1], green);
          near(tri.colors[i][3], alpha);
          return;
        }
    throw std::runtime_error("ink profile sample missing");
  };
  checkPoint(whole, 5, 2, 0.25, 0.25);
  checkPoint(whole, 2.5, 3.5, 0.3125, 0.625);
  checkPoint(whole, 10, 0, 0, 0);

  auto vectorStyle = ink;
  vectorStyle.profile.reset();
  vectorStyle.colors[1]   = {{0, 1, 0, 1}};
  const auto vectorStroke = otam::strokeSplines(mesh, {vectorStyle}, nullptr,
                                                identity, nullptr, true);
  checkPoint(vectorStroke, 5, 2, 1, 1);

  ink.group = "Middle";
  const auto sectionOfWhole =
      otam::strokeSplines(mesh, {ink}, nullptr, identity);
  checkPoint(sectionOfWhole, 2.5, 3.5, 0.3125, 0.625);
  profile.perSection = true;
  const auto section = otam::strokeSplines(mesh, {ink}, nullptr, identity);
  checkPoint(section, 1, 2, 0, 1);
  checkPoint(section, 2.5, 2, 0.25, 0.25);
  checkPoint(section, 4, 0, 0, 0);

  const auto view =
      otam::multiply(otam::translation(0, 0, -50), otam::rotation(0, 45, 0));
  const auto p         = otam::transform(view, {{2.5, 0, 0}});
  const auto projected = otam::strokeSplines(mesh, {ink}, &view, identity);
  checkPoint(projected, -1000 * p[0] / p[2], 2, 0.25, 0.25);

  const std::array<otam::Vec3, 4> corners{
      {{{0, 0, 0}}, {{2, 0, 0}}, {{2, 2, 0}}, {{0, 2, 0}}}};
  spline.closed = true;
  spline.curves.resize(corners.size());
  for (std::size_t i = 0; i < corners.size(); ++i) {
    auto &curve   = spline.curves[i];
    const auto &a = corners[i];
    const auto &b = corners[(i + 1) % corners.size()];
    curve.groups  = (i == 0 || i == 3) ? std::vector<std::size_t>{0}
                                       : std::vector<std::size_t>{};
    for (int j = 0; j < 4; ++j)
      curve.points[j] = {
          {a[0] + j * (b[0] - a[0]) / 3, a[1] + j * (b[1] - a[1]) / 3, 0}};
  }
  const auto acrossSeam = otam::strokeSplines(mesh, {ink}, nullptr, identity);
  check(std::any_of(acrossSeam.begin(), acrossSeam.end(),
                    [](const auto &tri) {
                      for (int i = 0; i < 3; ++i)
                        if (std::abs(tri.vertices[i][0] + 2) < 1e-8 &&
                            std::abs(tri.vertices[i][1] + 2) < 1e-8 &&
                            tri.colors[i][1] == 0.25f)
                          return true;
                      return false;
                    }),
        "closed ink section restarted at its spline seam");
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
    testInk(mesh);
    testSplineProfile();
    near(mesh.triangles[0].color[0], 1);
    near(mesh.triangles[0].color[1], 0);
    for (const auto &t : mesh.triangles)
      for (const auto &v : t.vertices) near(v[2], 0);
    check(mesh.triangles[0].hasNormals, "preview patch lacks smooth normals");
    for (const auto &t : mesh.triangles)
      for (const auto &n : t.normals) {
        near(n[0], 0);
        near(n[1], 0);
        near(n[2], 1);
      }
    // CP 3 is attached to head 2. Its outgoing bias shapes the 2–4
    // boundary without moving the attachment or changing spline 1–2.
    auto biased          = d.models.front();
    auto &occurrence     = biased.points[2];
    const auto base      = otam::splineSegment(biased, biased.splines[1], 0);
    const auto untouched = otam::splineSegment(biased, biased.splines[0], 0);
    const otam::Vec3 goal{{1.35, -0.4, 0.5}};
    const auto bias = base.outgoing.biasAt(goal, false);
    std::copy(bias.begin(), bias.end(), occurrence.bias.begin() + 3);
    const auto shaped = otam::splineSegment(biased, biased.splines[1], 0);
    for (int k = 0; k < 3; ++k) near(shaped.outgoing.point()[k], goal[k]);
    check(shaped.outgoing.origin == biased.points[1].position &&
              shaped.incoming.point() == base.incoming.point() &&
              otam::splineSegment(biased, biased.splines[0], 0).bezier() ==
                  untouched.bezier(),
          "bias edit leaked to a welded position or another tangent");
    const auto magnitudeOnly =
        shaped.outgoing.biasAt(shaped.outgoing.origin, true);
    near(magnitudeOnly[0], bias[0]);
    near(magnitudeOnly[1], bias[1]);
    near(magnitudeOnly[2], 0);
    const auto midpoint      = otam::sampleSpline(shaped.bezier(), 0.5);
    const auto checkBoundary = [&](const otam::Mesh &patch) {
      for (const auto &triangle : patch.triangles)
        for (const auto &v : triangle.vertices)
          if (std::hypot(v[0] - midpoint[0], v[1] - midpoint[1],
                         v[2] - midpoint[2]) < 1e-8)
            return;
      throw std::runtime_error("patch did not follow the edited bias handle");
    };
    checkBoundary(otam::previewMesh(biased, {}, 2));
    biased.patches[0].points.pop_back();
    const auto triangular = otam::previewMesh(biased, {}, 2);
    check(triangular.triangles.size() == 4, "curved triangle tessellation");
    checkBoundary(triangular);
    biased.points[1].position[2] = 2;
    const auto movedIn  = otam::splineSegment(biased, biased.splines[0], 0);
    const auto movedOut = otam::splineSegment(biased, biased.splines[1], 0);
    near(movedIn.incoming.origin[2], 2);
    near(movedOut.outgoing.origin[2], 2);
    auto curved                            = d;
    curved.models[0].points[3].position[2] = 1;
    const auto coarse                      = otam::previewMesh(curved, 0, 2),
               fine                        = otam::previewMesh(curved, 0, 4);
    bool varying                           = false;
    for (const auto &t : coarse.triangles)
      for (int i = 0; i < 3; ++i) {
        if (std::abs(t.normals[i][0]) > .1 || std::abs(t.normals[i][1]) > .1)
          varying = true;
        bool matched = false;
        for (const auto &f : fine.triangles)
          for (int j = 0; j < 3; ++j)
            if (t.vertices[i] == f.vertices[j]) {
              for (int k = 0; k < 3; ++k)
                near(t.normals[i][k], f.normals[j][k]);
              matched = true;
            }
        check(matched, "shared tessellation sample missing");
      }
    check(varying, "curved patch normals stayed flat");
    otam::Mesh normalMesh;
    otam::Triangle nt;
    nt.vertices   = {{{{-1, -1, 0}}, {{1, -1, 0}}, {{0, 1, 0}}}};
    nt.hasNormals = true;
    for (auto &n : nt.normals) n = {{1, 0, 1}};
    normalMesh.triangles.push_back(nt);
    auto scaled = otam::transformed(normalMesh, otam::scaling(2, 1, 1));
    near(scaled.triangles[0].normals[0][0], .5);
    near(scaled.triangles[0].normals[0][2], 1);
    check(!otam::transformed(normalMesh, otam::scaling(0, 0, 0))
               .triangles[0]
               .hasNormals,
          "singular model transform kept invalid shading normals");
    const auto optical = otam::projected(
        normalMesh,
        otam::multiply(otam::translation(0, 0, 1000), otam::rotation(0, 20, 0)),
        otam::identity());
    check(!optical.triangles.empty(),
          "normal projection clipped visible triangle");
    for (const auto &t : optical.triangles)
      for (const auto &n : t.normals)
        for (int k = 0; k < 3; ++k) near(n[k], nt.normals[0][k]);
    normalMesh.triangles[0].vertices = {
        {{{-1, -1, 998}}, {{1, -1, 1002}}, {{0, 1, 998}}}};
    normalMesh.triangles[0].normals = {{{{1, 0, 1}}, {{0, 1, 1}}, {{0, 0, 1}}}};
    const auto normalClipped        = otam::projected(normalMesh, 0, 0, 0);
    check(normalClipped.triangles.size() == 2 &&
              normalClipped.triangles[0].hasNormals,
          "normal clipping");
    near(normalClipped.triangles[0].normals[1][0], .75);
    near(normalClipped.triangles[0].normals[1][1], .25);
    auto overlap  = d;
    auto topGroup = overlap.models[0].groups.front();
    topGroup.name = "Cover";
    overlap.models[0].groups.push_back(topGroup);
    const auto layered = otam::previewMesh(overlap);
    check(layered.triangles.front().colorGroup == 1 &&
              layered.triangles.front().color == mesh.triangles.front().color,
          "equal RGB values lost the winning group identity");
    check(otam::projected(layered, 0, 0, 0).triangles.front().colorGroup == 1,
          "camera projection lost the group identity");
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
    // Full optical camera pose: inverse TRS, clipping after rotation, and
    // exact historical framing with zero out-of-plane angles.
    const auto pose = otam::multiply(
        otam::translation(30, -40, 1100),
        otam::multiply(otam::rotation(23, -31, 17), otam::scaling(2, 3, 0.5)));
    const auto inv  = otam::inverse(pose);
    const auto unit = otam::multiply(inv, pose);
    for (int k = 0; k < 16; ++k) near(unit[k], otam::identity()[k]);
    rejects([&] { otam::inverse(otam::scaling(1, 0, 1)); });
    const auto cameraMesh =
        otam::transformed(mesh, otam::translation(0, 0, -1000));
    auto cameraProjection = otam::projected(otam::transformed(cameraMesh, pose),
                                            pose, otam::identity());
    check(cameraProjection.triangles.size() == mesh.triangles.size(),
          "camera lost front-facing geometry");
    for (std::size_t t = 0; t < mesh.triangles.size(); ++t)
      for (int v = 0; v < 3; ++v) {
        near(cameraProjection.triangles[t].vertices[v][0],
             mesh.triangles[t].vertices[v][0]);
        near(cameraProjection.triangles[t].vertices[v][1],
             mesh.triangles[t].vertices[v][1]);
      }
    const auto planar = otam::multiply(
        otam::translation(12, -17, 0),
        otam::multiply(otam::rotation(0, 0, 37), otam::scaling(2, 3, 1)));
    const auto oldMesh = otam::transformed(mesh, otam::translation(0, 0, 230));
    const auto oldProjection = otam::projected(oldMesh, 12, -17, 100);
    cameraProjection         = otam::projected(
                oldMesh, otam::multiply(otam::translation(0, 0, 1100), planar),
                otam::multiply(planar, otam::scaling(1.1, 1.1, 1)));
    for (std::size_t t = 0; t < oldProjection.triangles.size(); ++t)
      for (int v = 0; v < 3; ++v)
        for (int k = 0; k < 3; ++k)
          near(cameraProjection.triangles[t].vertices[v][k],
               oldProjection.triangles[t].vertices[v][k]);
    const auto back = otam::multiply(otam::translation(0, 0, 1000),
                                     otam::rotation(0, 180, 0));
    check(otam::projected(mesh, back, otam::identity()).triangles.empty(),
          "back-facing camera rendered behind its eye");
    auto eyeCrossing = cameraMesh;
    eyeCrossing.triangles.resize(1);
    eyeCrossing.triangles[0].vertices[0][2] = 10;
    check(otam::projected(otam::transformed(eyeCrossing, pose), pose,
                          otam::identity())
                  .triangles.size() == 2,
          "rotated camera clipped away the visible part of a triangle");
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

#include "liverender.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
}  // namespace
int main() {
  using namespace otglb;
  try {
    auto scene = std::make_shared<RenderScene>();
    RenderTriangle t{};
    t.vertices = {{{-1, -1, .5}, {1, -1, 1}, {0, 1, 2}}};
    scene->triangles.push_back(t);
    scene->surfaceCount       = 1;
    scene->surfacePerspective = true;
    scene->surfaceShader      = [](std::size_t, const std::array<double, 3> &) {
      SurfaceSample s;
      s.local        = {{.1f, .2f, .3f}};
      s.view         = {{0, 0, 100}};
      s.reflectivity = .5;
      s.blend        = 1;
      return s;
    };
    const auto live = prepareLiveFrame(*scene);
    check(live.batches.size() == 1 && live.batches[0].vertices.size() == 3,
          "live surface missing");
    check(live.batches[0].vertices[0].position[3] == 2 &&
              live.batches[0].vertices[2].position[3] == .5,
          "live perspective interpolation lost reciprocal depth");
    RenderScene parent;
    parent.layers.push_back({scene, {{2, 0, 3, 0, 2, 4}}});
    parent.colorScale[3] = .25;
    const auto group     = prepareLiveFrame(parent);
    check(group.batches[0].affine[2] == 3 &&
              group.batches[0].colorScale[3] == .25,
          "live group lost source placement/opacity");
    std::atomic<bool> obsolete{true};
    check(prepareLiveFrame(*scene, nullptr, &obsolete).batches.empty(),
          "obsolete preparation escaped");
    t.vertices[0].x     = std::numeric_limits<double>::infinity();
    scene->triangles[0] = t;
    bool rejected       = false;
    try {
      prepareLiveFrame(*scene);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    check(rejected, "non-finite GPU geometry accepted");
    scene->triangles[0].vertices[0].x = -1;
    scene->triangles.resize(LiveFrame::MaxVertices / 3 + 1,
                            scene->triangles[0]);
    rejected = false;
    try {
      prepareLiveFrame(*scene);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    check(rejected, "GPU triangle capacity silently truncated");
    std::cout << "Live scene hierarchy, perspective, cancellation and capacity "
                 "checks passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

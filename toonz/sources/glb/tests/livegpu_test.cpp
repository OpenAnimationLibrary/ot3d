#include "livegpu.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QImage>
#include <iostream>
#include <cmath>
#include <stdexcept>

namespace {
void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
void near(float a, float b, float tolerance = .003f) {
  if (std::abs(a - b) > tolerance) {
    std::cerr << a << " != " << b << '\n';
    throw std::runtime_error("GPU/software reflection mismatch");
  }
}
otglb::RenderScene fixture(otglb::EnvironmentLayer layer) {
  using namespace otglb;
  RenderScene scene;
  RenderTriangle t{};
  t.vertices = {{{-1, -1, 1}, {3, -1, 1}, {-1, 3, 1}}};
  scene.triangles.push_back(t);
  scene.surfaceCount  = 1;
  scene.environments  = {layer};
  scene.surfaceShader = [](std::size_t, const std::array<double, 3> &) {
    SurfaceSample s;
    s.local        = {{.1f, .2f, .3f}};
    s.reflectivity = .6;
    s.blend        = .8;
    return s;
  };
  return scene;
}
}  // namespace
int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  try {
    QSurfaceFormat format;
    format.setVersion(2, 1);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    QOpenGLContext context;
    context.setFormat(format);
    check(context.create(), "GPU test cannot create context");
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    check(context.makeCurrent(&surface),
          "GPU test cannot make context current");
    auto *f = context.functions();
    std::cout << "Renderer: " << f->glGetString(GL_RENDERER) << '\n';
    QOpenGLFramebufferObjectFormat targetFormat;
    targetFormat.setInternalTextureFormat(0x8814);
    QOpenGLFramebufferObject target(64, 64, targetFormat);
    check(target.isValid(), "float target allocation");
    target.bind();
    otglb::LiveGpu gpu;
    otglb::EnvironmentLayer layer;
    layer.map = std::make_shared<otglb::EnvironmentMap>(
        1, 1, std::vector<otglb::EnvironmentColor>{{{4, .5f, .25f}}});
    QMatrix4x4 matrix;
    QString error;
    auto test = [&](otglb::RenderScene scene) {
      auto frame =
          std::make_shared<otglb::LiveFrame>(otglb::prepareLiveFrame(scene));
      target.bind();
      f->glClearColor(0, 0, 0, 0);
      f->glClear(GL_COLOR_BUFFER_BIT);
      f->glViewport(2, 3, 19, 23);
      f->glActiveTexture(GL_TEXTURE3);
      check(gpu.draw(frame, matrix, 64, 64, error), error.toUtf8().constData());
      GLint viewport[4], active = 0, bound = 0;
      f->glGetIntegerv(GL_VIEWPORT, viewport);
      f->glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
      f->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
      check(viewport[0] == 2 && viewport[1] == 3 && viewport[2] == 19 &&
                viewport[3] == 23 && active == GL_TEXTURE3 &&
                GLuint(bound) == target.handle(),
            "GPU changed surrounding viewer state");
      float pixel[4]{};
      f->glReadPixels(32, 32, 1, 1, GL_RGBA, GL_FLOAT, pixel);
      const auto s        = scene.surfaceShader(0, {{.25, .25, .5}});
      const auto expected = otglb::shadeSurfaceSample(s, scene.environments);
      near(pixel[0], expected.r);
      near(pixel[1], expected.g);
      near(pixel[2], expected.b);
      near(pixel[3], expected.alpha);
      const auto uploads = gpu.uploads();
      check(gpu.draw(frame, matrix, 64, 64, error), error.toUtf8().constData());
      check(gpu.uploads() == uploads,
            "Unchanged frame uploaded geometry again");
    };
    test(fixture(layer));
    const auto environmentUploads = gpu.environmentUploads();
    layer.overrideMaterial        = true;
    layer.reflectivity            = .8;
    layer.blend                   = .25;
    layer.gain                    = {{.5f, 2, 3}};
    test(fixture(layer));
    check(gpu.environmentUploads() == environmentUploads,
          "Material edit uploaded an unchanged panorama again");
    std::vector<otglb::EnvironmentColor> pixels;
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 8; ++x) pixels.push_back({{x / 8.f, y / 4.f, .25f}});
    layer.map =
        std::make_shared<otglb::EnvironmentMap>(8, 4, std::move(pixels));
    layer.setRotation({{15, 90, -30}});
    test(fixture(layer));
    auto scene          = fixture(layer);
    auto ibl            = layer;
    ibl.diffuseLighting = true;
    scene.environments.insert(scene.environments.begin(), ibl);
    test(scene);
    scene.surfaceShader = [](std::size_t, const std::array<double, 3> &) {
      otglb::SurfaceSample s;
      s.local = {{.2f, .3f, .4f}};
      s.alpha = .4f;
      return s;
    };
    test(scene);
    scene.environments.assign(5, layer);
    bool rejected = false;
    try {
      otglb::prepareLiveFrame(scene);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    check(rejected, "Excess reflection layers silently truncated");
    {
      using namespace otglb;
      auto pixels = std::make_shared<std::vector<ColorPixel>>();
      pixels->push_back({1, 0, 0, 1});
      pixels->push_back({0, 1, 0, 1});
      const auto plane = prepareImagePlane(2, 1, pixels, {});
      auto live        = std::make_shared<LiveFrame>(prepareLiveFrame(plane));
      target.bind();
      f->glClearColor(0, 0, 0, 0);
      f->glClear(GL_COLOR_BUFFER_BIT);
      QMatrix4x4 planeMatrix;
      planeMatrix.scale(1, 2);
      check(gpu.draw(live, planeMatrix, 64, 64, error),
            error.toUtf8().constData());
      float left[4], right[4];
      f->glReadPixels(0, 32, 1, 1, GL_RGBA, GL_FLOAT, left);
      f->glReadPixels(63, 32, 1, 1, GL_RGBA, GL_FLOAT, right);
      check(
          left[0] > .99 && left[1] < .001 && right[0] < .001 && right[1] > .99,
          "Live image-plane texture sampling/wrap differs from CPU");
    }
    check(f->glGetError() == GL_NO_ERROR, "Live Render left an OpenGL error");
    if (argc > 1) {
      using namespace otglb;
      std::vector<EnvironmentColor> studio(512 * 256);
      for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 512; ++x) {
          const float sky   = .04f + .12f * y / 256;
          const bool window = (x > 80 && x < 130 && y > 95 && y < 205) ||
                              (x > 285 && x < 310 && y > 105 && y < 225);
          studio[y * 512 + x] =
              window ? EnvironmentColor{{3, 2.7f, 2.4f}}
                     : EnvironmentColor{{sky, sky * .9f, sky * 1.2f}};
        }
      EnvironmentLayer env;
      env.map = std::make_shared<EnvironmentMap>(512, 256, std::move(studio));
      auto sphere          = std::make_shared<RenderScene>();
      sphere->environments = {env};
      std::vector<std::array<EnvironmentDirection, 3>> normals;
      const double pi = std::acos(-1.0);
      for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 64; ++x) {
          EnvironmentDirection points[4];
          for (int j = 0; j < 4; ++j) {
            double a  = pi * (y + (j > 1)) / 32,
                   b  = 2 * pi * (x + (j == 1 || j == 3)) / 64;
            points[j] = {{std::sin(a) * std::cos(b), std::cos(a),
                          std::sin(a) * std::sin(b)}};
          }
          for (const auto indices :
               {std::array<int, 3>{{0, 1, 2}}, std::array<int, 3>{{1, 3, 2}}}) {
            RenderTriangle t{};
            std::array<EnvironmentDirection, 3> n;
            for (int j = 0; j < 3; ++j) {
              n[j]          = points[indices[j]];
              t.vertices[j] = {n[j][0] * .85, n[j][1] * .85, n[j][2]};
            }
            sphere->triangles.push_back(t);
            normals.push_back(n);
          }
        }
      sphere->surfaceCount  = sphere->triangles.size();
      sphere->surfaceShader = [normals](std::size_t i,
                                        const std::array<double, 3> &b) {
        SurfaceSample s;
        s.normal = {{0, 0, 0}};
        for (int k = 0; k < 3; ++k)
          for (int j = 0; j < 3; ++j) s.normal[k] += b[j] * normals[i][j][k];
        const double length = std::hypot(s.normal[0], s.normal[1], s.normal[2]);
        for (auto &v : s.normal) v /= length;
        s.local        = {{.075f, .018f, .13f}};
        s.reflectivity = .8;
        s.blend        = .7;
        return s;
      };
      auto imageFrame = std::make_shared<LiveFrame>(prepareLiveFrame(*sphere));
      QOpenGLFramebufferObject image(512, 512);
      image.bind();
      f->glClearColor(.035f, .035f, .045f, 1);
      f->glClear(GL_COLOR_BUFFER_BIT);
      check(gpu.draw(imageFrame, matrix, 512, 512, error),
            error.toUtf8().constData());
      check(image.toImage().save(QString::fromLocal8Bit(argv[1])),
            "GPU snapshot save failed");
    }
    gpu.release();
    std::cout << "Live GPU reflection, HDR, rotation, IBL, alpha, cache and "
                 "state checks passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

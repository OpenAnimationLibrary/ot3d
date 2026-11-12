#pragma once

#include "tgl.h"
#include "toonz/amlevelrender.h"

#include <algorithm>
#include <cmath>
#include <optional>

// The Viewer uses an orthographic GL viewport, even when the model itself is
// projected through the scene camera. Keep eye Z to drag at the picked depth.
struct AMToolProjection {
  otam::Matrix modelToEye, eyeToModel, filmToScreen, screenToFilm;
  bool perspective;

  std::optional<otam::Vec3> project(const otam::Vec3 &point) const {
    auto eye = otam::transform(modelToEye, point);
    if (perspective) {
      if (eye[2] > -1) return {};
      const double scale = -1000.0 / eye[2];
      eye[0] *= scale;
      eye[1] *= scale;
    } else if (std::abs(eye[2]) > 4000.0) {
      return {};
    }
    const auto screen = otam::transform(filmToScreen, eye);
    if (!std::all_of(screen.begin(), screen.end(),
                     [](double n) { return std::isfinite(n); }))
      return {};
    return screen;
  }

  otam::Vec3 unproject(const TPointD &point, double depth) const {
    auto film = otam::transform(screenToFilm, {{point.x, point.y, depth}});
    if (perspective) {
      const double scale = -depth / 1000.0;
      film[0] *= scale;
      film[1] *= scale;
    }
    return otam::transform(eyeToModel, film);
  }
};

inline AMToolProjection amToolProjection(TXsheet *xsheet, double frame,
                                         otam::Matrix modelToEye, bool view3D) {
  // Use the exact matrix in effect for the Viewer tool overlay, including
  // camera stand rotation, pan, zoom and device-pixel centering.
  double glMatrix[16];
  glGetDoublev(GL_MODELVIEW_MATRIX, glMatrix);
  otam::Matrix viewer;
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c) viewer[4 * r + c] = glMatrix[4 * c + r];

  auto filmToScreen = otam::identity();
  if (view3D) {
    modelToEye = otam::multiply(viewer, modelToEye);
  } else {
    const auto camera = amModelCamera(xsheet, frame);
    modelToEye        = otam::multiply(otam::inverse(camera.pose), modelToEye);
    filmToScreen      = otam::multiply(viewer, camera.imageToStage);
  }
  return {modelToEye, otam::inverse(modelToEye), filmToScreen,
          otam::inverse(filmToScreen), !view3D};
}

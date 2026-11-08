#pragma once

#include "liverender.h"
#include <QMatrix4x4>
#include <QString>
#include <memory>

namespace otglb {
// Context-local resources. Call release() with the owning context current.
// draw() targets the currently bound framebuffer and preserves its GL state.
class LiveGpu {
  class Imp;
  std::unique_ptr<Imp> m_imp;

public:
  LiveGpu();
  ~LiveGpu();
  void release();
  bool draw(const std::shared_ptr<const LiveFrame> &frame,
            const QMatrix4x4 &imageToClip, int width, int height,
            QString &error);
  std::size_t uploads() const;
  std::size_t environmentUploads() const;
};
}  // namespace otglb

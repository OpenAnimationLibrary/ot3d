#pragma once

#include "sceneviewer.h"
#include <QSet>
#include <QString>
#include <memory>

class LiveRenderController;
class QLabel;
namespace otglb {
class LiveGpu;
struct LiveFrame;
}  // namespace otglb

// Replacement behind the existing SceneViewer panel identifier. Inherited
// tablet input, 2D drawing, playback, rulers and Preview remain in SceneViewer.
class HybridSceneViewer final : public SceneViewer {
  QSet<QString> m_reportedDiagnostics;

  bool m_liveRender = false, m_liveDirty = true, m_liveFailed = false;
  std::unique_ptr<LiveRenderController> m_liveController;
  std::unique_ptr<otglb::LiveGpu> m_liveGpu;
  std::shared_ptr<const otglb::LiveFrame> m_liveFrame;
  QLabel *m_liveStatus;
  QMetaObject::Connection m_liveContextConnection;
  void invalidateLive();
  bool drawLive();
  void releaseLiveGpu();
  void reportDiagnostic(const QString &message);

protected:
  void drawScene() override;
  void initializeGL() override;

public:
  explicit HybridSceneViewer(ImageUtils::FullScreenWidget *parent);
  ~HybridSceneViewer() override;
  bool isLiveRenderEnabled() const { return m_liveRender; }
  void setLiveRenderEnabled(bool enabled);
};

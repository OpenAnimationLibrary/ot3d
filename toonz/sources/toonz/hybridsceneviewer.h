#pragma once

#include "sceneviewer.h"
#include <QSet>
#include <QString>

// Replacement behind the existing SceneViewer panel identifier. Inherited
// tablet input, 2D drawing, playback, rulers and Preview remain in SceneViewer.
class HybridSceneViewer final : public SceneViewer {
  QSet<QString> m_reportedDiagnostics;

  void reportDiagnostic(const QString &message);

protected:
  void drawScene() override;

public:
  explicit HybridSceneViewer(ImageUtils::FullScreenWidget *parent);
};

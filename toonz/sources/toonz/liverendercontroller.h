#pragma once

#include "liverender.h"
#include "trasterfx.h"
#include <QObject>
#include <QTimer>
#include <functional>
#include <memory>

// One preparation task per viewer. Requests supersede an active task, and the
// viewer builds only the latest pending snapshot when that task finishes.
class LiveRenderController final : public QObject {
  struct Job;
  std::shared_ptr<Job> m_job;
  QTimer m_poll;
  unsigned long long m_generation = 0;
  void poll();

public:
  explicit LiveRenderController(QObject *parent);
  ~LiveRenderController();
  void invalidate();
  bool busy() const { return bool(m_job); }
  void prepare(TFxP root, double frame, const TRenderSettings &settings);
  std::function<void(std::shared_ptr<const otglb::LiveFrame>, const QString &)>
      completed;
};

#include "liverendercontroller.h"

#include "toonz/t3drendersource.h"
#include "trenderer.h"
#include "texception.h"
#include <QThreadPool>
#include <QRunnable>
#include <atomic>
#include <set>
#include <stdexcept>

namespace {
class Task final : public QRunnable {
  std::function<void()> m_work;

public:
  explicit Task(std::function<void()> work) : m_work(std::move(work)) {}
  void run() override { m_work(); }
};
void ordered(TFx *fx, std::set<TFx *> &visited, std::vector<TFx *> &nodes) {
  if (!fx || !visited.insert(fx).second) return;
  if (visited.size() > 10000)
    throw std::runtime_error("Live Render FX graph exceeds 10000 nodes");
  for (int i = 0; i < fx->getInputPortCount(); ++i)
    ordered(fx->getInputPort(i)->getFx(), visited, nodes);
  nodes.push_back(fx);
}
class Session {
  TRenderer m_renderer;
  unsigned long m_id;
  double m_frame;

public:
  Session(double frame)
      : m_renderer(1), m_id(TRenderer::buildRenderId()), m_frame(frame) {
    m_renderer.enablePrecomputing(false);
    m_renderer.install(m_id);
    m_renderer.declareRenderStart(m_id);
    m_renderer.declareFrameStart(frame);
  }
  ~Session() {
    m_renderer.declareFrameEnd(m_frame);
    m_renderer.declareRenderEnd(m_id);
    m_renderer.uninstall();
  }
};
std::shared_ptr<const otglb::RenderScene> collect(
    TFx *fx, double frame, const TRenderSettings &settings,
    const std::atomic<bool> &obsolete, unsigned depth = 0) {
  if (!fx || obsolete.load()) return {};
  if (depth >= 128)
    throw std::runtime_error("Live Render FX nesting exceeds 128");
  if (auto *source = dynamic_cast<T3DRenderSource *>(fx))
    return source->get3DRenderScene(frame, nullptr, nullptr, nullptr,
                                    &settings);
  auto result = std::make_shared<otglb::RenderScene>();
  if (auto *affine = dynamic_cast<NaAffineFx *>(fx)) {
    auto child   = collect(fx->getInputPort(0)->getFx(), frame, settings,
                           obsolete, depth + 1);
    const auto a = affine->getPlacement(frame);
    if (child)
      result->layers.push_back(
          {child, {{a.a11, a.a12, a.a13, a.a21, a.a22, a.a23}}});
  } else if (fx->getFxType() == "STD_overFx" || fx->getFxType() == "overFx") {
    for (int i = fx->getInputPortCount() - 1; i >= 0; --i) {
      auto child = collect(fx->getInputPort(i)->getFx(), frame, settings,
                           obsolete, depth + 1);
      if (child) result->layers.push_back({child});
    }
  } else {
    // Ordinary 2D columns are drawn by the existing viewer. Do not silently
    // bypass unsupported post effects and claim that their 3D input is final.
    std::set<TFx *> visited;
    std::vector<TFx *> nodes;
    ordered(fx, visited, nodes);
    for (auto *node : nodes)
      if (dynamic_cast<T3DRenderSource *>(node))
        throw std::runtime_error(
            "Live Render encountered a 2D FX after 3D; use Preview to evaluate "
            "the complete graph");
  }
  return result;
}
}  // namespace
struct LiveRenderController::Job {
  std::atomic<bool> done{false}, obsolete{false};
  unsigned long long generation = 0;
  std::shared_ptr<const otglb::LiveFrame> result;
  QString error;
};
LiveRenderController::LiveRenderController(QObject *parent) : QObject(parent) {
  TRenderer::initialize();
  m_poll.setInterval(8);
  connect(&m_poll, &QTimer::timeout, this, [this] { poll(); });
}
LiveRenderController::~LiveRenderController() { invalidate(); }
void LiveRenderController::invalidate() {
  ++m_generation;
  if (m_job) m_job->obsolete.store(true);
}
void LiveRenderController::prepare(TFxP root, double frame,
                                   const TRenderSettings &settings) {
  if (m_job) return;
  auto job        = std::make_shared<Job>();
  job->generation = m_generation;
  m_job           = job;
  QThreadPool::globalInstance()->start(new Task([job, root, frame, settings] {
    try {
      Session session(frame);
      std::set<TFx *> visited;
      std::vector<TFx *> nodes;
      ordered(root.getPointer(), visited, nodes);
      // Mirror the renderer lifecycle for image inputs evaluated by 3D FX.
      struct Handlers {
        const std::vector<TFx *> &nodes;
        const TRenderSettings &settings;
        double frame;
        std::size_t started = 0, frames = 0;
        ~Handlers() {
          while (frames)
            nodes[--frames]->callEndRenderFrameHandler(&settings, frame);
          while (started) nodes[--started]->callEndRenderHandler();
        }
      } handlers{nodes, settings, frame};
      for (auto *node : nodes) {
        node->callStartRenderHandler();
        ++handlers.started;
      }
      for (auto *node : nodes) {
        node->callStartRenderFrameHandler(&settings, frame);
        ++handlers.frames;
      }
      auto scene = collect(root.getPointer(), frame, settings, job->obsolete);
      if (!job->obsolete.load()) {
        if (!scene) scene = std::make_shared<otglb::RenderScene>();
        auto live = otglb::prepareLiveFrame(*scene, nullptr, &job->obsolete);
        if (!job->obsolete.load())
          job->result = std::make_shared<otglb::LiveFrame>(std::move(live));
      }
    } catch (const TException &e) {
      job->error = QString::fromStdWString(e.getMessage());
    } catch (const std::exception &e) {
      job->error = QString::fromUtf8(e.what());
    } catch (...) {
      job->error = QStringLiteral("Live Render preparation failed");
    }
    job->done.store(true);
  }));
  m_poll.start();
}
void LiveRenderController::poll() {
  if (!m_job || !m_job->done.load()) return;
  auto job = std::move(m_job);
  m_poll.stop();
  if (job->generation == m_generation && !job->obsolete.load() && completed)
    completed(job->result, job->error);
  else if (completed)
    completed({}, QString());
}

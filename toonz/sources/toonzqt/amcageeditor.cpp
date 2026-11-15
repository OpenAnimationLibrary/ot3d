#include "amcageeditor.h"

#include "amcage.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace {
using otam::CageIndex;
using otam::DistortionCage;
using otam::Vec3;

QString label(const char *text) {
  return QCoreApplication::translate("AMDistortionCageEditor", text);
}

class CageSlice final : public QWidget {
  const DistortionCage &m_cage;
  const CageIndex &m_selected;
  const int &m_frame;
  CageIndex m_axes{0, 1, 2};
  QRectF m_dragBounds;
  QPointF m_press;
  Vec3 m_pressOffset{};
  bool m_dragging = false;

  template <class Visit>
  void visitSlice(Visit visit) const {
    const int a = m_axes[0], b = m_axes[1];
    CageIndex index = m_selected;
    for (int v = 0; v <= m_cage.sections[b]; ++v)
      for (int u = 0; u <= m_cage.sections[a]; ++u) {
        index[a] = u;
        index[b] = v;
        visit(index);
      }
  }

  QPointF location(const CageIndex &index, const Vec3 &offset) const {
    const int a = m_axes[0], b = m_axes[1];
    return {double(index[a]) / m_cage.sections[a] + offset[a],
            1 - double(index[b]) / m_cage.sections[b] - offset[b]};
  }

  QRectF modelBounds() const {
    if (m_dragging) return m_dragBounds;
    double left = -0.12, top = -0.12, right = 1.12, bottom = 1.12;
    visitSlice([&](const CageIndex &index) {
      const auto p = location(index, m_cage.offset(index, m_frame));
      left         = std::min(left, p.x() - 0.12);
      top          = std::min(top, p.y() - 0.12);
      right        = std::max(right, p.x() + 0.12);
      bottom       = std::max(bottom, p.y() + 0.12);
    });
    return {left, top, right - left, bottom - top};
  }

  QTransform projection(const QRectF &bounds) const {
    const double scale =
        std::min(width() / bounds.width(), height() / bounds.height());
    QTransform map;
    map.translate(width() * 0.5, height() * 0.5);
    map.scale(scale, scale);
    map.translate(-bounds.center().x(), -bounds.center().y());
    return map;
  }

public:
  std::function<void(const CageIndex &)> selectPoint;
  std::function<void(const Vec3 &)> movePoint;

  CageSlice(const DistortionCage &cage, const CageIndex &selected,
            const int &frame)
      : m_cage(cage), m_selected(selected), m_frame(frame) {
    setMinimumSize(380, 300);
    setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(label("Distortion cage slice"));
  }

  const CageIndex &axes() const { return m_axes; }

  void setPlane(int plane) {
    constexpr CageIndex axes[] = {{0, 1, 2}, {0, 2, 1}, {1, 2, 0}};
    m_axes                     = axes[plane];
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().base());
    const auto map  = projection(modelBounds());
    const auto rest = [&](const CageIndex &index) {
      return map.map(location(index, {}));
    };
    const auto posed = [&](const CageIndex &index) {
      return map.map(location(index, m_cage.offset(index, m_frame)));
    };
    painter.setPen(QPen(palette().mid().color(), 1, Qt::DotLine));
    visitSlice([&](const CageIndex &index) {
      for (int i = 0; i < 2; ++i) {
        auto next = index;
        ++next[m_axes[i]];
        if (m_cage.contains(next)) painter.drawLine(rest(index), rest(next));
      }
    });
    painter.setPen(QPen(palette().mid().color(), 1.5));
    visitSlice([&](const CageIndex &index) {
      for (int i = 0; i < 2; ++i) {
        auto next = index;
        ++next[m_axes[i]];
        if (m_cage.contains(next)) painter.drawLine(posed(index), posed(next));
      }
    });
    visitSlice([&](const CageIndex &index) {
      const auto offset     = m_cage.offset(index, m_frame);
      const double distance = std::hypot(offset[0], offset[1], offset[2]);
      const double hue      = (1 - std::min(1.0, distance / 0.25)) / 3;
      const bool selected   = index == m_selected;
      QColor tint           = QColor::fromHsvF(hue, 0.55, 0.87);
      const auto background = palette().base().color();
      tint.setRed((3 * tint.red() + background.red()) / 4);
      tint.setGreen((3 * tint.green() + background.green()) / 4);
      tint.setBlue((3 * tint.blue() + background.blue()) / 4);
      painter.setBrush(tint);
      painter.setPen(QPen(
          selected ? palette().highlight().color() : palette().text().color(),
          selected ? 2.5 : 1.0));
      const double radius = selected ? 6 : 4.5;
      painter.drawEllipse(posed(index), radius, radius);
    });
  }

  void keyPressEvent(QKeyEvent *event) override {
    auto index = m_selected;
    switch (event->key()) {
    case Qt::Key_Left:
      --index[m_axes[0]];
      break;
    case Qt::Key_Right:
      ++index[m_axes[0]];
      break;
    case Qt::Key_Down:
      --index[m_axes[1]];
      break;
    case Qt::Key_Up:
      ++index[m_axes[1]];
      break;
    default:
      QWidget::keyPressEvent(event);
      return;
    }
    if (m_cage.contains(index)) selectPoint(index);
  }

  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() != Qt::LeftButton) return;
    const auto bounds = modelBounds();
    const auto map    = projection(bounds);
    auto nearest      = m_selected;
    double best       = 10;
    bool found        = false;
    visitSlice([&](const CageIndex &index) {
      const auto delta =
          map.map(location(index, m_cage.offset(index, m_frame))) -
          event->localPos();
      const double distance = std::hypot(delta.x(), delta.y());
      if (distance < best) {
        best    = distance;
        nearest = index;
        found   = true;
      }
    });
    if (!found) return;
    m_dragBounds  = bounds;
    m_press       = map.inverted().map(event->localPos());
    m_pressOffset = m_cage.offset(nearest, m_frame);
    m_dragging    = true;
    selectPoint(nearest);
  }

  void mouseMoveEvent(QMouseEvent *event) override {
    if (!m_dragging || !(event->buttons() & Qt::LeftButton)) return;
    const auto map   = projection(m_dragBounds);
    const auto delta = map.inverted().map(event->localPos()) - m_press;
    auto offset      = m_pressOffset;
    offset[m_axes[0]] =
        std::clamp(offset[m_axes[0]] + delta.x(), -DistortionCage::maxOffset,
                   DistortionCage::maxOffset);
    offset[m_axes[1]] =
        std::clamp(offset[m_axes[1]] - delta.y(), -DistortionCage::maxOffset,
                   DistortionCage::maxOffset);
    movePoint(offset);
  }

  void mouseReleaseEvent(QMouseEvent *event) override {
    if (event->button() != Qt::LeftButton) return;
    m_dragging = false;
    update();
  }
};

class CageEditor final : public QDialog {
  DistortionCage m_cage;
  CageIndex m_selected{0, 0, 0};
  int m_frame;
  CageSlice *m_view;
  QSpinBox *m_slice;
  QLabel *m_pointLabel;
  std::array<QDoubleSpinBox *, 3> m_offsets;
  QPushButton *m_setKey, *m_deleteKey;
  QLabel *m_status;

  void syncPoint() {
    const int axis = m_view->axes()[2];
    {
      const QSignalBlocker blocked(m_slice);
      m_slice->setRange(0, m_cage.sections[axis]);
      m_slice->setValue(m_selected[axis]);
    }
    const auto offset = m_cage.offset(m_selected, m_frame);
    for (int i = 0; i < 3; ++i) {
      const QSignalBlocker blocked(m_offsets[i]);
      m_offsets[i]->setValue(100 * offset[i]);
    }
    m_pointLabel->setText(label("Point %1, %2, %3")
                              .arg(m_selected[0])
                              .arg(m_selected[1])
                              .arg(m_selected[2]));
    const auto point = m_cage.points.find(m_selected);
    const bool atKey =
        point != m_cage.points.end() && point->second.count(m_frame);
    m_setKey->setEnabled(!atKey);
    m_deleteKey->setEnabled(atKey);
    m_view->update();
  }

  void moveSelected(const Vec3 &offset) {
    if (offset == m_cage.offset(m_selected, m_frame)) return;
    auto &track = m_cage.points[m_selected];
    if (!track.empty() && track.begin()->first >= 0)
      track[m_frame] = offset;
    else if (offset == Vec3{})
      m_cage.points.erase(m_selected);
    else
      track[-1] = offset;
    syncPoint();
  }

  bool commit(const std::function<void(const QString &)> &apply) {
    try {
      const auto serialized = otam::writeCage(m_cage);
      apply(QString::fromUtf8(serialized.data(), int(serialized.size())));
      m_status->clear();
      return true;
    } catch (const std::exception &error) {
      m_status->setText(QString::fromUtf8(error.what()));
      return false;
    }
  }

public:
  CageEditor(QWidget *parent, DistortionCage cage,
             const std::shared_ptr<const otam::Model> &model, int frame,
             const std::function<void(const QString &)> &apply)
      : QDialog(parent)
      , m_cage(std::move(cage))
      , m_frame(std::clamp(frame, 0, std::numeric_limits<int>::max() - 1)) {
    setWindowTitle(label("A:M Distortion Cage"));
    auto *layout   = new QVBoxLayout(this);
    auto *settings = new QFormLayout();
    layout->addLayout(settings);
    auto *target = new QComboBox(this);
    target->addItem(label("Model Bone (whole model)"), QString());
    if (model)
      for (const auto &bone : model->bones) {
        const auto path =
            QString::fromUtf8(bone.path.data(), int(bone.path.size()));
        target->addItem(path, path);
      }
    const auto path =
        QString::fromUtf8(m_cage.bonePath.data(), int(m_cage.bonePath.size()));
    int selected = target->findData(path);
    if (selected < 0) {
      target->addItem(label("Missing bone (%1)").arg(path), path);
      selected = target->count() - 1;
    }
    target->setCurrentIndex(selected);
    target->setToolTip(
        label("A named bone also includes all its descendants."));
    settings->addRow(label("Target"), target);
    connect(target, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, target](int) {
              m_cage.bonePath =
                  target->currentData().toString().toUtf8().toStdString();
            });

    auto *sections              = new QHBoxLayout();
    const char *sectionLabels[] = {"Width", "Height", "Depth"};
    for (int i = 0; i < 3; ++i) {
      auto *count = new QSpinBox(this);
      count->setRange(1, DistortionCage::maxSections);
      count->setValue(m_cage.sections[i]);
      count->setAccessibleName(label(sectionLabels[i]));
      count->setToolTip(
          label("Point offsets stay at their grid indices when "
                "the section count changes."));
      sections->addWidget(new QLabel(label(sectionLabels[i]), this));
      sections->addWidget(count);
      connect(count, QOverload<int>::of(&QSpinBox::valueChanged), this,
              [this, i](int value) {
                m_cage.sections[i] = value;
                m_selected[i]      = std::min(m_selected[i], value);
                syncPoint();
              });
    }
    settings->addRow(label("Sections"), sections);

    auto *views = new QHBoxLayout();
    auto *plane = new QComboBox(this);
    plane->addItems(
        {label("Front (XY)"), label("Top (XZ)"), label("Side (YZ)")});
    m_slice = new QSpinBox(this);
    m_slice->setAccessibleName(label("Slice"));
    views->addWidget(plane);
    views->addWidget(new QLabel(label("Slice"), this));
    views->addWidget(m_slice);
    views->addStretch();
    auto *sceneFrame = new QSpinBox(this);
    sceneFrame->setRange(1, std::numeric_limits<int>::max());
    sceneFrame->setValue(m_frame + 1);
    sceneFrame->setAccessibleName(label("Frame"));
    views->addWidget(new QLabel(label("Frame"), this));
    views->addWidget(sceneFrame);
    layout->addLayout(views);

    m_selected[2] = m_cage.sections[2] / 2;
    m_view        = new CageSlice(m_cage, m_selected, m_frame);
    m_view->setParent(this);
    m_view->setToolTip(
        label("Drag a handle to move it in this slice. Use the arrow keys to "
              "select adjacent points. Green "
              "means less movement. Red means 25% of the "
              "cage size or more."));
    layout->addWidget(m_view, 1);
    m_view->selectPoint = [this](const CageIndex &index) {
      m_selected = index;
      syncPoint();
    };
    m_view->movePoint = [this](const Vec3 &offset) { moveSelected(offset); };
    connect(plane, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int view) {
              m_view->setPlane(view);
              syncPoint();
            });
    connect(m_slice, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int value) {
              m_selected[m_view->axes()[2]] = value;
              syncPoint();
            });
    connect(sceneFrame, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int value) {
              m_frame = value - 1;
              syncPoint();
            });

    auto *position = new QHBoxLayout();
    m_pointLabel   = new QLabel(this);
    position->addWidget(m_pointLabel);
    const char *axisLabels[] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; ++i) {
      auto *offset = m_offsets[i] = new QDoubleSpinBox(this);
      offset->setDecimals(2);
      offset->setSuffix("%");
      offset->setRange(-100 * DistortionCage::maxOffset,
                       100 * DistortionCage::maxOffset);
      offset->setAccessibleName(label(axisLabels[i]));
      offset->setToolTip(
          label("Offset as a percentage of the cage's rest "
                "size along this axis."));
      position->addWidget(new QLabel(label(axisLabels[i]), this));
      position->addWidget(offset);
      connect(offset, &QDoubleSpinBox::editingFinished, this,
              [this, offset, i] {
                auto value        = m_cage.offset(m_selected, m_frame);
                const double next = offset->value() / 100;
                if (std::abs(value[i] - next) <= 0.00005) return;
                value[i] = next;
                moveSelected(value);
              });
    }
    layout->addLayout(position);

    auto *keyRow = new QHBoxLayout();
    m_setKey     = new QPushButton(label("Set Point Key"), this);
    m_setKey->setToolTip(
        label("Key the selected point at this scene frame. "
              "Moving a keyed point adds or updates its key."));
    m_deleteKey = new QPushButton(label("Delete Point Key"), this);
    m_deleteKey->setToolTip(label("Removing a point's last key resets it."));
    auto *reset = new QPushButton(label("Reset Point"), this);
    for (auto *button : {m_setKey, m_deleteKey, reset}) {
      button->setAutoDefault(false);
      keyRow->addWidget(button);
    }
    layout->addLayout(keyRow);
    connect(m_setKey, &QPushButton::clicked, this, [this] {
      const auto offset = m_cage.offset(m_selected, m_frame);
      auto &track       = m_cage.points[m_selected];
      track.erase(-1);
      track[m_frame] = offset;
      syncPoint();
    });
    connect(m_deleteKey, &QPushButton::clicked, this, [this] {
      auto point = m_cage.points.find(m_selected);
      if (point == m_cage.points.end()) return;
      point->second.erase(m_frame);
      if (point->second.empty()) m_cage.points.erase(point);
      syncPoint();
    });
    connect(reset, &QPushButton::clicked, this, [this] { moveSelected({}); });

    auto *legend =
        new QLabel(label("Green to red shows increasing displacement"), this);
    layout->addWidget(legend);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                 QDialogButtonBox::Apply,
                             this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, apply] {
      if (commit(apply)) accept();
    });
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, [this, apply] { commit(apply); });
    syncPoint();
  }
};
}  // namespace

void editAMDistortionCage(QWidget *parent, const QString &state,
                          const std::shared_ptr<const otam::Model> &model,
                          int frame,
                          const std::function<void(const QString &)> &apply) {
  CageEditor editor(parent, otam::readCage(state.toUtf8().toStdString()), model,
                    frame, apply);
  editor.exec();
}

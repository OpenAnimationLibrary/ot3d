#include "primitivelibrary.h"

#include "floatingpanelcommand.h"
#include "iocommand.h"
#include "menubarcommandids.h"
#include "pane.h"
#include "tapp.h"
#include "texception.h"
#include "toonz/amlevelrender.h"
#include "toonz/stage.h"
#include "toonz/tcolumnhandle.h"
#include "toonz/tframehandle.h"
#include "toonz/toonzscene.h"
#include "toonz/tscenehandle.h"
#include "toonz/txsheet.h"
#include "toonz/txsheethandle.h"
#include "toonzqt/gutil.h"

#include <QCheckBox>
#include <QDir>
#include <QDrag>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QTemporaryFile>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <iterator>

namespace {
constexpr auto PrimitiveMime   = "application/x-ot3d-primitive";
constexpr auto SnapMime        = "application/x-ot3d-primitive-snap";
constexpr const char *Shapes[] = {
    QT_TRANSLATE_NOOP("PrimitiveLibrary", "Cube"),
    QT_TRANSLATE_NOOP("PrimitiveLibrary", "Sphere"),
    QT_TRANSLATE_NOOP("PrimitiveLibrary", "Cylinder"),
    QT_TRANSLATE_NOOP("PrimitiveLibrary", "Cone"),
};

QString translate(const char *text) {
  return QCoreApplication::translate("PrimitiveLibrary", text);
}

class PrimitiveList final : public QListWidget {
  QCheckBox *m_snap;

public:
  PrimitiveList(QCheckBox *snap, QWidget *parent)
      : QListWidget(parent), m_snap(snap) {
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    for (const auto *shape : Shapes) {
      auto *item = new QListWidgetItem(translate(shape), this);
      item->setData(Qt::UserRole, QString::fromLatin1(shape));
    }
  }

protected:
  void startDrag(Qt::DropActions) override {
    auto *item = currentItem();
    if (!item) return;
    auto *mime = new QMimeData;
    mime->setData(PrimitiveMime,
                  item->data(Qt::UserRole).toString().toLatin1());
    if (m_snap->isChecked()) mime->setData(SnapMime, "1");
    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->exec(Qt::CopyAction);
    drag->deleteLater();
  }
};

class PrimitiveLibraryFactory final : public TPanelFactory {
public:
  PrimitiveLibraryFactory() : TPanelFactory("PrimitiveLibrary") {}
  void initialize(TPanel *panel) override {
    auto *widget = new QWidget(panel);
    auto *layout = new QVBoxLayout(widget);
    auto *hint   = new QLabel(translate("Drag a model into the scene Viewer. "
                                          "Hold Shift to bypass snapping."),
                              widget);
    hint->setWordWrap(true);
    auto *snap = new QCheckBox(translate("Snap to model surfaces"), widget);
    snap->setChecked(true);
    layout->addWidget(hint);
    layout->addWidget(new PrimitiveList(snap, widget));
    layout->addWidget(snap);
    panel->setWidget(widget);
    panel->setWindowTitle(translate("Primitive Library"));
    panel->allowMultipleInstances(false);
  }
} primitiveLibraryFactory;

OpenFloatingPanel openPrimitiveLibrary(MI_OpenPrimitiveLibrary,
                                       "PrimitiveLibrary",
                                       translate("Primitive Library"));

// Publish the project copy atomically without replacing an existing model.
bool materialize(const QString &target, const QByteArray &bytes) {
  auto matches = [&] {
    QFile existing(target);
    return existing.open(QIODevice::ReadOnly) &&
           existing.size() == bytes.size() && existing.readAll() == bytes;
  };
  if (QFileInfo::exists(target)) return matches();
  if (!QDir().mkpath(QFileInfo(target).absolutePath())) return false;
  QTemporaryFile temp(target + ".XXXXXX");
  if (!temp.open() || temp.write(bytes) != bytes.size() || !temp.flush())
    return false;
  temp.close();
  if (!temp.rename(target)) return matches();
  temp.setAutoRemove(false);
  return true;
}

}  // namespace

bool PrimitiveLibrary::isDrag(const QMimeData *mime) {
  return mime->hasFormat(PrimitiveMime);
}

bool PrimitiveLibrary::canDrop(const QMimeData *mime) {
  const auto name = mime->data(PrimitiveMime);
  auto *app       = TApp::instance();
  auto *scene     = app->getCurrentScene()->getScene();
  return scene &&
         app->getCurrentXsheet()->getXsheet() == scene->getTopXsheet() &&
         std::any_of(std::begin(Shapes), std::end(Shapes),
                     [&](const char *shape) { return name == shape; });
}

bool PrimitiveLibrary::drop(const QMimeData *mime, const TPointD &position,
                            bool bypassSnap) {
  if (!canDrop(mime)) return false;
  auto *app    = TApp::instance();
  auto *scene  = app->getCurrentScene()->getScene();
  auto *xsheet = app->getCurrentXsheet()->getXsheet();

  const auto name = QString::fromLatin1(mime->data(PrimitiveMime));
  const auto file = name + ".glb";
  QFile source(":/primitives/" + file);
  const TFilePath path(("+extras/OT3D Primitives/" + file).toStdWString());
  const TFilePath target = scene->decodeFilePath(path);
  try {
    const bool snap = !bypassSnap && mime->hasFormat(SnapMime);
    // Each bundled model fits a centered unit cube in GLB coordinates.
    const double halfExtent =
        0.5 * otglb::ProjectionHeight / otglb::RenderOptions().orthoHeight;
    const int frame = app->getCurrentFrame()->getFrameIndex();
    auto placement =
        nativeModelDropPosition(xsheet, frame, position, snap, halfExtent);
    placement[0] /= Stage::inch;
    placement[1] /= Stage::inch;
    if (!target.isAbsolute() || !source.open(QIODevice::ReadOnly) ||
        source.size() <= 0 ||
        !materialize(target.getQString(), source.readAll())) {
      DVGui::error(translate("Could not copy the primitive to %1.")
                       .arg(target.getQString()));
      return false;
    }

    IoCmd::LoadResourceArguments args;
    args.resourceDatas.emplace_back(path);
    args.importPolicy  = IoCmd::LoadResourceArguments::LOAD;
    args.renamePolicy  = IoCmd::LoadResourceArguments::RenamePolicy::NEVER;
    args.convertPolicy = IoCmd::LoadResourceArguments::ConvertPolicy::NEVER;
    args.expose        = true;
    args.modelPosition = placement;
    args.row0          = frame;
    args.col0          = xsheet->getColumnCount();
    if (!IoCmd::loadResources(args, false)) return false;
    app->getCurrentColumn()->setColumnIndex(args.col0);
    return true;
  } catch (const TException &error) {
    DVGui::error(QString::fromStdWString(error.getMessage()));
  } catch (const std::exception &error) {
    DVGui::error(QString::fromUtf8(error.what()));
  }
  return false;
}

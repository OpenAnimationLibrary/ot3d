#include "amactionmenu.h"

#include "amaction.h"
#include "functioneditortracks.h"
#include "historytypes.h"
#include "tapp.h"
#include "texception.h"
#include "toonz/tframehandle.h"
#include "toonz/toonzscene.h"
#include "toonz/tscenehandle.h"
#include "toonz/txshamlevel.h"
#include "toonz/txsheet.h"
#include "toonz/txsheethandle.h"
#include "toonz/txshlevelcolumn.h"
#include "toonzqt/dvdialog.h"
#include "tundo.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QSaveFile>

#include <exception>
#include <utility>

namespace {

struct ActionTarget {
  TXsheet *xsheet;
  TXshLevelColumnP column;
  TXshLevelP level;
  int row;

  bool isCurrent() const {
    auto *app       = TApp::instance();
    const int index = column->getIndex();
    return app->getCurrentXsheet()->getXsheet() == xsheet &&
           !app->getCurrentFrame()->isPlaying() && index >= 0 &&
           xsheet->getColumn(index) == column.getPointer() &&
           !column->isLocked() &&
           xsheet->getCell(row, index).m_level.getPointer() ==
               level.getPointer();
  }

  AMActionBinding binding() const {
    auto action = column->getAMAction();
    if (action.level.getPointer() != level.getPointer())
      return AMActionBinding();
    return action;
  }

  QString modelPath() const {
    return xsheet->getScene()->decodeFilePath(level->getPath()).getQString();
  }
};

class UndoAMActionBinding final : public TUndo {
  TXshLevelColumnP m_column;
  ToonzScene *m_scene;
  AMActionBinding m_before, m_after;
  QString m_name;

  void apply(const AMActionBinding &binding) const {
    auto *app = TApp::instance();
    if (app->getCurrentScene()->getScene() != m_scene) return;
    auto *xsheet    = m_column->getXsheet();
    const int index = m_column->getIndex();
    if (!xsheet || index < 0 ||
        xsheet->getColumn(index) != m_column.getPointer())
      return;

    m_column->setAMAction(binding);
    app->getCurrentScene()->notifySceneChanged();
    app->getCurrentXsheet()->notifyXsheetChanged();
  }

public:
  UndoAMActionBinding(TXshLevelColumn *column, AMActionBinding after,
                      QString name)
      : m_column(column)
      , m_scene(column->getXsheet()->getScene())
      , m_before(column->getAMAction())
      , m_after(std::move(after))
      , m_name(std::move(name)) {}

  void undo() const override { apply(m_before); }
  void redo() const override { apply(m_after); }
  int getSize() const override {
    const auto size = [](const std::shared_ptr<const otam::Action> &action) {
      if (!action) return std::size_t(0);
      constexpr auto overhead = 4 * sizeof(void *);
      std::size_t bytes       = sizeof(*action) + action->defaultModel.size();
      for (const auto &channel : action->channels) {
        const auto &keys = channel.second.keys;
        bytes += sizeof(channel) + overhead +
                 keys.size() *
                     (sizeof(std::pair<const double, otam::Vec3>) + overhead);
      }
      return bytes;
    };
    const auto before =
        m_before.action != m_after.action ? size(m_before.action) : 0;
    return int(sizeof(*this) + before + size(m_after.action));
  }
  QString getHistoryString() override { return m_name; }
  int getHistoryType() override { return HistoryType::Xsheet; }
};

void changeBinding(const ActionTarget &target, AMActionBinding binding,
                   const QString &name) {
  changeAMActionBinding(target.column.getPointer(), std::move(binding), name);
}

bool isActionPath(const QFileInfo &info) {
  return info.suffix().compare(QStringLiteral("act"), Qt::CaseInsensitive) == 0;
}

QString chooseSavePath(QWidget *parent, const QString &title,
                       const QString &suggested) {
  const auto filter = QObject::tr("Animation:Master Actions (*.act)");
  QString path =
      QFileDialog::getSaveFileName(parent, title, suggested, filter, nullptr,
                                   QFileDialog::DontConfirmOverwrite);
  if (path.isEmpty()) return {};

  if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".act");
  const QFileInfo info(path);
  // QSaveFile can follow a symlink to another file. Only write action paths.
  if (!isActionPath(info) ||
      (info.isSymLink() &&
       !isActionPath(QFileInfo(info.canonicalFilePath())))) {
    DVGui::warning(QObject::tr("Choose a file with the .act extension."));
    return {};
  }

  if (info.exists() &&
      DVGui::MsgBox(QObject::tr("Overwrite the action file %1?").arg(path),
                    QObject::tr("Overwrite"), QObject::tr("Cancel"), 1,
                    parent) != 1)
    return {};

  return info.absoluteFilePath();
}

void writeActionFile(const QString &path, const otam::Action &action) {
  const auto bytes = otam::writeAction(action);
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) ||
      file.write(bytes.data(), qint64(bytes.size())) != qint64(bytes.size()) ||
      !file.commit())
    throw TException(QObject::tr("Could not save the A:M action.\n%1")
                         .arg(file.errorString())
                         .toStdWString());
}

otam::Action readActionFile(const QString &path) {
  if (!QFileInfo(path).isFile())
    throw TException(
        QObject::tr("Choose an existing .act file.").toStdWString());
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    throw TException(QObject::tr("Could not open the A:M action.\n%1")
                         .arg(file.errorString())
                         .toStdWString());

  const auto limit = qint64(otam::Limits{}.bytes);
  if (file.size() > limit)
    throw TException(QObject::tr("The A:M action exceeds the file size limit.")
                         .toStdWString());
  const auto bytes = file.read(limit + 1);
  if (file.error() != QFileDevice::NoError || bytes.size() > limit)
    throw TException(QObject::tr("Could not read the A:M action.\n%1")
                         .arg(file.errorString())
                         .toStdWString());
  return otam::parseAction(bytes.toStdString());
}

void attachAction(const ActionTarget &target, const QString &path,
                  std::shared_ptr<const otam::Action> action,
                  const QString &name) {
  changeBinding(target, {target.level, TFilePath(path), target.row, action},
                name);
  TApp::instance()->getCurrentFrame()->setFrame(target.row);
}

void createAction(const ActionTarget &target, QWidget *parent) {
  const QFileInfo modelFile(target.modelPath());
  const QString suggested = modelFile.absoluteDir().filePath(
      modelFile.completeBaseName() + QStringLiteral(".act"));
  const auto name = QObject::tr("Create A:M Action");
  const auto path = chooseSavePath(parent, name, suggested);
  if (path.isEmpty() || !target.isCurrent()) return;
  const auto model = target.level->getAMLevel()->editableModel();
  if (!model) return;

  auto action          = std::make_shared<otam::Action>();
  action->defaultModel = model->name.empty()
                             ? modelFile.completeBaseName().toStdString()
                             : model->name;
  writeActionFile(path, *action);
  attachAction(target, path, std::move(action), name);
}

void loadAction(const ActionTarget &target, QWidget *parent) {
  const auto name    = QObject::tr("Load A:M Action");
  const QString path = QFileDialog::getOpenFileName(
      parent, name, QFileInfo(target.modelPath()).absolutePath(),
      QObject::tr("Animation:Master Actions (*.act)"));
  if (path.isEmpty() || !target.isCurrent()) return;
  const auto model = target.level->getAMLevel()->editableModel();
  if (!model) return;
  if (!isActionPath(QFileInfo(path)))
    throw TException(
        QObject::tr("Choose a file with the .act extension.").toStdWString());

  auto action = std::make_shared<otam::Action>(readActionFile(path));
  otam::validateAction(*action, *model);
  attachAction(target, QFileInfo(path).absoluteFilePath(), std::move(action),
               name);
}

void saveAction(const ActionTarget &target, QWidget *parent) {
  const auto current = target.binding();
  if (!current.action) return;
  const auto path = chooseSavePath(parent, QObject::tr("Save A:M Action"),
                                   current.path.getQString());
  if (path.isEmpty() || !target.isCurrent()) return;
  auto binding = target.binding();
  if (binding.action != current.action || binding.path != current.path ||
      binding.startRow != current.startRow)
    return;

  writeActionFile(path, *binding.action);
  const TFilePath newPath(path);
  if (newPath == binding.path) return;
  binding.path = newPath;
  changeBinding(target, std::move(binding), QObject::tr("Set A:M Action File"));
}

void runActionCommand(void (*command)(const ActionTarget &, QWidget *),
                      const ActionTarget &target, QWidget *parent) {
  if (!target.isCurrent()) return;
  try {
    command(target, parent);
  } catch (const TException &e) {
    DVGui::error(QString::fromStdWString(e.getMessage()));
  } catch (const std::exception &e) {
    DVGui::error(QString::fromUtf8(e.what()));
  }
}

}  // namespace

void changeAMActionBinding(TXshLevelColumn *column, AMActionBinding binding,
                           const QString &name) {
  auto undo =
      std::make_unique<UndoAMActionBinding>(column, std::move(binding), name);
  undo->redo();
  TUndoManager::manager()->add(undo.release());
}

void reloadAMActionFromFile(TXshLevelColumn *column, QWidget *parent) {
  TXshLevelColumnP owner(column);
  auto *app    = TApp::instance();
  auto *xsheet = app->getCurrentXsheet()->getXsheet();
  if (!xsheet || owner->getXsheet() != xsheet || owner->isLocked() ||
      app->getCurrentFrame()->isPlaying())
    return;

  const auto current = owner->getAMAction();
  if (!current.action || current.path.isEmpty()) return;
  auto *level = current.level->getAMLevel();
  auto model  = level ? level->editableModel() : nullptr;
  if (!model) return;

  const QString path =
      xsheet->getScene()->decodeFilePath(current.path).getQString();
  try {
    auto source = std::make_shared<otam::Action>(readActionFile(path));
    otam::validateAction(*source, *model);
    if (otam::writeAction(*source) == otam::writeAction(*current.action))
      return;
    if (QMessageBox::question(
            parent, QObject::tr("Reload A:M Action"),
            QObject::tr("Replace the attached Action with the keys from %1?")
                .arg(QDir::toNativeSeparators(path)),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes)
      return;

    if (app->getCurrentXsheet()->getXsheet() != xsheet ||
        owner->getXsheet() != xsheet || owner->isLocked() ||
        app->getCurrentFrame()->isPlaying())
      return;
    auto binding = owner->getAMAction();
    if (binding.action != current.action || binding.path != current.path ||
        binding.startRow != current.startRow ||
        binding.level != current.level || level->editableModel() != model)
      return;
    binding.action = std::move(source);
    changeAMActionBinding(owner.getPointer(), std::move(binding),
                          QObject::tr("Reload A:M Action"));
  } catch (const TException &error) {
    DVGui::warning(QString::fromStdWString(error.getMessage()));
  } catch (const std::exception &error) {
    DVGui::warning(QString::fromUtf8(error.what()));
  }
}

void addAMActionMenu(QMenu &menu, TXsheet *xsheet, int row, int column) {
  auto *app   = TApp::instance();
  auto *scene = app->getCurrentScene()->getScene();
  if (!xsheet || !scene || xsheet != scene->getTopXsheet() || row < 0 ||
      column < 0 || app->getCurrentFrame()->isPlaying())
    return;

  const auto cell = xsheet->getCell(row, column);
  auto *level     = cell.isEmpty() ? nullptr : cell.m_level->getAMLevel();
  auto *xcol      = xsheet->getColumn(column);
  if (!level || !xcol || !xcol->getLevelColumn()) return;

  const ActionTarget target{xsheet, TXshLevelColumnP(xcol->getLevelColumn()),
                            cell.m_level, row};
  auto *actionMenu = menu.addMenu(QObject::tr("Action"));
  actionMenu->setEnabled(!xcol->isLocked());

  const bool editable = bool(level->editableModel());
  QWidget *parent     = menu.parentWidget();
  auto *create        = actionMenu->addAction(
             QObject::tr("Create Action..."), actionMenu,
             [parent, target] { runActionCommand(createAction, target, parent); });
  create->setEnabled(editable);
  auto *load = actionMenu->addAction(
      QObject::tr("Load Action..."), actionMenu,
      [parent, target] { runActionCommand(loadAction, target, parent); });
  load->setEnabled(editable);

  const auto attached = target.binding();
  if (!attached.action) return;
  actionMenu->addSeparator();
  auto *show = actionMenu->addAction(
      QObject::tr("Show Control Point in Function Editor…"), actionMenu,
      [parent, target] {
        if (!target.isCurrent()) return;
        const auto binding = target.binding();
        if (!binding.action || binding.action->channels.empty()) return;
        QStringList points;
        for (const auto &entry : binding.action->channels)
          points << QString::number(qlonglong(entry.first));
        bool ok          = false;
        const auto point = QInputDialog::getItem(
            parent, QObject::tr("A:M Action Curves"),
            QObject::tr("Control Point ID"), points, 0, false, &ok);
        if (ok && target.isCurrent() &&
            target.binding().action == binding.action)
          showAMActionInFunctionEditor(target.column.getPointer(),
                                       {point.toLongLong()});
      });
  show->setEnabled(!attached.action->channels.empty());
  actionMenu
      ->addAction(QObject::tr("Reload Action from File…"), actionMenu,
                  [parent, target] {
                    if (target.isCurrent())
                      reloadAMActionFromFile(target.column.getPointer(),
                                             parent);
                  })
      ->setEnabled(!attached.path.isEmpty());
  actionMenu->addAction(
      QObject::tr("Save Action..."), actionMenu,
      [parent, target] { runActionCommand(saveAction, target, parent); });
  actionMenu
      ->addAction(QObject::tr("Set Start to This Frame"), actionMenu,
                  [target] {
                    if (!target.isCurrent()) return;
                    auto binding = target.binding();
                    if (!binding.action || binding.startRow == target.row)
                      return;
                    binding.startRow = target.row;
                    changeBinding(target, std::move(binding),
                                  QObject::tr("Move A:M Action Start"));
                    TApp::instance()->getCurrentFrame()->setFrame(target.row);
                  })
      ->setEnabled(attached.startRow != row);
  actionMenu->addAction(QObject::tr("Remove Action"), actionMenu, [target] {
    if (target.isCurrent() && target.binding().action)
      changeBinding(target, AMActionBinding(),
                    QObject::tr("Remove A:M Action"));
  });
}

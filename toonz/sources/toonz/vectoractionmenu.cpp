#include "vectoractionmenu.h"

#include "functioneditortracks.h"
#include "tapp.h"
#include "texception.h"
#include "toonz/tframehandle.h"
#include "toonz/toonzscene.h"
#include "toonz/txshcell.h"
#include "toonz/txshlevelcolumn.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txsheet.h"
#include "toonz/txsheethandle.h"
#include "toonz/vectoraction.h"
#include "toonzqt/dvdialog.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QSaveFile>

namespace {

struct Target {
  TXsheet *xsheet;
  TXshLevelColumnP column;
  TXshCell cell;
  int row;

  bool isCurrent() const {
    auto *app       = TApp::instance();
    const int index = column->getIndex();
    return app->getCurrentXsheet()->getXsheet() == xsheet && index >= 0 &&
           xsheet->getColumn(index) == column.getPointer() &&
           !app->getCurrentFrame()->isPlaying() && !column->isLocked() &&
           xsheet->getCell(row, index) == cell;
  }

  QString path(const TFilePath &file) const {
    return xsheet->getScene()->decodeFilePath(file).getQString();
  }

  void change(const VectorActionBinding &binding, const QString &label) const {
    auto *app = TApp::instance();
    changeVectorActionBinding(column.getPointer(), binding,
                              app->getCurrentScene(), app->getCurrentXsheet(),
                              label);
  }
};

bool isActionPath(const QString &path) {
  return QFileInfo(path).suffix().compare("vact", Qt::CaseInsensitive) == 0;
}

QString chooseSavePath(QWidget *parent, const QString &title,
                       const QString &suggested) {
  QString path = QFileDialog::getSaveFileName(
      parent, title, suggested, QObject::tr("Vector Actions (*.vact)"), nullptr,
      QFileDialog::DontConfirmOverwrite);
  if (path.isEmpty()) return {};
  if (QFileInfo(path).suffix().isEmpty()) path += ".vact";
  const QFileInfo info(path);
  if (!isActionPath(path) ||
      (info.isSymLink() && !isActionPath(info.canonicalFilePath())))
    throw TException(
        QObject::tr("Choose a file with the .vact extension.").toStdWString());
  if (info.exists() &&
      QMessageBox::question(parent, title,
                            QObject::tr("Overwrite %1?").arg(path)) !=
          QMessageBox::Yes)
    return {};
  return info.absoluteFilePath();
}

void writeFile(const QString &path, const VectorAction &action) {
  const auto bytes = action.write();
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
      !file.commit())
    throw TException(QObject::tr("Could not save the vector Action.\n%1")
                         .arg(file.errorString())
                         .toStdWString());
}

std::shared_ptr<const VectorAction> readFile(const QString &path,
                                             const TVectorImageP &image) {
  QFile file(path);
  if (!isActionPath(path) || !QFileInfo(path).isFile() ||
      !file.open(QIODevice::ReadOnly))
    throw TException(
        QObject::tr("Choose an existing .vact file.").toStdWString());
  const auto bytes = file.read(VectorAction::MaxBytes + 1);
  if (file.error() != QFileDevice::NoError)
    throw TException(file.errorString().toStdWString());
  auto action = std::make_shared<const VectorAction>(VectorAction::read(bytes));
  if (!action->matches(image))
    throw TException(QObject::tr("The vector Action requires the same stroke "
                                 "order and control point layout as its source "
                                 "drawing.")
                         .toStdWString());
  return action;
}

bool confirmReplace(const Target &target, QWidget *parent,
                    const QString &title) {
  return !target.column->getVectorAction().action ||
         QMessageBox::question(parent, title,
                               QObject::tr("Replace this column's attached "
                                           "vector Action?")) ==
             QMessageBox::Yes;
}

template <typename Command>
void runCommand(const Target &target, Command command) {
  if (!target.isCurrent()) return;
  try {
    command();
  } catch (const TException &error) {
    DVGui::warning(QString::fromStdWString(error.getMessage()));
  }
}

}  // namespace

void reloadVectorActionFromFile(TXshLevelColumn *column, QWidget *parent) {
  auto *app          = TApp::instance();
  auto *xsheet       = app->getCurrentXsheet()->getXsheet();
  const int index    = column->getIndex();
  const auto current = column->getVectorAction();
  if (!xsheet || column->getXsheet() != xsheet || index < 0 ||
      xsheet->getColumn(index) != column || column->isLocked() ||
      app->getCurrentFrame()->isPlaying() || !current.action ||
      current.path.isEmpty())
    return;

  const TXshLevelColumnP owner(column);
  const auto title = QObject::tr("Reload Vector Action");
  try {
    const TXshCell cell(current.level, current.fid);
    const auto path = xsheet->getScene()->decodeFilePath(current.path);
    auto binding    = current;
    binding.action =
        readFile(path.getQString(), TVectorImageP(cell.getImage(false)));
    if (QMessageBox::question(parent, title,
                              QObject::tr("Replace the attached keys with the "
                                          "saved keys from %1?")
                                  .arg(path.getQString())) !=
            QMessageBox::Yes ||
        app->getCurrentXsheet()->getXsheet() != xsheet ||
        owner->getXsheet() != xsheet || owner->getIndex() < 0 ||
        xsheet->getColumn(owner->getIndex()) != owner.getPointer() ||
        app->getCurrentFrame()->isPlaying() || owner->isLocked() ||
        owner->getVectorAction().revision != current.revision)
      return;
    changeVectorActionBinding(owner.getPointer(), binding,
                              app->getCurrentScene(), app->getCurrentXsheet(),
                              title);
  } catch (const TException &error) {
    DVGui::warning(QString::fromStdWString(error.getMessage()));
  }
}

void addVectorActionMenu(QMenu &menu, TXsheet *xsheet, int row, int index) {
  auto *app = TApp::instance();
  if (!xsheet || row < 0 || index < 0 || app->getCurrentFrame()->isPlaying())
    return;

  const auto cell = xsheet->getCell(row, index);
  auto *xcolumn   = xsheet->getColumn(index);
  auto *column    = xcolumn ? xcolumn->getLevelColumn() : nullptr;
  if (!column || !cell.m_level || cell.m_level->getType() != PLI_XSHLEVEL)
    return;

  const Target target{xsheet, TXshLevelColumnP(column), cell, row};
  auto *actions = menu.addMenu(QObject::tr("Action"));
  actions->setEnabled(!column->isLocked());
  auto *parent = menu.parentWidget();

  const auto attach = [target](const QString &path,
                               std::shared_ptr<const VectorAction> action,
                               const QString &label) {
    target.change({target.cell.m_level, target.cell.m_frameId, TFilePath(path),
                   target.row, std::move(action)},
                  label);
    TApp::instance()->getCurrentFrame()->setFrame(target.row);
  };

  actions->addAction(
      QObject::tr("Create Action…"), actions, [target, parent, attach] {
        runCommand(target, [&] {
          const auto title = QObject::tr("Create Vector Action");
          const auto suggested =
              target.path(target.cell.m_level->getPath().withType("vact"));
          if (!confirmReplace(target, parent, title) || !target.isCurrent())
            return;
          const auto path = chooseSavePath(parent, title, suggested);
          if (path.isEmpty() || !target.isCurrent()) return;
          const TVectorImageP image(target.cell.getImage(false));
          if (!image || !image->getStrokeCount())
            throw TException(
                QObject::tr("Choose a vector drawing with strokes.")
                    .toStdWString());
          auto action = std::make_shared<const VectorAction>(
              VectorAction::forDrawing(image));
          writeFile(path, *action);
          attach(path, std::move(action), title);
        });
      });

  actions->addAction(
      QObject::tr("Load Action…"), actions, [target, parent, attach] {
        runCommand(target, [&] {
          const auto title = QObject::tr("Load Vector Action");
          if (!confirmReplace(target, parent, title) || !target.isCurrent())
            return;
          const auto path = QFileDialog::getOpenFileName(
              parent, title,
              QFileInfo(target.path(target.cell.m_level->getPath()))
                  .absolutePath(),
              QObject::tr("Vector Actions (*.vact)"));
          if (path.isEmpty() || !target.isCurrent()) return;
          auto action =
              readFile(path, TVectorImageP(target.cell.getImage(false)));
          attach(path, std::move(action), title);
        });
      });

  const auto current = column->getVectorAction();
  if (!current.matches(cell)) return;
  actions->addSeparator();
  actions->addAction(QObject::tr("Save Action…"), actions, [target, parent] {
    runCommand(target, [&] {
      auto binding = target.column->getVectorAction();
      if (!binding.matches(target.cell)) return;
      const auto path = chooseSavePath(
          parent, QObject::tr("Save Vector Action"), target.path(binding.path));
      if (path.isEmpty() || !target.isCurrent() ||
          binding.revision != target.column->getVectorAction().revision)
        return;
      writeFile(path, *binding.action);
      const TFilePath saved(path);
      if (saved == binding.path) return;
      binding.path = saved;
      target.change(binding, QObject::tr("Set Vector Action File"));
    });
  });
  actions
      ->addAction(QObject::tr("Reload Action from File…"), actions,
                  [target, parent] {
                    if (target.isCurrent())
                      reloadVectorActionFromFile(target.column.getPointer(),
                                                 parent);
                  })
      ->setEnabled(!current.path.isEmpty());
  actions
      ->addAction(
          QObject::tr("Set Start to This Frame"), actions,
          [target] {
            if (!target.isCurrent()) return;
            auto binding = target.column->getVectorAction();
            if (!binding.matches(target.cell) || binding.startRow == target.row)
              return;
            binding.startRow = target.row;
            target.change(binding, QObject::tr("Move Vector Action Start"));
          })
      ->setEnabled(current.startRow != row);
  actions->addAction(QObject::tr("Remove Action"), actions, [target] {
    if (target.isCurrent() &&
        target.column->getVectorAction().matches(target.cell))
      target.change({}, QObject::tr("Remove Vector Action"));
  });
  actions
      ->addAction(
          QObject::tr("Show Control Point in Function Editor…"), actions,
          [target, parent] {
            if (!target.isCurrent()) return;
            const auto binding = target.column->getVectorAction();
            if (!binding.matches(target.cell) ||
                binding.action->channels.empty())
              return;
            QStringList labels;
            std::vector<VectorAction::Point> points;
            for (const auto &[point, keys] : binding.action->channels) {
              labels << QObject::tr("Stroke %1, Point %2")
                            .arg(point.first + 1)
                            .arg(point.second + 1);
              points.push_back(point);
            }
            bool ok           = false;
            const auto choice = QInputDialog::getItem(
                parent, QObject::tr("Vector Action Curves"),
                QObject::tr("Control Point"), labels, 0, false, &ok);
            const int selected = labels.indexOf(choice);
            if (ok && selected >= 0 && target.isCurrent() &&
                target.column->getVectorAction().revision == binding.revision)
              showVectorActionInFunctionEditor(target.column.getPointer(),
                                               points[selected]);
          })
      ->setEnabled(!current.action->channels.empty());
}

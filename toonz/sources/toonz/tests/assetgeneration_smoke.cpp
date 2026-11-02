// SPDX-License-Identifier: BSD-3-Clause
#include "../assetgenerationpanel.h"
#include "../tapp.h"
#include "tundo.h"
#include "tparamcontainer.h"
#include "tnotanimatableparam.h"
#include "toonz/preferences.h"
#include "toonz/tcolumnfx.h"
#include "toonz/toonzscene.h"
#include "toonz/tscenehandle.h"
#include "toonz/txsheet.h"
#include "toonz/txsheethandle.h"
#include "toonz/txshzeraryfxcolumn.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextEdit>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
template <class T>
T *control(AssetGenerationPanel &panel, const char *name) {
  auto *value = panel.findChild<T *>(name);
  check(value != nullptr, "Native panel control is missing");
  return value;
}
void waitFor(const std::function<bool()> &ready) {
  QElapsedTimer timer;
  timer.start();
  while (!ready()) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
    check(timer.elapsed() < 20000, "Native asset job did not finish");
  }
}
}  // namespace

int runAssetGenerationSmoke(const QString &python, const QString &fixture) {
  auto *prefs          = Preferences::instance();
  const auto oldPython = prefs->getStringValue(ot3dPythonPath);
  const auto oldSculpt = prefs->getStringValue(ot3dSculptForgePath);
  struct Restore {
    Preferences *prefs;
    QString python, sculpt;
    ~Restore() {
      prefs->setValue(ot3dPythonPath, python, false);
      prefs->setValue(ot3dSculptForgePath, sculpt, false);
      TUndoManager::manager()->reset();
    }
  } restore{prefs, oldPython, oldSculpt};
  try {
    prefs->setValue(ot3dPythonPath, python, false);
    prefs->setValue(ot3dSculptForgePath, fixture, false);
    QTemporaryDir directory;
    check(directory.isValid(), "Cannot create native job test directory");
    AssetGenerationPanel panel;
    auto *probe    = control<QPushButton>(panel, "assetCheck");
    auto *generate = control<QPushButton>(panel, "assetGenerate");
    auto *cancel   = control<QPushButton>(panel, "assetCancel");
    auto *import   = control<QPushButton>(panel, "assetImport");
    auto *send     = control<QCheckBox>(panel, "assetSend");
    auto *prompt   = control<QTextEdit>(panel, "assetPrompt");
    auto *output   = control<QLineEdit>(panel, "assetOutput");
    auto *status   = control<QLabel>(panel, "assetStatus");
    control<QComboBox>(panel, "assetMode")->setCurrentIndex(1);
    probe->click();
    waitFor([&] { return probe->isEnabled(); });
    send->setChecked(true);
    check(generate->isEnabled(), "Local integration did not become ready");
    auto run = [&](const QString &text, const QString &filename) {
      prompt->setPlainText(text);
      output->setText(directory.filePath(filename));
      generate->click();
    };
    run("quota", "failure.glb");
    waitFor([&] { return generate->isEnabled(); });
    check(status->text().contains("quota") && !import->isEnabled(),
          "Provider failure was not displayed");
    run("wait", "cancelled.glb");
    cancel->click();
    waitFor([&] { return generate->isEnabled(); });
    check(!QFileInfo::exists(output->text()) && !import->isEnabled(),
          "Cancelled job became importable");
    run("cube", "changed-scene.glb");
    TApp::instance()->getCurrentScene()->notifySceneSwitched();
    waitFor([&] { return generate->isEnabled(); });
    check(QFileInfo::exists(output->text()) && !import->isEnabled(),
          "Scene switch did not invalidate the import target");
    run("cube", "asset.glb");
    waitFor([&] { return generate->isEnabled(); });
    check(import->isEnabled(), "Completed native job is not importable");
    auto *xsheet      = TApp::instance()->getCurrentXsheet()->getXsheet();
    const int history = TUndoManager::manager()->getHistoryCount();
    import->click();
    check(TUndoManager::manager()->getHistoryCount() == history + 1,
          "Native import did not register Undo");
    auto *column = xsheet->getColumn(0)->getZeraryFxColumn();
    check(column != nullptr, "Native import did not insert a GLB FX column");
    auto *fx = column->getZeraryColumnFx()->getZeraryFx();
    auto *path =
        dynamic_cast<TStringParam *>(fx->getParams()->getParam("modelFile"));
    check(path && path->getValue() == output->text().toStdWString(),
          "Imported FX lost its retained source path");
    const TFilePath scenePath(directory.filePath("scene.tnz").toStdWString());
    TApp::instance()->getCurrentScene()->getScene()->save(scenePath, nullptr,
                                                          false);
    ToonzScene reopened;
    reopened.loadTnzFile(scenePath);
    auto *restored = reopened.getXsheet()->getColumn(0)->getZeraryFxColumn();
    check(restored != nullptr, "GLB FX did not survive scene save/reopen");
    auto *restoredPath = dynamic_cast<TStringParam *>(
        restored->getZeraryColumnFx()->getZeraryFx()->getParams()->getParam(
            "modelFile"));
    check(restoredPath && restoredPath->getValue() == path->getValue(),
          "Scene reopen changed the generated GLB source");
    check(TUndoManager::manager()->undo(),
          "Native GLB import could not be undone");
    check(xsheet->isColumnEmpty(0), "Undo left generated GLB cells exposed");
    std::cout << "PASS native asset panel probe/failure/cancel/scene "
                 "guard/import/undo/save/reopen\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Asset panel smoke failed: " << error.what() << '\n';
    return 1;
  }
}

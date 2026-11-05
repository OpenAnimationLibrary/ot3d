// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "pane.h"
#include <QByteArray>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTextEdit;
class QShowEvent;
class ToonzScene;
class TXsheet;

class AssetGenerationPanel final : public TPanel {
  Q_OBJECT
  QComboBox *m_mode, *m_model;
  QTextEdit *m_prompt;
  QLineEdit *m_image, *m_output;
  QLabel *m_status, *m_terms;
  QCheckBox *m_send;
  QSpinBox *m_timeout;
  QPushButton *m_check, *m_generate, *m_cancel, *m_import;
  QProcess m_process;
  QTimer m_killTimer, m_watchdog;
  std::unique_ptr<QTemporaryDir> m_session;
  QByteArray m_stdout, m_stderr;
  QJsonObject m_catalog;
  QString m_python, m_sculptforge, m_asset, m_digest, m_submittedOutput;
  ToonzScene *m_originScene = nullptr;
  TXsheet *m_originXsheet   = nullptr;
  bool m_busy = false, m_probing = false, m_ready = false;
  bool m_targetValid = false, m_cancelling = false;

  void refreshConfiguration();
  void refreshModels();
  void refreshControls();
  void launch(bool probe);
  void finish(int exitCode, QProcess::ExitStatus status);
  void cancel();
  void importAsset();

protected:
  void showEvent(QShowEvent *event) override;

public:
  AssetGenerationPanel(QWidget *parent = nullptr);
  ~AssetGenerationPanel() override;
};

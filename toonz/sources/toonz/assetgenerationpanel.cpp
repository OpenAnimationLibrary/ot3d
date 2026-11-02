// SPDX-License-Identifier: BSD-3-Clause
#include "assetgenerationpanel.h"

#include "floatingpanelcommand.h"
#include "tapp.h"
#include "glbloader.h"
#include "tfx.h"
#include "tparam.h"
#include "tparamcontainer.h"
#include "tnotanimatableparam.h"
#include "toonz/fxcommand.h"
#include "toonz/preferences.h"
#include "toonz/tcolumnhandle.h"
#include "toonz/tframehandle.h"
#include "toonz/toonzscene.h"
#include "toonz/tscenehandle.h"
#include "toonz/txsheet.h"
#include "toonz/txsheethandle.h"
#include "toonzqt/dvdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QShowEvent>
#include <QSpinBox>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>
#include <filesystem>

namespace {
const char *const Command = "MI_AssetGeneration";

bool executable(const QString &path) {
  const QFileInfo file(path);
  return file.isAbsolute() && file.isFile() && file.isExecutable();
}

bool writeFile(const QString &path, const QByteArray &bytes) {
  QSaveFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() &&
         file.commit();
}
}  // namespace

AssetGenerationPanel::AssetGenerationPanel(QWidget *parent) : TPanel(parent) {
  setPanelType("AssetGeneration");
  setWindowTitle(tr("Asset Generation"));
  allowMultipleInstances(false);
  auto *body        = new QWidget(this);
  auto *layout      = new QVBoxLayout(body);
  auto *description = new QLabel(
      tr("Generate a new 3D asset with SculptForge and Hugging Face. "
         "Configure Python 3.11+ and SculptForge CLI in Preferences > Addons."),
      body);
  description->setWordWrap(true);
  layout->addWidget(description);
  m_check = new QPushButton(tr("Check Integration (Local Only)"), body);
  layout->addWidget(m_check);
  auto *form = new QFormLayout;
  m_mode     = new QComboBox(body);
  m_mode->addItem(tr("Image to 3D"), "image");
  m_mode->addItem(tr("Text to 3D"), "text");
  m_model  = new QComboBox(body);
  m_prompt = new QTextEdit(body);
  m_prompt->setAcceptRichText(false);
  m_prompt->setMaximumHeight(120);
  m_image      = new QLineEdit(body);
  m_output     = new QLineEdit(body);
  auto pathRow = [&](QLineEdit *field, bool saving) {
    auto *row       = new QWidget(body);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->addWidget(field);
    auto *browse = new QPushButton(tr("Browse..."), row);
    rowLayout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this, field, saving]() {
      const QString path =
          saving ? QFileDialog::getSaveFileName(this, tr("New GLB Asset"),
                                                field->text(),
                                                tr("GLB Models (*.glb)"))
                 : QFileDialog::getOpenFileName(
                       this, tr("Source Image"), field->text(),
                       tr("Images (*.png *.jpg *.jpeg *.webp)"));
      if (!path.isEmpty()) field->setText(path);
    });
    return row;
  };
  form->addRow(tr("Input:"), m_mode);
  form->addRow(tr("Model:"), m_model);
  form->addRow(tr("Image:"), pathRow(m_image, false));
  form->addRow(tr("Description:"), m_prompt);
  form->addRow(tr("New GLB:"), pathRow(m_output, true));
  m_timeout = new QSpinBox(body);
  m_timeout->setRange(30, 7200);
  m_timeout->setValue(1800);
  m_timeout->setSuffix(tr(" seconds"));
  form->addRow(tr("Timeout:"), m_timeout);
  layout->addLayout(form);
  m_terms = new QLabel(body);
  m_terms->setWordWrap(true);
  m_terms->setOpenExternalLinks(true);
  layout->addWidget(m_terms);
  m_send = new QCheckBox(
      tr("Send this input to the selected Hugging Face service"), body);
  layout->addWidget(m_send);
  auto *buttons = new QHBoxLayout;
  m_generate    = new QPushButton(tr("Generate New Asset"), body);
  m_cancel      = new QPushButton(tr("Cancel"), body);
  m_import      = new QPushButton(tr("Import GLB Model FX"), body);
  buttons->addWidget(m_generate);
  buttons->addWidget(m_cancel);
  buttons->addWidget(m_import);
  layout->addLayout(buttons);
  m_status = new QLabel(body);
  m_status->setWordWrap(true);
  m_status->setTextFormat(Qt::PlainText);
  m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_status);
  layout->addStretch();
  setWidget(body);
  resize(500, 560);
  connect(m_check, &QPushButton::clicked, this, [this]() { launch(true); });
  connect(m_generate, &QPushButton::clicked, this, [this]() { launch(false); });
  connect(m_cancel, &QPushButton::clicked, this, &AssetGenerationPanel::cancel);
  connect(m_import, &QPushButton::clicked, this,
          &AssetGenerationPanel::importAsset);
  connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this]() { refreshModels(); });
  connect(
      m_model, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
      [this]() {
        m_send->setChecked(false);
        const auto metadata = m_model->currentData().toJsonObject();
        m_terms->setText(
            tr("Review <a href=\"%1\">model terms</a> and the "
               "<a href=\"%2\">hosted Space</a> before use. "
               "Service availability and quota can change.")
                .arg(metadata.value("license_url").toString().toHtmlEscaped(),
                     metadata.value("space_url").toString().toHtmlEscaped()));
        refreshControls();
      });
  connect(m_send, &QCheckBox::toggled, this, [this]() { refreshControls(); });
  connect(&m_process, &QProcess::readyReadStandardOutput, this, [this]() {
    m_stdout += m_process.readAllStandardOutput();
    if (m_stdout.size() > 128 * 1024) {
      m_stdout.truncate(128 * 1024);
      cancel();
      m_status->setText(tr("Worker response exceeded its size limit."));
    }
  });
  connect(&m_process, &QProcess::readyReadStandardError, this, [this]() {
    m_stderr = (m_stderr + m_process.readAllStandardError()).right(8192);
  });
  connect(&m_process,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          &AssetGenerationPanel::finish);
  connect(&m_process, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
              m_busy = false;
              m_watchdog.stop();
              m_ready = false;
              m_status->setText(
                  tr("Cannot launch Python. Check Preferences > Addons."));
              refreshControls();
            }
          });
  m_killTimer.setSingleShot(true);
  connect(&m_killTimer, &QTimer::timeout, this, [this]() {
    if (m_busy) m_process.kill();
  });
  m_watchdog.setSingleShot(true);
  connect(&m_watchdog, &QTimer::timeout, this, [this]() {
    cancel();
    m_status->setText(tr("Worker deadline exceeded. Cancelling..."));
  });
  connect(TApp::instance()->getCurrentScene(), &TSceneHandle::sceneSwitched,
          this, [this]() {
            m_targetValid = false;
            refreshControls();
          });
  connect(TApp::instance()->getCurrentXsheet(), &TXsheetHandle::xsheetSwitched,
          this, [this]() {
            m_targetValid = false;
            refreshControls();
          });
  connect(TApp::instance()->getCurrentScene(), &TSceneHandle::preferenceChanged,
          this, [this](const QString &) {
            if (!m_busy) refreshConfiguration();
          });
  refreshConfiguration();
}

AssetGenerationPanel::~AssetGenerationPanel() {
  if (m_busy) {
    cancel();
    if (!m_process.waitForFinished(5000)) {
      m_process.kill();
      m_process.waitForFinished(2000);
    }
  }
}

void AssetGenerationPanel::showEvent(QShowEvent *event) {
  TPanel::showEvent(event);
  if (!m_busy) refreshConfiguration();
}

void AssetGenerationPanel::refreshConfiguration() {
  auto *prefs            = Preferences::instance();
  const auto python      = prefs->getStringValue(ot3dPythonPath);
  const auto sculptforge = prefs->getStringValue(ot3dSculptForgePath);
  if (python != m_python || sculptforge != m_sculptforge) m_ready = false;
  m_python      = python;
  m_sculptforge = sculptforge;
  if (!executable(m_python) || !executable(m_sculptforge)) {
    m_ready = false;
    m_status->setText(
        tr("Select Python and SculptForge executable files in Preferences > "
           "Addons."));
  } else if (!m_ready) {
    m_status->setText(tr(
        "Configured. Choose Check Integration to verify local capabilities."));
  }
  refreshControls();
}

void AssetGenerationPanel::refreshModels() {
  m_model->clear();
  const auto models =
      m_catalog.value(m_mode->currentData().toString()).toArray();
  for (const auto &value : models) {
    const auto model = value.toObject();
    m_model->addItem(model.value("label").toString(), model);
  }
  const int trellis = m_mode->currentData().toString() == "image" ? 0 : -1;
  if (trellis == 0) {
    for (int i = 0; i < m_model->count(); ++i)
      if (m_model->itemData(i).toJsonObject().value("id").toString() ==
          "trellis-2")
        m_model->setCurrentIndex(i);
  }
  m_send->setChecked(false);
  refreshControls();
}

void AssetGenerationPanel::refreshControls() {
  const bool text = m_mode->currentData().toString() == "text";
  m_check->setEnabled(!m_busy && executable(m_python) &&
                      executable(m_sculptforge));
  m_mode->setEnabled(!m_busy);
  m_model->setEnabled(!m_busy && m_ready);
  m_prompt->setEnabled(!m_busy && text);
  m_image->setEnabled(!m_busy && !text);
  m_output->setEnabled(!m_busy);
  m_timeout->setEnabled(!m_busy);
  m_send->setEnabled(!m_busy && m_ready);
  m_generate->setEnabled(!m_busy && m_ready && m_send->isChecked() &&
                         m_model->count());
  m_cancel->setEnabled(m_busy && !m_cancelling);
  m_import->setEnabled(!m_busy && !m_asset.isEmpty() && m_targetValid);
}

void AssetGenerationPanel::launch(bool probing) {
  if (m_busy) return;
  refreshConfiguration();
  if (!executable(m_python) || !executable(m_sculptforge) ||
      (!probing && (!m_ready || !m_send->isChecked())))
    return;
  if (!probing) {
    if (QFileInfo(m_output->text())
                .suffix()
                .compare("glb", Qt::CaseInsensitive) != 0 ||
        !QFileInfo(m_output->text()).isAbsolute() ||
        QFileInfo::exists(m_output->text()) ||
        QFileInfo::exists(m_output->text() + ".ot3d.json")) {
      m_status->setText(
          tr("Choose a new absolute .glb filename. Existing assets are never "
             "overwritten."));
      return;
    }
    const bool text = m_mode->currentData().toString() == "text";
    if ((text && (m_prompt->toPlainText().trimmed().isEmpty() ||
                  m_prompt->toPlainText().size() > 16000)) ||
        (!text && !QFileInfo(m_image->text()).isFile())) {
      m_status->setText(
          tr("Choose an existing image or enter a description of up to 16000 "
             "characters."));
      return;
    }
  }
  m_session.reset(new QTemporaryDir);
  QFile embedded(":/ot3d/sculptforge_job.py");
  if (!m_session->isValid() || !embedded.open(QIODevice::ReadOnly) ||
      !writeFile(m_session->filePath("worker.py"), embedded.readAll())) {
    m_status->setText(tr("Cannot prepare the asset worker."));
    return;
  }
  QStringList args{m_session->filePath("worker.py"), "--sculptforge",
                   m_sculptforge, "--cancel-file",
                   m_session->filePath("cancel")};
  if (probing) {
    args << "--probe";
    m_ready = false;
  } else {
    QJsonObject request{
        {"sculptforge", m_sculptforge},
        {"model", m_model->currentData().toJsonObject().value("id")},
        {"output", m_output->text()},
        {"timeout", m_timeout->value()}};
    request.insert(
        m_mode->currentData().toString() == "text" ? "text" : "image",
        m_mode->currentData().toString() == "text" ? m_prompt->toPlainText()
                                                   : m_image->text());
    if (!writeFile(m_session->filePath("request.json"),
                   QJsonDocument(request).toJson())) {
      m_status->setText(tr("Cannot write the asset request."));
      return;
    }
    args << "--request" << m_session->filePath("request.json") << "--run";
    m_asset.clear();
    m_digest.clear();
    m_submittedOutput = m_output->text();
    m_originScene     = TApp::instance()->getCurrentScene()->getScene();
    m_originXsheet    = TApp::instance()->getCurrentXsheet()->getXsheet();
    m_targetValid     = true;
  }
  m_stdout.clear();
  m_stderr.clear();
  m_probing    = probing;
  m_busy       = true;
  m_cancelling = false;
  m_status->setText(probing ? tr("Checking local integration...")
                            : tr("Running generation..."));
  refreshControls();
  m_watchdog.start(probing ? 30000 : (m_timeout->value() + 30) * 1000);
  m_process.start(m_python, args);
}

void AssetGenerationPanel::cancel() {
  if (!m_busy || !m_session) return;
  m_cancelling = true;
  writeFile(m_session->filePath("cancel"), "cancel");
  m_killTimer.start(6000);
  m_status->setText(tr("Cancelling..."));
  refreshControls();
}

void AssetGenerationPanel::finish(int exitCode, QProcess::ExitStatus status) {
  m_killTimer.stop();
  m_watchdog.stop();
  m_stdout += m_process.readAllStandardOutput();
  m_busy = false;
  QJsonParseError parse;
  const auto result = QJsonDocument::fromJson(m_stdout, &parse).object();
  if (m_cancelling) {
    m_status->setText(tr("Cancelled. No asset was imported."));
  } else if (status != QProcess::NormalExit || exitCode != 0 ||
             parse.error != QJsonParseError::NoError ||
             result.contains("error")) {
    m_status->setText(result.value("error").toString(
        tr("Worker failed. Check Python 3.11+, SculptForge configuration and "
           "service availability.")));
  } else if (m_probing) {
    m_catalog = result.value("models").toObject();
    m_ready   = !m_catalog.isEmpty();
    refreshModels();
    m_status->setText(
        tr("Integration ready. No network generation was submitted."));
  } else if (result.value("status").toString() == "completed") {
    m_asset  = m_submittedOutput;
    m_digest = result.value("sha256").toString();
    m_status->setText(
        m_targetValid
            ? tr("Completed. Review the saved asset and choose Import GLB "
                 "Model FX.")
            : tr("Completed and saved. The target scene changed; import this "
                 "asset manually in the intended scene."));
  } else {
    m_status->setText(tr("Worker returned an unexpected result."));
  }
  m_cancelling = false;
  refreshControls();
}

void AssetGenerationPanel::importAsset() {
  auto *app = TApp::instance();
  if (m_busy || m_asset.isEmpty() || !m_targetValid ||
      app->getCurrentScene()->getScene() != m_originScene ||
      app->getCurrentXsheet()->getXsheet() != m_originXsheet)
    return;
  QFile file(m_asset);
  QCryptographicHash digest(QCryptographicHash::Sha256);
  if (!file.open(QIODevice::ReadOnly) || !digest.addData(&file) ||
      QString::fromLatin1(digest.result().toHex()) != m_digest) {
    m_status->setText(
        tr("The generated asset changed or is unavailable. Import refused."));
    return;
  }
#ifdef _WIN32
  const std::filesystem::path path(m_asset.toStdWString());
#else
  const auto path = std::filesystem::u8path(m_asset.toUtf8().toStdString());
#endif
  const auto loaded = otglb::load(path);
  if (!loaded || loaded.asset->meshes.empty()) {
    m_status->setText(tr("The OT3D GLB loader rejected this asset: %1")
                          .arg(QString::fromStdString(loaded.error)));
    return;
  }
  TFxP fx = TFx::create("STD_glbModelFx");
  auto *parameter =
      fx ? dynamic_cast<TStringParam *>(fx->getParams()->getParam("modelFile"))
         : nullptr;
  if (!parameter) {
    m_status->setText(tr("GLB Model FX is unavailable."));
    return;
  }
  parameter->setValue(m_asset.toStdWString());
  if (auto *colors =
          dynamic_cast<TIntEnumParam *>(fx->getParams()->getParam("colorMode")))
    colors->setValue(1);
  fx->setName(QFileInfo(m_asset).completeBaseName().toStdWString());
  TFxCommand::insertFx(fx.getPointer(), QList<TFxP>(),
                       QList<TFxCommand::Link>(), app,
                       app->getCurrentColumn()->getColumnIndex(),
                       app->getCurrentFrame()->getFrame());
  m_asset.clear();
  m_status->setText(
      tr("Imported as a new GLB Model FX. Use Undo to remove it."));
  refreshControls();
}

namespace {
class AssetGenerationFactory final : public TPanelFactory {
public:
  AssetGenerationFactory() : TPanelFactory("AssetGeneration") {}
  TPanel *createPanel(QWidget *parent) override {
    auto *panel = new AssetGenerationPanel(parent);
    panel->setObjectName(getPanelType());
    return panel;
  }
  void initialize(TPanel *) override {}
} assetGenerationFactory;

OpenFloatingPanel openAssetGeneration(Command, "AssetGeneration",
                                      QObject::tr("Asset Generation"));
}  // namespace

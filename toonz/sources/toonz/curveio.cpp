

#include "curveio.h"
#include "tsystem.h"
#include "tstream.h"
#include "tdoubleparam.h"
#include "tfilepath_io.h"
#include "toonz/tproject.h"
#include "tconvert.h"
#include "filebrowserpopup.h"
#include "filebrowser.h"
#include "tundo.h"
#include "tdoublekeyframe.h"
#include "texpression.h"
#include "toonz/txsheetexpr.h"

#include <QFileDialog>
#include <QFileInfo>

//=============================================================================

class LoadCurveUndo final : public TUndo {
  TDoubleParamP m_curve;
  TDoubleParamP m_oldCurve, m_newCurve;

public:
  LoadCurveUndo(TDoubleParam *curve) : m_curve(curve) {
    m_oldCurve = static_cast<TDoubleParam *>(curve->clone());
  }
  void onAdd() override {
    m_newCurve = static_cast<TDoubleParam *>(m_curve->clone());
  }

  void restore(const TDoubleParam &snapshot) const {
    m_curve->setDefaultValue(snapshot.getDefaultValue());
    m_curve->copyCurve(snapshot);
  }

  void undo() const override { restore(*m_oldCurve); }
  void redo() const override { restore(*m_newCurve); }
  int getSize() const override {
    return sizeof(*this) + 2 * sizeof(TDoubleParam);  // not very accurate
  }
};

//=============================================================================

class CurvePopup : public FileBrowserPopup {
  TFilePath m_folderPath;
  TDoubleParam *m_curve;

public:
  CurvePopup(const QString name, const TFilePath folderPath,
             TDoubleParam *curve)
      : FileBrowserPopup(name), m_folderPath(folderPath), m_curve(curve) {
    curve->addRef();
    m_browser->enableGlobalSelection(false);
  }

  ~CurvePopup() { m_curve->release(); }

  void setFilterType(QString type) {
    QStringList sl;
    sl << type;
    setFilterTypes(sl);
  }

  void initFolder() override { setFolder(m_folderPath); }

  bool checkOverride(const TFilePath &fp) const {
    if (TFileStatus(fp).doesExist()) {
      QString question = QObject::tr("Are you sure you want to override ") +
                         QString::fromStdWString(fp.getLevelNameW()) + "?";
      int ret = DVGui::MsgBox(question, QObject::tr("Override"),
                              QObject::tr("Cancel"), 1);
      return ret == 1;
    } else
      return true;
  }

  TDoubleParam *getCurve() const { return m_curve; }
};

//=============================================================================

class SaveCurvePopup final : public CurvePopup {
  TFilePath m_savedPath;

public:
  SaveCurvePopup(const TFilePath folderPath, TDoubleParam *curve)
      : CurvePopup(QObject::tr("Save Curve"), folderPath, curve) {
    setOkText(QObject::tr("Save"));
    setFilterType("curve");
  }

  bool execute() override;
  const TFilePath &savedPath() const { return m_savedPath; }
};

//-----------------------------------------------------------------------------

bool SaveCurvePopup::execute() {
  if (m_selectedPaths.empty()) return false;

  TFilePath fp(*m_selectedPaths.begin());

  if (fp.getType() == "") fp = fp.withType("curve");

  if (!checkOverride(fp)) return false;

  try {
    TSystem::touchParentDir(fp);
    TOStream os(fp);
    getCurve()->saveData(os);
    m_savedPath = fp;
    return true;
  } catch (...) {
    DVGui::warning(QObject::tr("It is not possible to save the curve."));
    return false;
  }
}

//=============================================================================

class LoadCurvePopup final : public CurvePopup {
public:
  LoadCurvePopup(const TFilePath folderPath, TDoubleParam *curve)
      : CurvePopup(QObject::tr("Load Curve"), folderPath, curve) {
    setOkText(QObject::tr("Load"));
    setFilterType("curve");
  }

  bool execute() override;
};

//-----------------------------------------------------------------------------

bool LoadCurvePopup::execute() {
  if (m_selectedPaths.empty()) return false;

  TFilePath fp(*m_selectedPaths.begin());

  if (fp.getType() == "") fp = fp.withType("curve");

  if (!TFileStatus(fp).doesExist()) return false;

  try {
    TIStream is(fp);
    // default value must be kept the same!!!
    TDoubleParam *curve = getCurve();
    double defaultValue = curve->getDefaultValue();
    LoadCurveUndo *undo = new LoadCurveUndo(curve);
    curve->loadData(is);
    curve->setDefaultValue(defaultValue);
    TUndoManager::manager()->add(undo);
  } catch (...) {
    DVGui::warning(QObject::tr("It is not possible to load the curve."));
    return false;
  }

  return true;
}

//=============================================================================

class ExportCurvePopup final : public CurvePopup {
  std::string m_name;

public:
  ExportCurvePopup(const TFilePath folderPath, TDoubleParam *curve,
                   const std::string &name)
      : CurvePopup(QObject::tr("Export Curve"), folderPath, curve)
      , m_name(name) {
    setOkText(QObject::tr("Export"));
    setFilterType("dat");
  }

  bool execute() override;
};

//-----------------------------------------------------------------------------

bool ExportCurvePopup::execute() {
  if (m_selectedPaths.empty()) return false;

  TFilePath fp(*m_selectedPaths.begin());

  if (fp.getType() == "") fp = fp.withType("dat");

  if (!checkOverride(fp)) return false;

  try {
    TSystem::touchParentDir(fp);
    Tofstream os(fp);
    os << "# RELEASE:      5.0" << std::endl;
    os << "# FILE NAME:    " << ::to_string(fp) << std::endl;
    os << "# COMPOSED BY:  " << TSystem::getUserName().toStdString()
       << std::endl;
    os << "# MACHINE NAME: " << TSystem::getHostName().toStdString()
       << std::endl;
    // os << "# DATE:         " << TSystem::getCurrentTime() << endl;
    os << std::endl << std::endl;

    int frameCount      = 1;
    TDoubleParam *curve = getCurve();
    if (curve->getKeyframeCount() > 0)
      frameCount =
          curve->keyframeIndexToFrame(curve->getKeyframeCount() - 1) + 1;

    os << "# Dump of:     " << m_name << std::endl;
    os << "# Frame range: 1 " << frameCount << std::endl;
    os << std::endl;

    for (int i = 0; i < frameCount; i++) {
      os.setf(std::ios::fixed, std::ios::floatfield);
      os.setf(std::ios::showpoint);
      os.precision(5);
      os.width(12);
      os << curve->getValue(i) << std::endl;
    }
  } catch (...) {
    DVGui::warning(QObject::tr("It is not possible to export data."));
    return false;
  }

  return true;
}

//------------------------------------------

TFilePath saveCurve(TDoubleParam *curve) {
  TFilePath folderPath =
      TProjectManager::instance()->getCurrentProject()->getScenesPath();
  SaveCurvePopup popup(folderPath, curve);
  popup.exec();
  return popup.savedPath();
}

//------------------------------------------

void loadCurve(TDoubleParam *curve) {
  TFilePath folderPath =
      TProjectManager::instance()->getCurrentProject()->getScenesPath();
  LoadCurvePopup popup(folderPath, curve);
  popup.exec();
}

//------------------------------------------

void exportCurve(TDoubleParam *curve, const std::string &name) {
  TFilePath folderPath =
      TProjectManager::instance()->getCurrentProject()->getScenesPath();
  ExportCurvePopup popup(folderPath, curve, name);
  popup.exec();
}

QString chooseCurveFileToOpen(QWidget *parent) {
  return QFileDialog::getOpenFileName(parent, QObject::tr("Open Curve File"),
                                      TProjectManager::instance()
                                          ->getCurrentProject()
                                          ->getScenesPath()
                                          .getQString(),
                                      QObject::tr("Curve Files (*.curve)"));
}

bool readCurveFile(const QString &path, TDoubleParam *curve) {
  try {
    TIStream stream{TFilePath(path)};
    if (!stream) return false;
    curve->loadData(stream);
    curve->setName(QFileInfo(path).fileName().toStdString());
    return true;
  } catch (...) {
    return false;
  }
}

bool applyCurveFile(TDoubleParam *target, TDoubleParam *source) {
  for (int i = 0; i < source->getKeyframeCount(); ++i) {
    const auto &key = source->getKeyframe(i);
    if (key.m_type != TDoubleKeyframe::Expression &&
        key.m_type != TDoubleKeyframe::SimilarShape)
      continue;
    TExpression expression;
    expression.setGrammar(target->getGrammar());
    expression.setText(key.m_expressionText);
    if (dependsOn(expression, target)) {
      DVGui::warning(QObject::tr(
          "Applying this curve would create a circular reference."));
      return false;
    }
  }

  auto undo = std::make_unique<LoadCurveUndo>(target);
  target->copyCurve(*source);
  TUndoManager::manager()->add(undo.release());
  return true;
}

void reloadCurveData(TDoubleParam *target, const TDoubleParam &source) {
  auto undo = std::make_unique<LoadCurveUndo>(target);
  target->setDefaultValue(source.getDefaultValue());
  target->copyCurve(source);
  TUndoManager::manager()->add(undo.release());
}

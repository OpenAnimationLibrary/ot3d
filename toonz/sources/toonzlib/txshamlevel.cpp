#include "toonz/txshamlevel.h"
#include "toonz/toonzscene.h"
#include "toonz/txshleveltypes.h"
#include "tstream.h"
#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <stdexcept>

DEFINE_CLASS_CODE(TXshAMLevel, 55)
PERSIST_IDENTIFIER(TXshAMLevel, "amModelLevel")

TXshAMLevel::TXshAMLevel(std::wstring name) : TXshLevel(m_classCode, name) {
  m_type = AM_XSHLEVEL;
}
TXshAMLevel::~TXshAMLevel() = default;
void TXshAMLevel::setPath(const TFilePath &path, bool keepCached) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_path == path) return;
  m_path = path;
  if (keepCached) return;  // Re-encode the same physical source after Save As.
  m_document.reset();
  m_mesh.reset();
  m_revision.clear();
  m_error.clear();
}
void TXshAMLevel::getFids(std::vector<TFrameId> &fids) const {
  if (!isEmpty()) fids.push_back(TFrameId(1));
}
void TXshAMLevel::loadData(TIStream &is) {
  std::string tag;
  while (is.matchTag(tag)) {
    if (tag == "name") {
      std::wstring name;
      is >> name;
      setName(name);
    } else if (tag == "path")
      is >> m_path;
    else
      throw TException("A:M level: unknown scene tag " + tag);
    is.closeChild();
  }
}
void TXshAMLevel::saveData(TOStream &os) {
  os.child("name") << getName();
  os.child("path") << m_path;
}
void TXshAMLevel::load() {
  const TFilePath path =
      getScene() ? getScene()->decodeFilePath(m_path) : m_path;
  try {
    QFile file(QString::fromStdWString(path.getWideString()));
    if (!file.open(QIODevice::ReadOnly))
      throw std::runtime_error("cannot open model source");
    const qint64 budget = qint64(otam::Limits().bytes);
    if (file.size() <= 0 || file.size() > budget)
      throw std::runtime_error("model source exceeds size limit or is empty");
    const QByteArray bytes = file.read(budget + 1);
    if (bytes.size() > budget || file.error() != QFileDevice::NoError)
      throw std::runtime_error(
          "model source read failed or exceeded size limit");
    auto doc = std::make_shared<otam::Document>(
        otam::parse(std::string(bytes.constData(), std::size_t(bytes.size()))));
    if (doc->models.size() != 1)
      throw std::runtime_error(
          "multiple models require a project importer; source is unchanged");
    auto mesh = std::make_shared<otam::Mesh>(otam::previewMesh(*doc));
    if (mesh->triangles.empty())
      throw std::runtime_error(
          "no supported explicit patches; patch discovery/special-patch "
          "evaluation is not yet available");
    for (const auto &warning : mesh->warnings)
      qWarning().noquote() << "A:M model:" << QString::fromStdString(warning);
    const auto revision =
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
            .toHex()
            .toStdString();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_document = std::move(doc);
    m_mesh     = std::move(mesh);
    m_revision = revision;
    m_error.clear();
  } catch (const std::exception &e) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Do not silently render stale geometry after a failed explicit reload.
    m_document.reset();
    m_mesh.reset();
    m_revision.clear();
    m_error = "A:M model " + path.getQString().toStdString() + ": " + e.what();
    throw TException(m_error);
  }
}
std::shared_ptr<const otam::Document> TXshAMLevel::document() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_document;
}
std::shared_ptr<const otam::Mesh> TXshAMLevel::mesh() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_mesh;
}
std::string TXshAMLevel::revision() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_revision;
}
std::string TXshAMLevel::error() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_error;
}

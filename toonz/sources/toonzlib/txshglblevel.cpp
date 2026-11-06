#include "toonz/txshglblevel.h"
#include "toonz/toonzscene.h"
#include "toonz/txshleveltypes.h"
#include "tstream.h"
#include <QDebug>
#include <stdexcept>

DEFINE_CLASS_CODE(TXshGlbLevel, 56)
PERSIST_IDENTIFIER(TXshGlbLevel, "glbModelLevel")

TXshGlbLevel::TXshGlbLevel(std::wstring name) : TXshLevel(m_classCode, name) {
  m_type = GLB_XSHLEVEL;
}
TXshGlbLevel::~TXshGlbLevel() = default;
void TXshGlbLevel::setPath(const TFilePath &path, bool keepCached) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_path == path) return;
  m_path = path;
  if (keepCached) return;  // Re-encode the same physical source after Save As.
  m_asset.reset();
  m_geometry.reset();
  m_error.clear();
}
void TXshGlbLevel::getFids(std::vector<TFrameId> &fids) const {
  if (!isEmpty()) fids.push_back(TFrameId(1));
}
void TXshGlbLevel::loadData(TIStream &is) {
  std::string tag;
  while (is.matchTag(tag)) {
    if (tag == "name") {
      std::wstring name;
      is >> name;
      setName(name);
    } else if (tag == "path")
      is >> m_path;
    else
      throw TException("GLB level: unknown scene tag " + tag);
    is.closeChild();
  }
}
void TXshGlbLevel::saveData(TOStream &os) {
  os.child("name") << getName();
  os.child("path") << m_path;
}
void TXshGlbLevel::load() {
  const auto path = getScene() ? getScene()->decodeFilePath(m_path) : m_path;
  try {
#ifdef _WIN32
    const std::filesystem::path native(path.getWideString());
#else
    const auto native =
        std::filesystem::u8path(path.getQString().toUtf8().toStdString());
#endif
    auto loaded = otglb::load(native);
    if (!loaded) throw std::runtime_error(loaded.error);
    otglb::RenderOptions options;
    options.materialColors = true;
    auto geometry          = std::make_shared<otglb::ViewerGeometry>(
        otglb::prepareViewerGeometry(*loaded.asset, options));
    if (geometry->triangles.empty())
      throw std::runtime_error("no supported triangles in the default scene");
    geometry->warnings.insert(geometry->warnings.end(), loaded.warnings.begin(),
                              loaded.warnings.end());
    for (const auto &warning : geometry->warnings)
      qWarning().noquote() << "GLB model:" << QString::fromStdString(warning);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_asset    = std::move(loaded.asset);
    m_geometry = std::move(geometry);
    m_error.clear();
  } catch (const std::exception &error) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_asset.reset();
    m_geometry.reset();
    m_error =
        "GLB model " + path.getQString().toStdString() + ": " + error.what();
    throw TException(m_error);
  }
}

std::shared_ptr<const otglb::Asset> TXshGlbLevel::asset() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_asset;
}

std::shared_ptr<const otglb::ViewerGeometry> TXshGlbLevel::geometry() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_geometry;
}

std::string TXshGlbLevel::error() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_error;
}

#include "toonz/txshamlevel.h"
#include "amaction.h"
#include "toonz/toonzscene.h"
#include "toonz/txshleveltypes.h"
#include "tstream.h"
#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <charconv>
#include <set>
#include <unordered_map>

DEFINE_CLASS_CODE(TXshAMLevel, 55)
PERSIST_IDENTIFIER(TXshAMLevel, "amModelLevel")

namespace {
const int DefaultStyle = 1;
const otam::Color DefaultSurface{{0.65f, 0.65f, 0.65f}};

TPixel32 styleColor(const otam::Color &color) {
  return TPixel32(std::lround(color[0] * 255), std::lround(color[1] * 255),
                  std::lround(color[2] * 255), 255);
}

TPixel32 opaque(TPixel32 color) {
  color.m = 255;
  return color;
}

std::string membershipKey(const otam::Group &group) {
  auto points = group.points;
  std::sort(points.begin(), points.end());
  points.erase(std::unique(points.begin(), points.end()), points.end());
  QCryptographicHash hash(QCryptographicHash::Sha256);
  for (auto id : points) {
    hash.addData(QByteArray::number(qulonglong(id)));
    hash.addData(",", 1);
  }
  return hash.result().toHex().toStdString();
}
TXshAMLevel::PointValue pointValue(const otam::ControlPoint &point) {
  return {point.position, point.bias};
}

bool validPointValue(const TXshAMLevel::PointValue &value) {
  const auto finite = [](double n) { return std::isfinite(n); };
  return std::all_of(value.position.begin(), value.position.end(), finite) &&
         std::all_of(value.bias.begin(), value.bias.end(), finite);
}
}  // namespace

TXshAMLevel::TXshAMLevel(std::wstring name)
    : TXshLevel(m_classCode, name), m_palette(new TPalette) {
  m_type = AM_XSHLEVEL;
  m_palette->getStyle(DefaultStyle)->setMainColor(styleColor(DefaultSurface));
  m_palette->getStyle(DefaultStyle)->setName(L"Default Surface");
}
TXshAMLevel::~TXshAMLevel() = default;
void TXshAMLevel::setPath(const TFilePath &path, bool keepCached) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_path == path) return;
  m_path = path;
  if (keepCached) return;  // Re-encode the same physical source after Save As.
  m_document.reset();
  m_editableModel.reset();
  m_mesh.reset();
  m_renderMesh.reset();
  m_renderPalette.reset();
  m_colorOverrides.clear();
  m_actionSample = {};
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
    else if (tag == "surfacePalette") {
      m_palette->loadData(is);
      if (m_palette->getStyleCount() <= DefaultStyle)
        throw TException("A:M palette: missing default surface style");
    } else if (tag == "surfaceStyles") {
      std::string child;
      while (is.matchTag(child)) {
        if (child != "group")
          throw TException("A:M palette: expected a group binding");
        std::string key;
        int id, red, green, blue;
        is >> key >> id >> red >> green >> blue;
        if (id <= DefaultStyle || id >= 4096 || red < 0 || red > 255 ||
            green < 0 || green > 255 || blue < 0 || blue > 255 ||
            m_groupStyles.size() >= 4094)
          throw TException("A:M palette: invalid group binding");
        m_groupStyles.emplace(key,
                              SurfaceStyle{id, TPixel32(red, green, blue)});
        is.closeChild();
      }
    } else if (tag == "controlPointEdits") {
      if (!m_editRevision.empty())
        throw TException("A:M level: duplicate control point edits");
      is >> m_editRevision;
      if (m_editRevision.size() != 64 ||
          m_editRevision.find_first_not_of("0123456789abcdef") !=
              std::string::npos)
        throw TException("A:M level: invalid control point source revision");
      const auto readNumber = [&] {
        std::string token;
        is >> token;
        bool ok;
        const double n = QByteArray::fromStdString(token).toDouble(&ok);
        if (!ok || !std::isfinite(n))
          throw TException("A:M level: invalid control point value");
        return n;
      };
      std::string child;
      while (is.matchTag(child)) {
        if (child != "point" || m_pointEdits.size() >= otam::Limits().records)
          throw TException("A:M level: invalid control point edit record");
        std::string token;
        is >> token;
        otam::Id id       = 0;
        const auto end    = token.data() + token.size();
        const auto parsed = std::from_chars(token.data(), end, id);
        if (parsed.ec != std::errc() || parsed.ptr != end)
          throw TException("A:M level: invalid control point ID");
        PointValue value;
        for (auto &n : value.position) n = readNumber();
        for (auto &n : value.bias) n = readNumber();
        if (!m_pointEdits.emplace(id, value).second)
          throw TException("A:M level: duplicate control point ID");
        is.closeChild();
      }
    } else
      throw TException("A:M level: unknown scene tag " + tag);
    is.closeChild();
  }
}
void TXshAMLevel::saveData(TOStream &os) {
  std::lock_guard<std::mutex> lock(m_mutex);
  os.child("name") << getName();
  os.child("path") << m_path;
  os.openChild("surfacePalette");
  m_palette->saveData(os);
  os.closeChild();
  if (!m_groupStyles.empty()) {
    os.openChild("surfaceStyles");
    for (const auto &[key, binding] : m_groupStyles)
      os.child("group") << key << binding.id << int(binding.sourceColor.r)
                        << int(binding.sourceColor.g)
                        << int(binding.sourceColor.b);
    os.closeChild();
  }
  if (!m_pointEdits.empty()) {
    os.openChild("controlPointEdits");
    os << m_editRevision;
    for (const auto &[id, value] : m_pointEdits) {
      os.openChild("point");
      os << std::to_string(id);
      for (auto n : value.position)
        os << QByteArray::number(n, 'g', 17).toStdString();
      for (auto n : value.bias)
        os << QByteArray::number(n, 'g', 17).toStdString();
      os.closeChild();
    }
    os.closeChild();
  }
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
    const auto revision =
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
            .toHex()
            .toStdString();
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto &source    = doc->models.front();
    auto model            = std::shared_ptr<const otam::Model>(doc, &source);
    auto warnings         = doc->warnings;
    const bool staleEdits = !m_pointEdits.empty() && m_editRevision != revision;
    if (staleEdits) {
      warnings.push_back(
          "A:M source changed. Control point edits are suspended until the "
          "original source is restored.");
    } else if (!m_pointEdits.empty()) {
      auto edited         = std::make_shared<otam::Model>(source);
      std::size_t applied = 0;
      for (std::size_t i = 0; i < edited->points.size(); ++i) {
        auto &point     = edited->points[i];
        const auto edit = m_pointEdits.find(point.id);
        if (edit == m_pointEdits.end()) continue;
        if (point.head != i && edit->second.position != point.position)
          throw std::runtime_error("an attached point cannot own a position");
        point.position = edit->second.position;
        point.bias     = edit->second.bias;
        ++applied;
      }
      if (applied != m_pointEdits.size())
        throw std::runtime_error("control point edit refers to an unknown ID");
      model = std::move(edited);
    }
    auto mesh =
        std::make_shared<otam::Mesh>(otam::previewMesh(*model, warnings));
    if (mesh->triangles.empty())
      throw std::runtime_error(
          "no supported explicit patches; patch discovery/special-patch "
          "evaluation is not yet available");
    for (const auto &warning : mesh->warnings)
      qWarning().noquote() << "A:M model:" << QString::fromStdString(warning);
    refreshPalette(source, revision);
    m_editableModel = staleEdits ? nullptr : std::move(model);
    m_document = std::move(doc);
    m_mesh     = std::move(mesh);
    m_renderMesh = m_mesh;
    m_colorOverrides.clear();
    m_actionSample = {};
    updatePaletteColorsLocked();
    m_revision = revision;
    m_error.clear();
  } catch (const std::exception &e) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Do not silently render stale geometry after a failed explicit reload.
    m_document.reset();
    m_editableModel.reset();
    m_mesh.reset();
    m_renderMesh.reset();
    m_renderPalette.reset();
    m_colorOverrides.clear();
    m_actionSample = {};
    m_revision.clear();
    m_error = "A:M model " + path.getQString().toStdString() + ": " + e.what();
    throw TException(m_error);
  }
}
std::shared_ptr<const otam::Document> TXshAMLevel::document() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_document;
}
std::shared_ptr<const otam::Model> TXshAMLevel::editableModel() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_editableModel;
}

bool TXshAMLevel::setControlPoint(otam::Id id, const PointValue &value,
                                  const std::string &sourceRevision) {
  return setControlPoints({{id, value}}, sourceRevision);
}

bool TXshAMLevel::setControlPoints(const PointValues &values,
                                   const std::string &sourceRevision) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_editableModel || sourceRevision != m_revision || values.empty())
    return false;

  const auto &source = m_document->models.front();
  if (values.size() > source.points.size()) return false;
  std::unordered_map<otam::Id, const PointValue *> requested;
  requested.reserve(values.size());
  for (const auto &[id, value] : values)
    if (!validPointValue(value) || !requested.emplace(id, &value).second)
      return false;

  std::vector<std::pair<std::size_t, const PointValue *>> changed;
  changed.reserve(values.size());
  for (std::size_t i = 0; i < source.points.size() && !requested.empty(); ++i) {
    const auto &point = source.points[i];
    const auto it     = requested.find(point.id);
    if (it == requested.end()) continue;
    const auto &value = *it->second;
    if (point.head != i && point.position != value.position) return false;
    if (!(pointValue(m_editableModel->points[i]) == value))
      changed.emplace_back(i, &value);
    requested.erase(it);
  }
  if (!requested.empty() || changed.empty()) return false;

  auto model = std::make_shared<otam::Model>(*m_editableModel);
  std::map<otam::Id, PointValue> replacements;
  for (const auto &[i, value] : changed) {
    auto &point    = model->points[i];
    point.position = value->position;
    point.bias     = value->bias;
    if (!(pointValue(source.points[i]) == *value))
      replacements.emplace(point.id, *value);
  }
  auto editRevision = m_revision;
  auto overrides    = paletteColorOverridesLocked(
         m_renderPalette ? *m_renderPalette : *m_palette);
  std::shared_ptr<const otam::Mesh> mesh, renderMesh;
  try {
    mesh = std::make_shared<otam::Mesh>(
        otam::previewMesh(*model, m_document->warnings));
    renderMesh = meshWithColorsLocked(mesh, overrides);
  } catch (const std::runtime_error &e) {
    qWarning().noquote() << "A:M control point edits:" << e.what();
    return false;
  }

  // Publish the model, serialized edits and render snapshot together.
  for (const auto &change : changed)
    m_pointEdits.erase(source.points[change.first].id);
  m_pointEdits.merge(replacements);
  if (m_pointEdits.empty()) editRevision.clear();
  m_editRevision   = std::move(editRevision);
  m_editableModel = std::move(model);
  m_mesh          = std::move(mesh);
  m_renderMesh     = std::move(renderMesh);
  m_colorOverrides = std::move(overrides);
  m_actionSample   = {};
  return true;
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

void TXshAMLevel::refreshPalette(const otam::Model &model,
                                 const std::string &revision) {
  m_palette->setPaletteName(getName());
  auto *page = m_palette->getPage(0);
  if (!page) page = m_palette->addPage(L"Surfaces");
  m_palette->getStyle(DefaultStyle)->setName(L"Default Surface");
  if (!m_palette->getStylePage(DefaultStyle)) page->addStyle(DefaultStyle);

  // Match unique control-point memberships across renames and source
  // reordering. When several groups have the same membership, also use their
  // names. Identical duplicates are only safe to match within the same file
  // revision.
  std::vector<std::string> keys(model.groups.size());
  std::map<std::string, int> counts;
  for (std::size_t i = 0; i < model.groups.size(); ++i) {
    if (!model.groups[i].hasColor) continue;
    keys[i] = membershipKey(model.groups[i]);
    ++counts[keys[i]];
  }
  std::map<std::string, int> namedCounts;
  for (std::size_t i = 0; i < model.groups.size(); ++i) {
    if (keys[i].empty()) continue;
    if (counts[keys[i]] > 1)
      keys[i] += ":" + QCryptographicHash::hash(
                           QByteArray::fromStdString(model.groups[i].name),
                           QCryptographicHash::Sha256)
                           .toHex()
                           .toStdString();
    ++namedCounts[keys[i]];
  }

  auto previous = std::move(m_groupStyles);
  m_groupStyles.clear();
  m_styleByGroup.assign(model.groups.size(), -1);
  std::set<int> usedStyles;
  for (std::size_t i = 0; i < model.groups.size(); ++i) {
    auto &key         = keys[i];
    const auto &group = model.groups[i];
    if (key.empty()) continue;
    if (namedCounts[key] > 1) key += ":" + revision + ":" + std::to_string(i);
    const auto source = styleColor(group.color);
    auto old          = previous.find(key);
    int id            = -1;
    if (old != previous.end() && old->second.id < m_palette->getStyleCount() &&
        !usedStyles.count(old->second.id)) {
      id          = old->second.id;
      auto *style = m_palette->getStyle(id);
      if (!style->hasMainColor() || !m_palette->getStylePage(id))
        m_palette->setStyle(id, source);
      else if (m_palette->getKeyframeCount(id) == 0 &&
               opaque(style->getMainColor()) == old->second.sourceColor &&
               source != old->second.sourceColor)
        style->setMainColor(source);
    } else {
      id = m_palette->addStyle(source);
    }
    if (id < 0)
      continue;  // The palette's style limit leaves source RGB intact.
    if (!m_palette->getStylePage(id)) page->addStyle(id);
    const auto name = QString::fromStdString(group.name);
    m_palette->getStyle(id)->setName(
        name.isEmpty()
            ? QStringLiteral("Group %1").arg(qulonglong(i + 1)).toStdWString()
            : name.toStdWString());
    m_groupStyles.emplace(key, SurfaceStyle{id, source});
    m_styleByGroup[i] = id;
    usedStyles.insert(id);
  }

  // Hide orphaned bindings without recycling style IDs that undo commands
  // or the palette UI could still refer to.
  for (const auto &[key, binding] : previous) {
    if (usedStyles.count(binding.id) ||
        binding.id >= m_palette->getStyleCount())
      continue;
    if (auto *oldPage = m_palette->getStylePage(binding.id))
      oldPage->removeStyle(oldPage->search(binding.id));
  }
}

bool TXshAMLevel::updatePaletteColorsLocked() {
  if (!m_mesh) return false;
  const bool wasAnimated = bool(m_renderPalette);
  if (m_palette->isAnimated()) {
    m_renderPalette = m_palette->clone();
    // The clone can start at the UI's current frame with an edited style.
    // Reset its frame so the first render always evaluates the saved keys.
    m_renderPalette->setFrame(-1);
    return true;
  }
  m_renderPalette.reset();
  return updateRenderMeshLocked(*m_palette) || wasAnimated;
}

std::map<int, TPixel32> TXshAMLevel::paletteColorOverridesLocked(
    const TPalette &palette) const {
  std::map<int, TPixel32> overrides;
  auto resolve = [&](int id, const TPixel32 &source) {
    if (id >= palette.getStyleCount() || !palette.getStylePage(id)) return;
    const auto *style = palette.getStyle(id);
    if (!style->hasMainColor()) return;
    const auto color = opaque(style->getMainColor());
    if (color != source) overrides.emplace(id, color);
  };
  resolve(DefaultStyle, styleColor(DefaultSurface));
  for (const auto &[key, binding] : m_groupStyles)
    resolve(binding.id, binding.sourceColor);
  return overrides;
}

bool TXshAMLevel::updateRenderMeshLocked(const TPalette &palette) const {
  auto overrides = paletteColorOverridesLocked(palette);
  if (overrides == m_colorOverrides) return false;
  m_renderMesh     = meshWithColorsLocked(m_mesh, overrides);
  m_colorOverrides = std::move(overrides);
  return true;
}

std::shared_ptr<const otam::Mesh> TXshAMLevel::meshWithColorsLocked(
    const std::shared_ptr<const otam::Mesh> &mesh,
    const std::map<int, TPixel32> &overrides) const {
  if (overrides.empty()) return mesh;
  auto colored = std::make_shared<otam::Mesh>(*mesh);
  for (auto &triangle : colored->triangles) {
    const int style = triangle.colorGroup < 0
                          ? DefaultStyle
                          : m_styleByGroup.at(triangle.colorGroup);
    auto color      = overrides.find(style);
    if (color != overrides.end()) {
      const auto p   = color->second;
      triangle.color = {{p.r / 255.f, p.g / 255.f, p.b / 255.f}};
    }
  }
  return colored;
}

void TXshAMLevel::sampleActionLocked(
    const std::shared_ptr<const otam::Action> &action, double seconds) const {
  // Cache one immutable pose and its geometry. A renderer can hold either
  // snapshot after the scene changes or this cache is replaced.
  seconds = std::min(seconds, action->duration);
  if (m_actionSample.action.lock() == action &&
      m_actionSample.seconds == seconds)
    return;
  ActionSample sample;
  sample.action  = action;
  sample.seconds = seconds;
  try {
    auto model = std::make_shared<otam::Model>(*m_editableModel);
    otam::applyAction(*action, *model, seconds);
    sample.mesh = std::make_shared<otam::Mesh>(
        otam::previewMesh(*model, m_document->warnings));
    sample.model = std::move(model);
  } catch (const std::runtime_error &e) {
    sample.warning = std::string("A:M action skipped: ") + e.what();
  }
  m_actionSample = std::move(sample);
}

std::shared_ptr<const otam::Model> TXshAMLevel::actionModel(
    const std::shared_ptr<const otam::Action> &action, double seconds) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_editableModel || !action || action->channels.empty() || seconds < 0)
    return m_editableModel;
  sampleActionLocked(action, seconds);
  return m_actionSample.model ? m_actionSample.model : m_editableModel;
}

bool TXshAMLevel::updatePaletteColors() {
  std::lock_guard<std::mutex> lock(m_mutex);
  return updatePaletteColorsLocked();
}

std::shared_ptr<const otam::Mesh> TXshAMLevel::renderMesh(int frame) const {
  return renderMesh(frame, {}, 0);
}

std::shared_ptr<const otam::Mesh> TXshAMLevel::renderMesh(
    int frame, const std::shared_ptr<const otam::Action> &action,
    double seconds) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_renderPalette && m_renderPalette->getFrame() != frame) {
    m_renderPalette->setFrame(frame);
    updateRenderMeshLocked(*m_renderPalette);
  }
  if (!m_editableModel || !action || action->channels.empty() || seconds < 0)
    return m_renderMesh;

  sampleActionLocked(action, seconds);
  if (!m_actionSample.mesh) {
    auto fallback = std::make_shared<otam::Mesh>(*m_renderMesh);
    fallback->warnings.push_back(m_actionSample.warning);
    return fallback;
  }
  return meshWithColorsLocked(m_actionSample.mesh, m_colorOverrides);
}

std::map<int, TPixel32> TXshAMLevel::sourceStyleColors() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::map<int, TPixel32> colors{{DefaultStyle, styleColor(DefaultSurface)}};
  for (const auto &[key, binding] : m_groupStyles)
    colors.emplace(binding.id, binding.sourceColor);
  return colors;
}

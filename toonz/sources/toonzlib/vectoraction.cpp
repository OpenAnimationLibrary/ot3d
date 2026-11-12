#include "toonz/vectoraction.h"

#include "texception.h"
#include "tstroke.h"
#include "toutputproperties.h"
#include "toonz/toonzscene.h"
#include "toonz/sceneproperties.h"
#include "toonz/txsheet.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>

#include <cmath>
#include <memory>

namespace {

constexpr double MaxOffset = 1e9;
constexpr double MaxTime   = 3e9;

bool validTime(double time) { return time >= 0 && time <= MaxTime; }

double canonicalTime(double time) { return std::round(time * 1e9) / 1e9; }

[[noreturn]] void invalidAction() {
  throw TException("Invalid or unsupported vector Action file.");
}

bool validOffset(const TPointD &offset) {
  return std::abs(offset.x) <= MaxOffset && std::abs(offset.y) <= MaxOffset;
}

double frameRate(const TXsheet *xsheet) {
  const auto *scene = xsheet ? xsheet->getScene() : nullptr;
  const double fps =
      scene ? scene->getProperties()->getOutputProperties()->getFrameRate()
            : 24.0;
  return std::isfinite(fps) && fps > 0 ? fps : 24.0;
}

int pointCount(const TStroke &stroke) {
  return stroke.getControlPointCount() * (stroke.isSelfLoop() ? -1 : 1);
}

bool validPoint(const VectorAction &action, VectorAction::Point point) {
  const auto [stroke, index] = point;
  if (stroke < 0 || stroke >= int(action.topology.size()) || index < 0)
    return false;
  const int count = action.topology[stroke];
  // A closed stroke stores its first point again at the end.
  return index < (count < 0 ? -count - 1 : count);
}

bool matchesTopology(const VectorAction &action, const TVectorImageP &image) {
  if (image->getStrokeCount() != action.topology.size()) return false;
  for (UINT i = 0; i < image->getStrokeCount(); ++i)
    if (pointCount(*image->getStroke(i)) != action.topology[i]) return false;
  return true;
}

}  // namespace

VectorAction VectorAction::forDrawing(const TVectorImageP &image) {
  VectorAction action;
  QMutexLocker lock(image->getMutex());
  for (UINT i = 0; i < image->getStrokeCount(); ++i)
    action.topology.push_back(pointCount(*image->getStroke(i)));
  return action;
}

bool VectorAction::matches(const TVectorImageP &image) const {
  if (!image) return false;
  QMutexLocker lock(image->getMutex());
  return matchesTopology(*this, image);
}

QByteArray VectorAction::write() const {
  QJsonArray strokes, tracks;
  for (int count : topology) strokes.append(count);
  for (const auto &[point, keys] : channels) {
    QJsonArray entries;
    for (const auto &[time, offset] : keys)
      entries.append(QJsonArray{time, offset.x, offset.y});
    tracks.append(QJsonArray{point.first, point.second, entries});
  }
  const auto bytes = QJsonDocument(QJsonObject{{"format", "OT3D Vector Action"},
                                               {"version", 1},
                                               {"strokes", strokes},
                                               {"tracks", tracks}})
                         .toJson(QJsonDocument::Compact);
  if (bytes.size() > MaxBytes)
    throw TException("Vector Action exceeds the file size limit.");
  return bytes;
}

VectorAction VectorAction::read(const QByteArray &bytes) {
  if (bytes.size() > MaxBytes) invalidAction();
  const auto object  = QJsonDocument::fromJson(bytes).object();
  const auto strokes = object.value("strokes");
  const auto tracks  = object.value("tracks");
  if (object.value("format") != QJsonValue("OT3D Vector Action") ||
      object.value("version").toDouble() != 1 || !strokes.isArray() ||
      !tracks.isArray())
    invalidAction();

  VectorAction action;
  for (const auto value : strokes.toArray()) {
    const int count = value.toInt();
    if (!count || count < -MaxBytes || count > MaxBytes) invalidAction();
    action.topology.push_back(count);
  }
  for (const auto value : tracks.toArray()) {
    const auto track = value.toArray();
    if (track.size() != 3 || !track.at(2).isArray()) invalidAction();
    const Point point(track.at(0).toInt(-1), track.at(1).toInt(-1));
    if (!validPoint(action, point)) invalidAction();
    Keys keys;
    for (const auto entry : track.at(2).toArray()) {
      const auto key = entry.toArray();
      if (key.size() != 3 || !key.at(0).isDouble() || !key.at(1).isDouble() ||
          !key.at(2).isDouble())
        invalidAction();
      const double time = key.at(0).toDouble();
      const TPointD offset(key.at(1).toDouble(), key.at(2).toDouble());
      if (!validTime(time) || !validOffset(offset) ||
          !keys.emplace(canonicalTime(time), offset).second)
        invalidAction();
    }
    if (keys.empty() || !action.channels.emplace(point, std::move(keys)).second)
      invalidAction();
  }
  return action;
}

TPointD VectorAction::sample(Point point, double seconds) const {
  const auto channel = channels.find(point);
  if (channel == channels.end() || channel->second.empty()) return {};
  const auto &keys = channel->second;
  const auto next  = keys.lower_bound(seconds);
  if (next == keys.begin()) return next->second;
  const auto previous = std::prev(next);
  if (next == keys.end()) return previous->second;
  const double t =
      (seconds - previous->first) / (next->first - previous->first);
  return previous->second * (1 - t) + next->second * t;
}

void VectorAction::setKeys(double seconds, const Offsets &offsets) {
  if (!validTime(seconds)) invalidAction();
  seconds = canonicalTime(seconds);
  for (const auto &[point, offset] : offsets)
    if (!validPoint(*this, point) || !validOffset(offset)) invalidAction();
  for (const auto &[point, offset] : offsets) {
    auto &keys = channels[point];
    if (keys.empty() && seconds > 0) keys.emplace(0, TPointD());
    keys[seconds] = offset;
  }
}

void VectorAction::replaceKeys(Point point, Keys keys) {
  if (!validPoint(*this, point)) invalidAction();
  for (const auto &[time, offset] : keys)
    if (!validTime(time) || !validOffset(offset)) invalidAction();
  if (keys.empty())
    channels.erase(point);
  else
    channels[point] = std::move(keys);
}

double vectorActionTime(const TXsheet *xsheet, int startRow, double frame) {
  // Round to nanoseconds so revisiting a key lands on its stored time.
  return canonicalTime((frame - startRow) / frameRate(xsheet));
}

double vectorActionFrame(const TXsheet *xsheet, int startRow, double seconds) {
  return startRow + seconds * frameRate(xsheet);
}

namespace {

TVectorImageP deformDrawing(TVectorImageP result,
                            const VectorAction::Offsets &offsets) {
  result->findRegions();
  std::vector<int> changed;
  std::vector<TStroke *> oldStrokes;
  std::vector<std::unique_ptr<TStroke>> oldCopies;
  for (const auto &[point, offset] : offsets) {
    const auto [stroke, index] = point;
    auto *curve                = result->getStroke(stroke);
    if (changed.empty() || changed.back() != stroke) {
      changed.push_back(stroke);
      oldCopies.push_back(std::make_unique<TStroke>(*curve));
      oldStrokes.push_back(oldCopies.back().get());
    }
    const TPointD position =
        static_cast<TPointD>(curve->getControlPoint(index)) + offset;
    curve->setControlPoint(index, position);
    if (index == 0 && curve->isSelfLoop())
      curve->setControlPoint(curve->getControlPointCount() - 1, position);
  }
  result->notifyChangedStrokes(changed, oldStrokes, false);
  return result;
}

}  // namespace

TVectorImageP VectorAction::pose(const TVectorImageP &image,
                                 double seconds) const {
  if (!image || seconds < 0 || channels.empty()) return image;
  Offsets offsets;
  for (const auto &[point, keys] : channels) {
    const auto offset = sample(point, seconds);
    if (offset != TPointD()) offsets.emplace(point, offset);
  }
  if (offsets.empty()) return image;
  TVectorImageP copy;
  {
    QMutexLocker lock(image->getMutex());
    if (!matchesTopology(*this, image)) return image;
    copy = image->clone();
  }
  return deformDrawing(copy, offsets);
}

TVectorImageP offsetVectorDrawing(const TVectorImageP &image,
                                  const VectorAction::Offsets &offsets) {
  if (!image || offsets.empty()) return image;
  TVectorImageP copy;
  {
    QMutexLocker lock(image->getMutex());
    copy = image->clone();
  }
  return deformDrawing(copy, offsets);
}

#include "amaction.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace otam {
namespace {

// HashTime.h uses 135,000 ticks per second. A:M's text format divides
// each second into 30 frame units, with a tick remainder after the dot.
constexpr std::int64_t TicksPerSecond = 135000;
constexpr int FramesPerSecond         = 30;
constexpr std::int64_t TicksPerFrame  = TicksPerSecond / FramesPerSecond;
// Keep tick precision when converting between the native clock and double.
constexpr std::int64_t MaxTicks = std::int64_t{1} << 50;
constexpr double MaxOffset      = 2e9;
// Even a file with one channel per key and maximum-width numbers fits in
// the tagged source reader's byte limit.
constexpr std::size_t MaxPositionKeys = 480000;

[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error("A:M Action: " + message);
}

std::int64_t actionTicks(double seconds) {
  const double scaled = seconds * TicksPerSecond;
  if (!std::isfinite(scaled) || scaled < 0 || scaled > MaxTicks)
    fail("time is out of range");
  return std::llround(scaled);
}

std::int64_t readUnsigned(std::string_view text) {
  if (text.empty()) fail("missing integer");
  std::int64_t value = 0;
  const auto end     = text.data() + text.size();
  const auto result  = std::from_chars(text.data(), end, value);
  if (result.ec != std::errc() || result.ptr != end || value < 0)
    fail("invalid nonnegative integer");
  return value;
}

double readTime(std::string_view text) {
  const auto colon = text.find(':');
  const auto seconds =
      colon == std::string_view::npos ? 0 : readUnsigned(text.substr(0, colon));
  if (colon != std::string_view::npos) text.remove_prefix(colon + 1);
  const auto dot   = text.find('.');
  const auto frame = readUnsigned(text.substr(0, dot));
  const auto tick =
      dot == std::string_view::npos ? 0 : readUnsigned(text.substr(dot + 1));
  if (seconds > MaxTicks / TicksPerSecond || frame >= FramesPerSecond ||
      tick >= TicksPerFrame)
    fail("time is out of range");
  const auto ticks = seconds * TicksPerSecond + frame * TicksPerFrame + tick;
  if (ticks > MaxTicks) fail("time is out of range");
  return double(ticks) / TicksPerSecond;
}

void writeTime(std::ostream &out, std::int64_t ticks) {
  const auto seconds   = ticks / TicksPerSecond;
  const auto frame     = ticks / TicksPerFrame % FramesPerSecond;
  const auto remainder = ticks % TicksPerFrame;
  if (seconds) out << seconds << ':';
  out << frame;
  if (remainder)
    out << '.' << std::setfill('0') << std::setw(4) << remainder
        << std::setfill(' ');
}

bool validOffset(const Vec3 &v) {
  return std::all_of(v.begin(), v.end(), [](double n) {
    return std::isfinite(n) && std::abs(n) <= MaxOffset;
  });
}

Vec3 readVector(std::istream &input) {
  Vec3 result{};
  if (!(input >> result[0] >> result[1] >> result[2]) || !validOffset(result))
    fail("invalid position offset");
  return result;
}

std::string trim(const std::string &s) {
  const auto first = s.find_first_not_of(" \r\n\t");
  return first == std::string::npos
             ? std::string()
             : s.substr(first, s.find_last_not_of(" \r\n\t") - first + 1);
}

std::string textContent(const Document &d, const SourceNode &node) {
  std::string result;
  auto begin = node.contentBegin;
  for (const auto i : node.children) {
    const auto &child = d.nodes[i];
    result.append(d.source, begin, child.begin - begin);
    result += '\n';
    begin = child.end;
  }
  result.append(d.source, begin, node.contentEnd - begin);
  return result;
}

std::map<std::string, std::string> properties(const Document &d,
                                              const SourceNode &node) {
  std::istringstream in(textContent(d, node));
  std::map<std::string, std::string> result;
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty()) continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos ||
        !result.emplace(trim(line.substr(0, eq)), trim(line.substr(eq + 1)))
             .second)
      fail("invalid or duplicate property in " + node.name);
  }
  return result;
}

void expectEmpty(const Document &d, const SourceNode &node) {
  if (!node.children.empty() || !trim(textContent(d, node)).empty())
    fail("unsupported content in " + node.name);
}

void checkAction(const Action &action) {
  const auto duration = actionTicks(action.duration);
  if (action.defaultModel.size() > 1024 ||
      trim(action.defaultModel) != action.defaultModel ||
      std::any_of(
          action.defaultModel.begin(), action.defaultModel.end(),
          [](unsigned char c) { return c < 32 || c == '<' || c == '>'; }))
    fail("invalid default model name");

  std::size_t records = 0;
  for (const auto &[id, channel] : action.channels) {
    if (id < 0 || channel.reference < 0 || channel.keys.empty())
      fail("invalid compact position channel");
    std::int64_t previous = -1;
    for (const auto &[seconds, offset] : channel.keys) {
      const auto ticks = actionTicks(seconds);
      if (++records > MaxPositionKeys || ticks <= previous ||
          ticks > duration || !validOffset(offset))
        fail("invalid, duplicate or excessive position keys");
      previous = ticks;
    }
  }
}

void readChannels(const Document &d, const SourceNode &node, Action &action) {
  if (!node.children.empty()) fail("unsupported compact muscle channel data");
  std::istringstream input(textContent(d, node));
  std::string line;
  std::size_t records = 0;
  while (std::getline(input, line)) {
    if (trim(line).empty()) continue;
    std::istringstream in(line);
    in.imbue(std::locale::classic());
    std::string id, reference, time;
    if (!(in >> id >> reference)) fail("incomplete compact position channel");
    MuscleChannel channel;
    channel.reference = readUnsigned(reference);
    double previous   = -1;
    while (in >> time) {
      const double seconds = readTime(time);
      const auto offset    = readVector(in);
      if (seconds <= previous || ++records > MaxPositionKeys)
        fail("out-of-order, duplicate or excessive position keys");
      channel.keys.emplace(seconds, offset);
      previous = seconds;
    }
    if (channel.keys.empty() ||
        !action.channels.emplace(readUnsigned(id), std::move(channel)).second)
      fail("empty or duplicate compact position channel");
  }
}

// Resolve by source ID and verify the owning spline. A CP track always
// targets the one position owner at a welded point.
std::map<Id, std::size_t> actionTargets(const Action &action,
                                        const Model &model) {
  std::map<Id, std::size_t> result;
  if (action.channels.empty()) return result;
  for (const auto &spline : model.splines) {
    if (spline.points.empty()) continue;
    const auto reference = model.points.at(spline.points.front()).id;
    for (const auto i : spline.points) {
      const auto &point  = model.points.at(i);
      const auto channel = action.channels.find(point.id);
      if (channel == action.channels.end()) continue;
      if (point.head != i || channel->second.reference != reference ||
          !result.emplace(point.id, i).second)
        fail("point " + std::to_string(point.id) +
             " does not own a position on the expected spline");
    }
  }
  if (result.size() != action.channels.size())
    fail("one or more control points are missing from the model");
  return result;
}

Vec3 sample(const MuscleChannel &channel, double seconds) {
  const auto &keys = channel.keys;
  if (keys.empty()) fail("empty position channel");
  const auto next = keys.lower_bound(seconds);
  if (next == keys.begin() || (next != keys.end() && next->first == seconds))
    return next->second;
  if (next == keys.end()) return keys.rbegin()->second;
  const auto previous = std::prev(next);
  const double u =
      (seconds - previous->first) / (next->first - previous->first);
  Vec3 result{};
  for (int axis = 0; axis < 3; ++axis)
    result[axis] = previous->second[axis] * (1 - u) + next->second[axis] * u;
  return result;
}

}  // namespace

Action parseAction(std::string source) {
  const auto d     = parseSource(std::move(source), "ACTIONFILE");
  const auto &root = d.nodes.front();
  for (const auto &[name, value] : properties(d, root)) {
    if (name != "ProductVersion" && name != "Release" && name != "FileInfoPos")
      fail("unsupported file property " + name);
  }

  const SourceNode *actionNode = nullptr;
  for (const auto i : root.children) {
    const auto &node = d.nodes[i];
    if (node.name == "ACTIONS") {
      if (actionNode || node.children.size() != 1 ||
          !trim(textContent(d, node)).empty() ||
          d.nodes[node.children.front()].name != "ACTION")
        fail("expected exactly one ACTION");
      actionNode = &d.nodes[node.children.front()];
    } else if (node.name == "POSTEFFECTS" || node.name == "IMAGES" ||
               node.name == "SOUNDS" || node.name == "MATERIALS" ||
               node.name == "OBJECTS" || node.name == "CHOREOGRAPHIES") {
      expectEmpty(d, node);
    } else {
      fail("unsupported section " + node.name);
    }
  }
  if (!actionNode) fail("missing ACTION");

  Action result;
  bool hasDuration = false;
  std::string playRange;
  for (const auto &[name, value] : properties(d, *actionNode)) {
    if (name == "DefaultModel") {
      result.defaultModel = value;
    } else if (name == "Frames") {
      hasDuration     = true;
      result.duration = readTime(value);
    } else if (name == "PlayRange") {
      playRange = value;
    } else if (name == "StrideStartPosition" || name == "StrideEndPosition") {
      std::istringstream in(value);
      in.imbue(std::locale::classic());
      std::string extra;
      if (readVector(in) != Vec3{} || in >> extra)
        fail("stride motion is unsupported");
    } else {
      fail("unsupported action property " + name);
    }
  }

  bool hasSplines = false;
  for (const auto i : actionNode->children) {
    const auto &node = d.nodes[i];
    if (node.name == "FileInfo" && node.children.empty()) continue;
    if (node.name != "SPLINECONTAINERSHORTCUT" || hasSplines ||
        !trim(textContent(d, node)).empty())
      fail("unsupported action section " + node.name);
    hasSplines = true;
    if (node.children.empty()) continue;
    if (node.children.size() != 1 ||
        d.nodes[node.children.front()].name != "COMPACTMUSCLECHANNELS")
      fail("unsupported animation channels");
    readChannels(d, d.nodes[node.children.front()], result);
  }
  if (!hasDuration)
    for (const auto &[id, channel] : result.channels)
      result.duration = std::max(result.duration, channel.keys.rbegin()->first);

  if (!playRange.empty()) {
    std::istringstream in(playRange);
    std::string first, last, extra;
    if (!(in >> first >> last) || in >> extra || readTime(first) != 0 ||
        readTime(last) != result.duration)
      fail("only full-range actions are supported");
  }
  checkAction(result);
  return result;
}

std::string writeAction(const Action &action) {
  checkAction(action);
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(std::numeric_limits<double>::max_digits10)
      << "<ACTIONFILE>\r\nProductVersion=19.5\r\nRelease=19.5 PC\r\n"
         "<POSTEFFECTS>\r\n</POSTEFFECTS>\r\n<IMAGES>\r\n</IMAGES>\r\n"
         "<SOUNDS>\r\n</SOUNDS>\r\n<MATERIALS>\r\n</MATERIALS>\r\n"
         "<OBJECTS>\r\n</OBJECTS>\r\n<ACTIONS>\r\n<ACTION>\r\n"
         "StrideStartPosition=0 0 0\r\nStrideEndPosition=0 0 0\r\n";
  if (!action.defaultModel.empty())
    out << "DefaultModel=" << action.defaultModel << "\r\n";
  const auto duration = actionTicks(action.duration);
  out << "PlayRange=0 ";
  writeTime(out, duration);
  out << "\r\nFrames=";
  writeTime(out, duration);
  out << "\r\n<SPLINECONTAINERSHORTCUT>\r\n<COMPACTMUSCLECHANNELS>\r\n";
  for (const auto &[id, channel] : action.channels) {
    out << id << ' ' << channel.reference;
    for (const auto &[seconds, offset] : channel.keys) {
      out << ' ';
      writeTime(out, actionTicks(seconds));
      out << ' ' << offset[0] << ' ' << offset[1] << ' ' << offset[2];
    }
    out << "\r\n";
  }
  out << "</COMPACTMUSCLECHANNELS>\r\n</SPLINECONTAINERSHORTCUT>\r\n";
  const auto fileInfoPos = static_cast<std::streamoff>(out.tellp());
  out << "<FileInfo>\r\nEmbedded=FALSE\r\n</FileInfo>\r\n</ACTION>\r\n"
         "</ACTIONS>\r\n<CHOREOGRAPHIES>\r\n</CHOREOGRAPHIES>\r\nFileInfoPos="
      << fileInfoPos << "\r\n</ACTIONFILE>\r\n";
  auto result = out.str();
  if (result.size() > Limits{}.bytes) fail("encoded action exceeds file limit");
  return result;
}

double canonicalActionTime(double seconds) {
  return double(actionTicks(seconds)) / TicksPerSecond;
}

void validateAction(const Action &action, const Model &model) {
  checkAction(action);
  actionTargets(action, model);
}

void applyAction(const Action &action, Model &model, double seconds) {
  if (!std::isfinite(seconds)) fail("time is out of range");
  const auto time    = canonicalActionTime(std::max(0.0, seconds));
  const auto targets = actionTargets(action, model);
  std::vector<std::pair<std::size_t, Vec3>> positions;
  positions.reserve(targets.size());
  for (const auto &[id, i] : targets) {
    auto offset = sample(action.channels.at(id), time);
    if (!validOffset(offset)) fail("invalid sampled position offset");
    auto position = model.points[i].position;
    for (int axis = 0; axis < 3; ++axis) {
      position[axis] += offset[axis];
      if (!std::isfinite(position[axis]))
        fail("invalid resulting point position");
    }
    positions.emplace_back(i, position);
  }
  for (const auto &[i, position] : positions)
    model.points[i].position = position;
}

void setPositionKey(Action &action, const Model &model, Id id, double seconds,
                    const Vec3 &offset) {
  const auto time = canonicalActionTime(seconds);
  if (!validOffset(offset)) fail("invalid position offset");
  const auto point =
      std::find_if(model.points.begin(), model.points.end(),
                   [id](const auto &candidate) { return candidate.id == id; });
  if (point == model.points.end())
    fail("control point is missing from the model");

  const auto head   = point->head;
  const auto headId = model.points.at(head).id;
  const auto spline = std::find_if(
      model.splines.begin(), model.splines.end(), [head](const auto &spline) {
        return std::find(spline.points.begin(), spline.points.end(), head) !=
               spline.points.end();
      });
  if (spline == model.splines.end()) fail("control point is outside a spline");
  const auto reference  = model.points.at(spline->points.front()).id;
  const auto existing   = action.channels.find(headId);
  const bool newChannel = existing == action.channels.end();
  const std::size_t added =
      newChannel ? (time > 0 ? 2 : 1) : !existing->second.keys.count(time);
  if (added) {
    std::size_t count = added;
    for (const auto &entry : action.channels) {
      if (entry.second.keys.size() > MaxPositionKeys - count)
        fail("position key limit exceeded");
      count += entry.second.keys.size();
    }
  }
  if (newChannel) {
    MuscleChannel channel;
    channel.reference = reference;
    if (time > 0) channel.keys.emplace(0, Vec3{});
    channel.keys.emplace(time, offset);
    action.channels.emplace(headId, std::move(channel));
  } else {
    if (existing->second.reference != reference)
      fail("control point does not belong to the expected spline");
    existing->second.keys[time] = offset;
  }
  action.duration = std::max(action.duration, time);
}

}  // namespace otam

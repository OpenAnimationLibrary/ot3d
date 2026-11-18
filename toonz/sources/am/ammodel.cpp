#include "ammodel.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <locale>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace otam {
namespace {
[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error("A:M: " + message);
}
std::string trim(const std::string &s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
std::vector<std::string> words(const std::string &s) {
  std::istringstream in(s);
  in.imbue(std::locale::classic());
  std::vector<std::string> result;
  std::string word;
  while (in >> word) result.push_back(word);
  return result;
}
template <typename T>
T number(const std::string &s) {
  std::istringstream in(s);
  in.imbue(std::locale::classic());
  T v{};
  char extra;
  if (!(in >> v) || (in >> extra) || !std::isfinite(double(v)))
    fail("invalid numeric record: " + s.substr(0, 80));
  return v;
}
struct SourceLine {
  std::string text;
  std::size_t offset;
};
std::vector<SourceLine> sourceLines(const Document &d, const SourceNode &n) {
  std::vector<SourceLine> result;
  const std::string_view source(d.source);
  const auto append = [&](std::size_t begin, std::size_t end) {
    auto remaining = source.substr(begin, end - begin);
    while (!remaining.empty()) {
      const auto line  = remaining.substr(0, remaining.find('\n'));
      const auto first = line.find_first_not_of(" \t\r");
      if (first != std::string_view::npos) {
        const auto last = line.find_last_not_of(" \t\r");
        result.push_back(
            {std::string(line.substr(first, last - first + 1)), begin + first});
      }
      if (line.size() == remaining.size()) break;
      remaining.remove_prefix(line.size() + 1);
      begin += line.size() + 1;
    }
  };
  std::size_t offset = n.contentBegin;
  for (auto child : n.children) {
    const auto &c = d.nodes[child];
    append(offset, c.begin);
    offset = c.end;
  }
  append(offset, n.contentEnd);
  return result;
}
std::vector<std::string> lines(const Document &d, const SourceNode &n) {
  std::vector<std::string> result;
  for (auto &line : sourceLines(d, n)) result.push_back(std::move(line.text));
  return result;
}

std::size_t wordOffset(const std::string &line, std::size_t index) {
  constexpr auto whitespace = " \t\r\v\f";
  std::size_t pos           = 0;
  for (std::size_t i = 0; i < index; ++i)
    pos =
        line.find_first_not_of(whitespace, line.find_first_of(whitespace, pos));
  return pos;
}

std::size_t sourceLineNumber(const Document &d, std::size_t offset) {
  return 1 + std::count(d.source.begin(), d.source.begin() + offset, '\n');
}

std::string expandBiasDefaults(const Document &d,
                               const std::vector<std::size_t> &dots,
                               const Limits &limits) {
  constexpr std::string_view defaultBias = "0 0 1";
  constexpr auto extraBytes              = defaultBias.size() - 1;
  if (dots.size() > (limits.bytes - d.source.size()) / extraBytes)
    fail("repaired model would exceed the file size limit");
  std::string result;
  result.reserve(d.source.size() + extraBytes * dots.size());
  std::size_t offset = 0;
  for (const auto dot : dots) {
    result.append(d.source, offset, dot - offset);
    result += defaultBias;
    offset = dot + 1;
  }
  result.append(d.source, offset, std::string::npos);

  // A:M stores the byte offset of the FileInfo tag in the footer. Update
  // it when it points to a real section whose position has shifted.
  const std::string key = "FileInfoPos=";
  for (const auto &line : sourceLines(d, d.nodes.front())) {
    if (line.text.compare(0, key.size(), key) != 0) continue;
    const auto value = line.text.substr(key.size());
    const auto info  = std::find_if(
         d.nodes.begin(), d.nodes.end(), [&](const SourceNode &node) {
          return node.name == "FileInfo" && node.begin < line.offset &&
                 std::to_string(node.begin) == value;
        });
    if (info == d.nodes.end()) continue;
    const auto shift = [&](std::size_t pos) {
      return extraBytes *
             (std::lower_bound(dots.begin(), dots.end(), pos) - dots.begin());
    };
    result.replace(line.offset + key.size() + shift(line.offset), value.size(),
                   std::to_string(info->begin + shift(info->begin)));
    break;
  }
  if (result.size() > limits.bytes)
    fail("repaired model would exceed the file size limit");
  return result;
}
void warn(std::vector<std::string> &warnings, const std::string &message) {
  if (std::find(warnings.begin(), warnings.end(), message) == warnings.end())
    warnings.push_back(message);
}
Vec3 add(const Vec3 &a, const Vec3 &b) {
  return {{a[0] + b[0], a[1] + b[1], a[2] + b[2]}};
}
Vec3 sub(const Vec3 &a, const Vec3 &b) {
  return {{a[0] - b[0], a[1] - b[1], a[2] - b[2]}};
}
Vec3 scale(const Vec3 &a, double s) { return {{a[0] * s, a[1] * s, a[2] * s}}; }
double length(const Vec3 &a) {
  return std::hypot(a[0], std::hypot(a[1], a[2]));
}
double dot(const Vec3 &a, const Vec3 &b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vec3 cross(const Vec3 &a, const Vec3 &b) {
  return {{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
           a[0] * b[1] - a[1] * b[0]}};
}
Vec3 unit(const Vec3 &v) {
  const double size = length(v);
  return size > 1e-12 ? scale(v, 1 / size) : Vec3{};
}
constexpr double Radians = 3.14159265358979323846 / 180.0;
Vec3 mix(const Vec3 &a, const Vec3 &b, double t) {
  return add(scale(a, 1 - t), scale(b, t));
}
std::map<Id, std::size_t> index(const Model &m) {
  std::map<Id, std::size_t> ids;
  for (std::size_t i = 0; i < m.points.size(); ++i)
    if (!ids.emplace(m.points[i].id, i).second) fail("duplicate CP identifier");
  return ids;
}
void resolve(Model &m) {
  const auto ids = index(m);
  std::vector<unsigned char> state(m.points.size(), 0);
  for (std::size_t i = 0; i < m.points.size(); ++i) {
    std::vector<std::size_t> chain;
    std::size_t p = i;
    while (!state[p]) {
      state[p] = 1;
      chain.push_back(p);
      if (m.points[p].attachedTo < 0) {
        m.points[p].head = p;
        state[p]         = 2;
        break;
      }
      const auto found = ids.find(m.points[p].attachedTo);
      if (found == ids.end()) fail("attachment references a missing CP");
      p = found->second;
    }
    if (state[p] == 1) fail("cyclic CP attachment");
    const std::size_t head = m.points[p].head;
    for (auto cp : chain) {
      m.points[cp].head = head;
      state[cp]         = 2;
    }
  }
  for (const auto &p : m.patches)
    for (auto cp : p.points)
      if (!ids.count(cp)) fail("patch references a missing CP");
}

struct HookRecord {
  std::size_t point, offset;
  Id target;
  double fraction;
};

void resolveSplineHooks(Model &model, const Document &document,
                        const std::vector<HookRecord> &records) {
  if (records.empty()) return;
  const auto ids = index(model);
  using Location = std::pair<std::size_t, std::size_t>;
  std::vector<Location> occurrences(model.points.size());
  for (std::size_t s = 0; s < model.splines.size(); ++s)
    for (std::size_t i = 0; i < model.splines[s].points.size(); ++i)
      occurrences[model.splines[s].points[i]] = {s, i};

  std::vector<SplineHook> hooks;
  std::vector<std::size_t> hookByPoint(model.points.size(), records.size());
  for (const auto &record : records) {
    const auto target  = ids.find(record.target);
    const auto context = [&] {
      return " at CP " + std::to_string(model.points[record.point].id) +
             " (line " +
             std::to_string(sourceLineNumber(document, record.offset)) + ")";
    };
    if (target == ids.end()) fail("missing spline hook target" + context());
    const auto [spline, segment] = occurrences[target->second];
    const auto &host             = model.splines[spline];
    if (!host.closed && segment + 1 == host.points.size())
      fail("spline hook target has no outgoing segment" + context());
    hookByPoint[record.point] = hooks.size();
    hooks.push_back({record.point, spline, segment, record.fraction});
  }

  // The cubic depends on both endpoints and their adjacent spline points.
  // Evaluate dependent hooks after their hosts and reject cycles without
  // recursing through untrusted input.
  std::vector<std::vector<std::size_t>> dependents(hooks.size());
  std::vector<unsigned> remaining(hooks.size());
  std::vector<std::size_t> ready;
  for (std::size_t i = 0; i < hooks.size(); ++i) {
    const auto &hook = hooks[i];
    const auto &host = model.splines[hook.spline];
    const auto size  = host.points.size();
    const auto start = hook.segment, end = (start + 1) % size;
    for (const auto index :
         {start, end,
          (start || host.closed) ? (start + size - 1) % size : start,
          (end + 1 < size || host.closed) ? (end + 1) % size : end}) {
      const auto dependency =
          hookByPoint[model.points[host.points[index]].head];
      if (dependency == hooks.size()) continue;
      dependents[dependency].push_back(i);
      ++remaining[i];
    }
    if (!remaining[i]) ready.push_back(i);
  }
  for (std::size_t i = 0; i < ready.size(); ++i) {
    const auto hook = ready[i];
    model.splineHooks.push_back(hooks[hook]);
    for (const auto next : dependents[hook])
      if (!--remaining[next]) ready.push_back(next);
  }
  if (ready.size() != hooks.size()) fail("cyclic spline hook dependency");
  updateSplineHooks(model);
}
void readBoneSegment(const Document &d, const SourceNode &node, Model &model,
                     const std::map<Id, std::size_t> &ids,
                     std::set<std::string> &paths, int parent,
                     std::size_t &records, const Limits &limits) {
  if (++records > limits.records) fail("too many bone records");

  NativeBone bone;
  std::optional<Vec3> start, end;
  std::optional<Quaternion> orientation;
  std::optional<double> boneLength;
  for (const auto &line : lines(d, node)) {
    const auto sep = line.find('=');
    if (sep == std::string::npos) continue;
    const auto property = line.substr(0, sep);
    const auto value    = line.substr(sep + 1);
    if (property == "Name") bone.name = value;
    if (property == "Length") boneLength = number<double>(value);
    if (property == "Start" || property == "End") {
      const auto fields = words(value);
      if (fields.size() != 3) fail("invalid bone endpoint");
      Vec3 point;
      for (int i = 0; i < 3; ++i) {
        point[i] = number<double>(fields[i]);
        if (std::abs(point[i]) > 1e9) fail("bone coordinate exceeds limit");
      }
      (property == "Start" ? start : end) = point;
    }
    if (property == "Rotate") {
      const auto fields = words(value);
      if (fields.size() != 4) fail("invalid bone rotation");
      orientation.emplace();
      for (int i = 0; i < 4; ++i) (*orientation)[i] = number<double>(fields[i]);
    }
    if (property == "BoneColor") {
      const auto fields = words(value);
      if (fields.size() != 3 && fields.size() != 4) fail("invalid bone color");
      for (int i = 0; i < 3; ++i)
        bone.color[i] =
            float(std::clamp(number<double>(fields[i]), 0.0, 255.0) / 255.0);
    }
  }

  bone.parent = parent;
  if (parent >= 0) {
    const auto &parentBone = model.bones[parent];
    bone.path              = parentBone.path;
    if (!start)
      start = transform(inverse(parentBone.inverseBind), parentBone.tip);
  }
  if (bone.name.empty() || !start || !orientation || (!end && !boneLength))
    fail("incomplete bone rest transform");
  if (boneLength && (*boneLength < 0 || *boneLength > 2e9))
    fail("bone length exceeds limit");
  // A length prefix keeps sibling paths distinct even with slashes in names.
  bone.path += std::to_string(bone.name.size()) + ":" + bone.name;
  if (!paths.insert(bone.path).second) fail("duplicate bone path");

  const auto bind = multiply(translation((*start)[0], (*start)[1], (*start)[2]),
                             rotation(*orientation));
  bone.inverseBind = inverse(bind);
  bone.restLocal =
      parent < 0 ? bind : multiply(model.bones[parent].inverseBind, bind);
  bone.tip = end ? transform(bone.inverseBind, *end) : Vec3{0, 0, *boneLength};
  const int current = int(model.bones.size());
  model.bones.push_back(std::move(bone));

  for (auto i : node.children) {
    const auto &child = d.nodes[i];
    if (child.name == "SEGMENT") {
      readBoneSegment(d, child, model, ids, paths, current, records, limits);
    } else if (child.name == "NONSKINNEDCPS") {
      for (const auto &line : lines(d, child))
        for (const auto &word : words(line)) {
          if (++records > limits.records) fail("too many bone assignments");
          const auto cp = ids.find(number<Id>(word));
          if (cp == ids.end()) fail("bone references a missing CP");
          auto &owner = model.boneByHead[model.points[cp->second].head];
          if (owner >= 0 && owner != current)
            fail("conflicting bone assignments for a welded position");
          owner = current;
        }
    } else if (child.name == "WEIGHTEDCPS" &&
               (!child.children.empty() || !lines(d, child).empty())) {
      fail("weighted bone assignments are unsupported");
    }
  }
}

}  // namespace

Document parseSource(std::string source, const std::string &root,
                     const Limits &limits) {
  if (source.empty() || source.size() > limits.bytes)
    fail("file size exceeds limit or is empty");
  if (source.find('\0') != std::string::npos)
    fail("binary/UTF-16 files are not supported");
  Document d;
  d.source = std::move(source);
  std::vector<std::size_t> stack;
  std::size_t pos = 0;
  while ((pos = d.source.find('<', pos)) != std::string::npos) {
    if (d.source.compare(pos, 4, "<!--") == 0) {
      const auto end = d.source.find("-->", pos + 4);
      if (end == std::string::npos) fail("unterminated comment");
      pos = end + 3;
      continue;
    }
    if (d.source.compare(pos, 2, "<?") == 0) {
      const auto end = d.source.find("?>", pos + 2);
      if (end == std::string::npos) fail("unterminated declaration");
      pos = end + 2;
      continue;
    }
    if (d.source.compare(pos, 2, "<!") == 0)
      fail("DTD, entities and CDATA are not supported");
    const auto end = d.source.find('>', pos + 1);
    if (end == std::string::npos) fail("unterminated tag");
    std::string tag  = trim(d.source.substr(pos + 1, end - pos - 1));
    const bool close = !tag.empty() && tag[0] == '/';
    const bool empty = !tag.empty() && tag.back() == '/';
    if (close) tag = trim(tag.substr(1));
    if (empty) tag = trim(tag.substr(0, tag.size() - 1));
    // Native fixtures use simple tags. Do not guess the semantics of
    // attributes.
    if (tag.empty() ||
        tag.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUV"
                              "WXYZ0123456789_:-") != std::string::npos)
      fail("unsupported tag syntax");
    if (close) {
      if (stack.empty() || d.nodes[stack.back()].name != tag)
        fail("mismatched closing tag");
      auto &node      = d.nodes[stack.back()];
      node.contentEnd = pos;
      node.end        = end + 1;
      stack.pop_back();
    } else {
      if (stack.size() >= limits.depth || d.nodes.size() >= limits.records)
        fail("document structure exceeds limit");
      if (stack.empty() && !d.nodes.empty()) fail("multiple root elements");
      const std::size_t id = d.nodes.size();
      if (!stack.empty()) d.nodes[stack.back()].children.push_back(id);
      d.nodes.push_back({tag, pos, end + 1, end + 1, end + 1, {}});
      if (!empty) stack.push_back(id);
    }
    pos = end + 1;
  }
  if (!stack.empty() || d.nodes.empty() || d.nodes[0].name != root)
    fail("expected a complete " + root + " document");
  for (const auto &line : lines(d, d.nodes[0]))
    if (line.compare(0, 15, "ProductVersion=") == 0)
      d.version = line.substr(15);
  return d;
}

MixedBiasError::MixedBiasError(MixedBiasRepair repair)
    : std::runtime_error(
          "A:M: mixed default and custom spline tangents at CP " +
          std::to_string(repair.firstPoint) + " (line " +
          std::to_string(repair.firstLine) + ")")
    , repair(std::move(repair)) {}

Document parse(std::string source, const Limits &limits) {
  Document d = parseSource(std::move(source), "MODELFILE", limits);
  if (d.version != "19.5")
    warn(d.warnings,
         "File version is not the 19.5 reference dialect; compatibility is "
         "experimental.");
  std::size_t records = 0;
  std::vector<std::size_t> mixedBiasDots;
  std::size_t firstMixedOffset = d.source.size();
  Id firstMixedPoint           = 0;
  for (std::size_t ni = 0; ni < d.nodes.size(); ++ni) {
    if (d.nodes[ni].name != "MODEL") continue;
    Model m;
    std::vector<HookRecord> hookRecords;
    for (const auto &line : lines(d, d.nodes[ni]))
      if (line.compare(0, 5, "Name=") == 0) m.name = line.substr(5);
    // Descendants are in source order. Scope every record to its MODEL.
    for (std::size_t j = ni + 1;
         j < d.nodes.size() && d.nodes[j].begin < d.nodes[ni].end; ++j) {
      const auto &n = d.nodes[j];
      if (n.name == "SPLINE") {
        Spline spline;
        for (const auto &[line, lineOffset] : sourceLines(d, n)) {
          const auto w = words(line);
          if (w.size() < 4 || ++records > limits.records)
            fail("invalid or excessive CP records");
          ControlPoint cp;
          cp.flags       = number<std::uint32_t>(w[0]);
          const int kind = number<int>(w[1]);
          cp.id          = number<Id>(w[2]);
          if (cp.id < 0) fail("negative CP identifier");
          cp.sourceRecord = line;
          std::size_t biasOffset;
          if (kind == 0) {
            if (w.size() < 6) fail("incomplete CP position");
            for (int k = 0; k < 3; ++k) {
              cp.position[k] = number<double>(w[3 + k]);
              if (std::abs(cp.position[k]) > 1e9)
                fail("CP coordinate exceeds limit");
            }
            biasOffset = 6;
          } else if (kind == 1) {
            constexpr std::uint32_t hookFlag = 0x10u;
            if ((cp.flags & hookFlag) && w.size() == 5) {
              const auto fraction = number<double>(w[3]);
              const auto target   = number<Id>(w[4]);
              if (fraction <= 0 || fraction >= 1 || target < 0)
                fail("invalid spline hook at CP " + std::to_string(cp.id) +
                     " (line " +
                     std::to_string(sourceLineNumber(d, lineOffset)) + ")");
              hookRecords.push_back(
                  {m.points.size(), lineOffset, target, fraction});
              cp.isSplineHook = true;
              biasOffset      = 5;
            } else {
              cp.attachedTo = number<Id>(w[3]);
              if (cp.attachedTo < 0) fail("invalid attachment identifier");
              biasOffset = 4;
            }
          } else
            fail(
                "unsupported CP record kind (hook/special record); source left "
                "unchanged");
          // A:M can abbreviate either complete tangent with a dot. When
          // exactly one side uses that form, report a lossless source repair.
          if (w.size() == biasOffset + 4 &&
              ((w[biasOffset] == ".") != (w.back() == "."))) {
            const bool defaultIncoming = w[biasOffset] == ".";
            const auto customStart     = biasOffset + (defaultIncoming ? 1 : 0);
            for (std::size_t k = 0; k < 3; ++k)
              cp.bias[(defaultIncoming ? 3 : 0) + k] =
                  number<double>(w[customStart + k]);
            const auto dot    = biasOffset + (defaultIncoming ? 0 : 3);
            const auto offset = lineOffset + wordOffset(line, dot);
            if (offset < firstMixedOffset) {
              firstMixedOffset = offset;
              firstMixedPoint  = cp.id;
            }
            mixedBiasDots.push_back(offset);
          } else if (!(w.size() == biasOffset + 2 && w[biasOffset] == "." &&
                       w[biasOffset + 1] == ".")) {
            if (w.size() != biasOffset && w.size() != biasOffset + 6)
              fail("unsupported bias record layout");
            for (std::size_t k = biasOffset; k < w.size(); ++k)
              if (w[k] != ".") cp.bias[k - biasOffset] = number<double>(w[k]);
          }
          if (cp.flags != 262145 && cp.flags != 262149)
            warn(d.warnings,
                 "Special CP flags are retained, but their surface behavior is "
                 "not evaluated.");
          // The loop bit is observed in the 19.5 closed-spline fixtures.
          spline.closed = spline.closed || (cp.flags & 4u);
          spline.points.push_back(m.points.size());
          m.points.push_back(std::move(cp));
        }
        if (spline.points.size() < 2)
          fail("a spline must contain at least two CPs");
        m.splines.push_back(std::move(spline));
      } else if (n.name == "PATCHES") {
        for (const auto &line : lines(d, n)) {
          auto w = words(line);
          if (w.size() < 4 || ++records > limits.records)
            fail("invalid or excessive patch records");
          Patch p;
          p.flags        = number<std::uint32_t>(w[0]);
          p.sourceRecord = line;
          // The 19.5 rich four-corner record carries trailing normal indices.
          // Preserve those bytes; never interpret them as more CP identifiers.
          const std::size_t cornerEnd = w.size() == 10 ? 5 : w.size();
          for (std::size_t k = 1; k < cornerEnd; ++k)
            p.points.push_back(number<Id>(w[k]));
          m.patches.push_back(std::move(p));
        }
      } else if (n.name == "GROUP") {
        Group g;
        for (const auto &line : lines(d, n)) {
          if (line.compare(0, 5, "Name=") == 0) g.name = line.substr(5);
          if (line.compare(0, 13, "DiffuseColor=") == 0) {
            const auto rgb = words(line.substr(13));
            if (rgb.size() != 3) fail("invalid group diffuse color");
            for (int k = 0; k < 3; ++k)
              g.color[k] =
                  float(std::max(0.0, std::min(255.0, number<double>(rgb[k]))) /
                        255.0);
            g.hasColor = true;
          }
        }
        for (auto child : n.children)
          if (d.nodes[child].name == "CPS")
            for (const auto &line : lines(d, d.nodes[child]))
              for (const auto &w : words(line)) {
                if (++records > limits.records)
                  fail("group membership exceeds limit");
                g.points.push_back(number<Id>(w));
              }
        m.groups.push_back(std::move(g));
      } else if (n.name == "RELATIONSHIPS" &&
                 (!n.children.empty() || !lines(d, n).empty())) {
        warn(d.warnings, "A:M relationships are preserved but not evaluated.");
      }
    }
    if (m.points.empty()) fail("model has no supported spline geometry");
    resolve(m);
    resolveSplineHooks(m, d, hookRecords);
    try {
      std::set<std::string> paths;
      for (auto i : d.nodes[ni].children) {
        const auto &rig = d.nodes[i];
        if (rig.name != "BONES" || rig.children.empty()) continue;
        m.boneByHead.resize(m.points.size(), -1);
        const auto ids = index(m);
        for (auto j : rig.children) {
          const auto &segment = d.nodes[j];
          if (segment.name != "SEGMENT") fail("unsupported bone record");
          readBoneSegment(d, segment, m, ids, paths, -1, records, limits);
        }
      }
    } catch (const std::runtime_error &e) {
      m.bones.clear();
      m.boneByHead.clear();
      warn(d.warnings, std::string("Native bone rig skipped: ") + e.what());
    }
    if (m.patches.empty())
      warn(d.warnings,
           "No explicit patches: native patch discovery is not implemented.");
    d.models.push_back(std::move(m));
  }
  if (d.models.empty()) fail("no spline/patch MODEL was found");
  if (!mixedBiasDots.empty()) {
    if (d.models.size() != 1)
      fail(
          "mixed spline tangents in a multi-model file; a project importer "
          "is required");
    std::sort(mixedBiasDots.begin(), mixedBiasDots.end());
    throw MixedBiasError(
        {expandBiasDefaults(d, mixedBiasDots, limits), mixedBiasDots.size(),
         sourceLineNumber(d, firstMixedOffset), firstMixedPoint});
  }
  for (const auto &n : d.nodes)
    if ((n.name == "MATERIALS" || n.name == "ACTIONS" ||
         n.name == "CHOREOGRAPHIES" || n.name == "DECALS") &&
        (!n.children.empty() || !lines(d, n).empty()))
      warn(d.warnings,
           "Materials, decals, actions and choreography are retained as source "
           "records, not evaluated.");
  return d;
}

Vec3 BiasHandle::point() const {
  const double alpha = std::remainder(bias[0], 360.0) * Radians;
  const double gamma = std::remainder(bias[1], 360.0) * Radians;
  const auto inPlane =
      add(scale(forward, std::cos(gamma)), scale(side, std::sin(gamma)));
  const auto direction =
      add(scale(inPlane, std::cos(alpha)), scale(normal, std::sin(alpha)));
  return add(origin,
             scale(direction, length * std::clamp(bias[2], -100.0, 100.0)));
}

Bias BiasHandle::biasAt(const Vec3 &target, bool magnitudeOnly) const {
  if (length <= 1e-12) return bias;
  const auto delta = sub(target, origin);
  auto result      = bias;
  if (magnitudeOnly) {
    auto reference       = *this;
    reference.bias[2]    = bias[2] < 0 ? -1 : 1;
    const auto direction = scale(sub(reference.point(), origin), 1 / length);
    const double amount =
        std::clamp(dot(delta, direction) / length, 0.0, 100.0);
    result[2] = bias[2] < 0 ? -amount : amount;
    return result;
  }
  const double x = dot(delta, forward), y = dot(delta, side);
  const double z      = dot(delta, normal);
  const double planar = std::hypot(x, y);
  const double size   = std::hypot(planar, z);
  result[2]           = std::min(100.0, size / length);
  if (size > 1e-12) {
    result[0] = std::atan2(z, planar) / Radians;
    if (planar > 1e-12) result[1] = std::atan2(y, x) / Radians;
  }
  return result;
}

std::array<Vec3, 4> SplineSegment::bezier() const {
  return {
      {outgoing.origin, outgoing.point(), incoming.point(), incoming.origin}};
}
Vec3 sampleSpline(const std::array<Vec3, 4> &p, double t) {
  const double u = 1 - t;
  return add(add(scale(p[0], u * u * u), scale(p[1], 3 * u * u * t)),
             add(scale(p[2], 3 * u * t * t), scale(p[3], t * t * t)));
}

SplineSegment splineSegment(const Model &m, const Spline &s,
                            std::size_t start) {
  const auto size     = s.points.size();
  const auto end      = (start + 1) % size;
  const auto position = [&](std::size_t i) -> const Vec3 & {
    return m.points[m.points[s.points[i]].head].position;
  };
  const double span = length(sub(position(end), position(start))) / 3;
  const auto handle = [&](std::size_t i, bool incoming) {
    const Vec3 origin = position(i);
    const Vec3 prev =
        (i > 0 || s.closed) ? position((i + size - 1) % size) : origin;
    const Vec3 next =
        (i + 1 < size || s.closed) ? position((i + 1) % size) : origin;
    Vec3 forward = unit(incoming ? sub(prev, next) : sub(next, prev));
    if (forward == Vec3{})
      forward = unit(incoming ? sub(prev, origin) : sub(next, origin));
    Vec3 normal = unit(cross(unit(sub(origin, prev)), unit(sub(next, origin))));
    if (normal == Vec3{}) {
      // A straight or open-ended spline has no unique plane.
      const auto axis =
          std::abs(forward[2]) < 0.9 ? Vec3{{0, 0, 1}} : Vec3{{0, 1, 0}};
      normal = unit(sub(axis, scale(forward, dot(axis, forward))));
    }
    const auto &b     = m.points[s.points[i]].bias;
    const auto offset = incoming ? 0 : 3;
    return BiasHandle{
        origin, forward, cross(normal, forward),
        normal, span,    {{b[offset], b[offset + 1], b[offset + 2]}}};
  };
  return {s.points[start], s.points[end], handle(start, false),
          handle(end, true)};
}

void updateSplineHooks(Model &model) {
  for (const auto &hook : model.splineHooks) {
    const auto curve =
        splineSegment(model, model.splines[hook.spline], hook.segment).bezier();
    model.points[hook.point].position = sampleSpline(curve, hook.fraction);
  }
}

static std::array<Vec3, 4> splineInterval(const std::array<Vec3, 4> &curve,
                                          double from, double to) {
  const auto tangent = [&](double t) {
    return add(add(scale(sub(curve[1], curve[0]), (1 - t) * (1 - t)),
                   scale(sub(curve[2], curve[1]), 2 * t * (1 - t))),
               scale(sub(curve[3], curve[2]), t * t));
  };
  const auto a = sampleSpline(curve, from), b = sampleSpline(curve, to);
  return {{a, add(a, scale(tangent(from), to - from)),
           sub(b, scale(tangent(to), to - from)), b}};
}

bool applyPlasticBones(const Model &rest, Model &pose,
                       const std::vector<PlasticBone> &bones) {
  struct BoneFrame {
    Vec3 restStart, posedStart, restAxis, posedAxis, restSide, posedSide;
    double restLength, posedLength;
    bool spatial, moves;
  };
  std::vector<BoneFrame> frames;
  frames.reserve(bones.size());
  for (const auto &bone : bones) {
    auto restVector  = sub(bone.restEnd, bone.restStart);
    auto posedVector = sub(bone.posedEnd, bone.posedStart);
    if (!bone.spatial) restVector[2] = posedVector[2] = 0;
    const double restLength        = length(restVector);
    const double posedLength       = length(posedVector);
    if (!std::isfinite(restLength) || !std::isfinite(posedLength) ||
        restLength <= 1e-12)
      continue;
    const auto restAxis = scale(restVector, 1 / restLength);
    const auto posedAxis =
        bone.spatial
            ? bone.posedAxis
            : (posedLength > 1e-12 ? scale(posedVector, 1 / posedLength)
                                   : restAxis);
    const auto restSide =
        bone.spatial ? bone.restSide : Vec3{-restAxis[1], restAxis[0], 0};
    const auto posedSide =
        bone.spatial ? bone.posedSide : Vec3{-posedAxis[1], posedAxis[0], 0};
    frames.push_back(
        {bone.restStart, bone.posedStart, restAxis, posedAxis, restSide,
         posedSide, restLength, posedLength, bone.spatial,
         bone.restStart != bone.posedStart || bone.restEnd != bone.posedEnd ||
             restSide != posedSide});
  }
  if (frames.empty() || rest.points.size() != pose.points.size()) return false;

  bool changed = false;
  for (std::size_t i = 0; i < rest.points.size(); ++i) {
    if (rest.points[i].head != i) continue;
    const BoneFrame *nearest = nullptr;
    double best              = std::numeric_limits<double>::infinity();
    for (const auto &bone : frames) {
      auto fromStart = sub(rest.points[i].position, bone.restStart);
      if (!bone.spatial) fromStart[2] = 0;
      const double along =
          std::clamp(dot(fromStart, bone.restAxis), 0.0, bone.restLength);
      const auto offSegment = sub(fromStart, scale(bone.restAxis, along));
      const double distanceSquared = dot(offSegment, offSegment);
      if (distanceSquared < best) {
        best    = distanceSquared;
        nearest = &bone;
      }
    }
    if (!nearest || !nearest->moves) continue;

    auto &position        = pose.points[i].position;
    const auto fromStart  = sub(position, nearest->restStart);
    const auto &restAxis  = nearest->restAxis;
    const auto &posedAxis = nearest->posedAxis;
    const double along =
        dot(fromStart, restAxis) * nearest->posedLength / nearest->restLength;
    const double across = dot(fromStart, nearest->restSide);
    const auto restUp   = cross(restAxis, nearest->restSide);
    const auto posedUp  = cross(posedAxis, nearest->posedSide);
    auto result         = add(add(nearest->posedStart, scale(posedAxis, along)),
                              scale(nearest->posedSide, across));
    if (nearest->spatial)
      result = add(result, scale(posedUp, dot(fromStart, restUp)));
    else
      result[2] = position[2];

    if (std::all_of(result.begin(), result.end(),
                    [](double value) { return std::isfinite(value); }) &&
        result != position) {
      position = result;
      changed  = true;
    }
  }
  return changed;
}

Mesh previewMesh(const Document &d, std::size_t modelIndex,
                 unsigned subdivisions, const Limits &limits) {
  if (modelIndex >= d.models.size()) fail("invalid preview request");
  return previewMesh(d.models[modelIndex], d.warnings, subdivisions, limits);
}

Mesh previewMesh(const Model &model, const std::vector<std::string> &warnings,
                 unsigned subdivisions, const Limits &limits) {
  if (subdivisions < 1 || subdivisions > 32) fail("invalid preview request");
  Model posed;
  if (!model.splineHooks.empty()) {
    posed = model;
    updateSplineHooks(posed);
  }
  const auto &m  = model.splineHooks.empty() ? model : posed;
  const auto ids = index(m);
  Mesh out;
  out.warnings = warnings;
  out.patchGroups.resize(m.patches.size());
  warn(out.warnings,
       "Experimental A:M control-surface preview; Hash patch/bias evaluation "
       "fidelity is not validated.");
  warn(out.warnings,
       "Only explicit three/four-corner patches are displayed; automatic patch "
       "discovery is not implemented.");
  // Use one curve per welded boundary so neighboring patches share the same
  // samples. Bias is read from the actual occurrence on that spline.
  using Edge = std::pair<std::size_t, std::size_t>;
  std::map<Edge, std::array<Vec3, 4>> curves;
  std::set<Edge> patchEdges;
  for (const auto &patch : m.patches) {
    for (std::size_t i = 0; i < patch.points.size(); ++i) {
      const auto a = m.points[ids.at(patch.points[i])].head;
      const auto b =
          m.points[ids.at(patch.points[(i + 1) % patch.points.size()])].head;
      patchEdges.emplace(std::min(a, b), std::max(a, b));
    }
  }
  std::vector<std::set<std::size_t>> memberships;
  for (const auto &g : m.groups) {
    std::set<std::size_t> heads;
    for (auto id : g.points) {
      auto it = ids.find(id);
      if (it != ids.end()) heads.insert(m.points[it->second].head);
    }
    memberships.push_back(std::move(heads));
  }
  std::vector<std::vector<std::size_t>> pointGroups(m.points.size());
  for (std::size_t g = 0; g < m.groups.size(); ++g) {
    out.splineGroups.push_back(m.groups[g].name);
    for (const auto head : memberships[g]) pointGroups[head].push_back(g);
  }
  for (const auto &s : m.splines) {
    if (s.points.size() < 2) continue;
    auto &spline     = out.splines.emplace_back();
    spline.closed    = s.closed;
    const auto count = s.closed ? s.points.size() : s.points.size() - 1;
    spline.curves.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      auto segment = splineSegment(m, s, i);
      const auto a = m.points[segment.from].head;
      const auto b = m.points[segment.to].head;
      auto points  = segment.bezier();
      auto &curve  = spline.curves.emplace_back();
      curve.points = points;
      // The first welded boundary is the one used by patch tessellation.
      // Other authored splines can share its endpoints and remain unfilled.
      const bool boundary = curves.emplace(Edge(a, b), points).second;
      curve.patchEdge =
          boundary && patchEdges.count({std::min(a, b), std::max(a, b)}) != 0;
      std::set_intersection(pointGroups[a].begin(), pointGroups[a].end(),
                            pointGroups[b].begin(), pointGroups[b].end(),
                            std::back_inserter(curve.groups));
      std::reverse(points.begin(), points.end());
      curves.emplace(Edge(b, a), points);
    }
  }
  if (!m.splineHooks.empty()) {
    // A patch beside a hook can use only part of the host curve as an edge.
    // Reuse that exact cubic interval so the hook meets the host surface.
    using Fractions = std::map<std::size_t, double>;
    std::map<Edge, Fractions> hosts;
    std::map<std::size_t, std::vector<std::size_t>> neighbors;
    for (const auto &hook : m.splineHooks) {
      hosts[{hook.spline, hook.segment}][hook.point] = hook.fraction;
      neighbors.try_emplace(hook.point);
    }
    for (const auto &[a, b] : patchEdges) {
      auto first = neighbors.find(a), second = neighbors.find(b);
      if (first != neighbors.end()) first->second.push_back(b);
      if (second != neighbors.end()) second->second.push_back(a);
    }
    for (auto &[host, fractions] : hosts) {
      const auto segment = splineSegment(m, m.splines[host.first], host.second);
      const auto cubic   = segment.bezier();
      fractions.emplace(m.points[segment.from].head, 0);
      fractions.emplace(m.points[segment.to].head, 1);
      for (const auto &[a, from] : fractions) {
        auto edges = neighbors.find(a);
        if (edges == neighbors.end()) continue;
        for (const auto b : edges->second) {
          const auto to = fractions.find(b);
          if (to == fractions.end() || to->second == from) continue;
          auto interval = splineInterval(cubic, from, to->second);
          curves.emplace(Edge(a, b), interval);
          std::reverse(interval.begin(), interval.end());
          curves.emplace(Edge(b, a), interval);
        }
      }
    }
  }
  const auto boundary = [&](std::size_t a, std::size_t b) {
    const auto it = curves.find({a, b});
    if (it != curves.end()) return it->second;
    const auto &p = m.points[a].position, &q = m.points[b].position;
    return std::array<Vec3, 4>{{p, mix(p, q, 1.0 / 3), mix(p, q, 2.0 / 3), q}};
  };
  std::map<Edge, std::vector<std::size_t>> boundaryOwners;
  for (std::size_t p = 0; p < m.patches.size(); ++p) {
    const auto &patch = m.patches[p];
    std::vector<std::size_t> heads;
    for (auto id : patch.points) {
      auto h = m.points[ids.at(id)].head;
      if (heads.empty() || heads.back() != h) heads.push_back(h);
    }
    if (heads.size() > 1 && heads.front() == heads.back()) heads.pop_back();
    if (heads.size() != 3 && heads.size() != 4) {
      warn(out.warnings,
           "Five-point/hook/special patches are not tessellated by this "
           "preview.");
      continue;
    }
    if (std::set<std::size_t>(heads.begin(), heads.end()).size() !=
        heads.size())
      fail("degenerate patch control topology");
    for (std::size_t i = 0; i < heads.size(); ++i) {
      const auto next = heads[(i + 1) % heads.size()];
      boundaryOwners[std::minmax(heads[i], next)].push_back(p);
    }
    if (patch.flags != 24)
      warn(out.warnings,
           "Patch orientation/special flags are retained; preview uses "
           "recorded corner order.");
    for (std::size_t g = 0; g < m.groups.size(); ++g)
      if (std::all_of(heads.begin(), heads.end(), [&](std::size_t h) {
            return memberships[g].count(h) != 0;
          }))
        out.patchGroups[p].push_back(m.groups[g].name);
    Color color{{0.65f, 0.65f, 0.65f}};
    int colorGroup = -1;
    for (std::size_t g = 0; g < m.groups.size(); ++g)
      if (m.groups[g].hasColor &&
          std::all_of(heads.begin(), heads.end(), [&](std::size_t h) {
            return memberships[g].count(h) != 0;
          })) {
        color      = m.groups[g].color;
        colorGroup = int(g);
      }
    if (heads.size() == 3) {
      const auto ab       = boundary(heads[0], heads[1]);
      const auto bc       = boundary(heads[1], heads[2]);
      const auto ca       = boundary(heads[2], heads[0]);
      const auto straight = [](const std::array<Vec3, 4> &edge) {
        const auto chord  = sub(edge[3], edge[0]);
        const double size = length(chord);
        const auto axis   = unit(chord);
        for (int i = 1; i < 3; ++i) {
          const auto offset      = sub(edge[i], edge[0]);
          const auto along       = dot(offset, axis);
          const double tolerance = 1e-12 * std::max(size, 1.0);
          if (along < -tolerance || along > size + tolerance ||
              length(cross(offset, axis)) > tolerance)
            return false;
        }
        return true;
      };
      const bool flat         = straight(ab) && straight(bc) && straight(ca);
      const std::size_t count = flat ? 1 : subdivisions * subdivisions;
      if (count > limits.triangles - out.triangles.size())
        fail("preview triangle budget exceeded");
      if (flat) {
        out.triangles.push_back(
            {{{ab[0], bc[0], ca[0]}}, color, p, colorGroup});
        continue;
      }

      // A cubic triangular patch has six boundary handles and one interior
      // handle. Use their mean for the interior so all three edges follow
      // exactly the same splines as adjoining quads.
      const auto middle = scale(
          add(add(add(ab[1], ab[2]), add(bc[1], bc[2])), add(ca[1], ca[2])),
          1.0 / 6);
      const auto sample = [&](double u, double v) {
        const double w              = 1 - u - v;
        const auto edgeContribution = [](const std::array<Vec3, 4> &edge,
                                         double a, double b) {
          return add(scale(edge[0], a * a * a),
                     add(scale(edge[1], 3 * a * a * b),
                         scale(edge[2], 3 * a * b * b)));
        };
        return add(
            add(edgeContribution(ab, w, u), edgeContribution(bc, u, v)),
            add(edgeContribution(ca, v, w), scale(middle, 6 * w * u * v)));
      };
      const double step = 1.0 / subdivisions;
      const auto normal = [&](double u, double v) {
        constexpr double h = 1e-5;
        return unit(cross(sub(sample(u + h, v), sample(u - h, v)),
                          sub(sample(u, v + h), sample(u, v - h))));
      };
      for (unsigned y = 0; y < subdivisions; ++y)
        for (unsigned x = 0; x + y < subdivisions; ++x) {
          const double u = x * step, v = y * step;
          const double u1 = (x + 1) * step, v1 = (y + 1) * step;
          const auto a = sample(u, v), b = sample(u1, v), c = sample(u, v1);
          const auto na = normal(u, v), nb = normal(u1, v), nc = normal(u, v1);
          out.triangles.push_back(
              {{{a, b, c}}, color, p, colorGroup, {{na, nb, nc}}, true});
          if (x + y + 1 < subdivisions)
            out.triangles.push_back({{{b, sample(u1, v1), c}},
                                     color,
                                     p,
                                     colorGroup,
                                     {{nb, normal(u1, v1), nc}},
                                     true});
        }
      continue;
    }
    if (2 * subdivisions * subdivisions >
        limits.triangles - out.triangles.size())
      fail("preview triangle budget exceeded");
    const auto ab     = boundary(heads[0], heads[1]);
    const auto dc     = boundary(heads[3], heads[2]);
    const auto ad     = boundary(heads[0], heads[3]);
    const auto bc     = boundary(heads[1], heads[2]);
    const auto sample = [&](double u, double v) {
      Vec3 ruled    = add(mix(sampleSpline(ab, u), sampleSpline(dc, u), v),
                          mix(sampleSpline(ad, v), sampleSpline(bc, v), u));
      Vec3 bilinear = mix(
          mix(m.points[heads[0]].position, m.points[heads[1]].position, u),
          mix(m.points[heads[3]].position, m.points[heads[2]].position, u), v);
      return sub(ruled, bilinear);
    };
    const auto normal = [&](double u, double v) {
      constexpr double h = 1e-5;
      const auto du =
          sub(sample(std::min(1.0, u + h), v), sample(std::max(0.0, u - h), v));
      const auto dv =
          sub(sample(u, std::min(1.0, v + h)), sample(u, std::max(0.0, v - h)));
      Vec3 n{{du[1] * dv[2] - du[2] * dv[1], du[2] * dv[0] - du[0] * dv[2],
              du[0] * dv[1] - du[1] * dv[0]}};
      const double size = length(n);
      return size > 1e-20 ? scale(n, 1.0 / size) : Vec3{};
    };
    for (unsigned y = 0; y < subdivisions; ++y)
      for (unsigned x = 0; x < subdivisions; ++x) {
        const double u = double(x) / subdivisions, v = double(y) / subdivisions;
        const double u1 = double(x + 1) / subdivisions,
                     v1 = double(y + 1) / subdivisions;
        const Vec3 a = sample(u, v), b = sample(u1, v), c = sample(u1, v1),
                   e = sample(u, v1);
        const auto na = normal(u, v), nb = normal(u1, v), nc = normal(u1, v1),
                   ne = normal(u, v1);
        out.triangles.push_back(
            {{{a, b, c}}, color, p, colorGroup, {{na, nb, nc}}, true});
        out.triangles.push_back(
            {{{a, c, e}}, color, p, colorGroup, {{na, nc, ne}}, true});
      }
  }
  for (const auto &[edge, patches] : boundaryOwners)
    if (patches.size() == 2)
      out.adjoiningPatches.emplace_back(patches[0], patches[1]);
  std::sort(out.adjoiningPatches.begin(), out.adjoiningPatches.end());
  out.adjoiningPatches.erase(
      std::unique(out.adjoiningPatches.begin(), out.adjoiningPatches.end()),
      out.adjoiningPatches.end());
  return out;
}

Matrix identity() { return {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}; }
Matrix multiply(const Matrix &a, const Matrix &b) {
  Matrix m{};
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c)
      for (int k = 0; k < 4; ++k) m[4 * r + c] += a[4 * r + k] * b[4 * k + c];
  return m;
}
Matrix inverse(const Matrix &m) {
  for (double v : m)
    if (!std::isfinite(v)) fail("non-finite camera pose");
  if (m[12] != 0 || m[13] != 0 || m[14] != 0 || m[15] != 1)
    fail("camera pose must be affine");
  const double a = m[0], b = m[1], c = m[2];
  const double d = m[4], e = m[5], f = m[6];
  const double g = m[8], h = m[9], i = m[10];
  const double det =
      a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  const double norm = std::max({std::abs(a), std::abs(b), std::abs(c)}) *
                      std::max({std::abs(d), std::abs(e), std::abs(f)}) *
                      std::max({std::abs(g), std::abs(h), std::abs(i)});
  if (!std::isfinite(det) || !(norm > 0) || !std::isfinite(norm) ||
      std::abs(det) <= 1e-12 * norm)
    fail("singular camera pose");
  Matrix r = identity();
  r[0]     = (e * i - f * h) / det;
  r[1]     = (c * h - b * i) / det;
  r[2]     = (b * f - c * e) / det;
  r[4]     = (f * g - d * i) / det;
  r[5]     = (a * i - c * g) / det;
  r[6]     = (c * d - a * f) / det;
  r[8]     = (d * h - e * g) / det;
  r[9]     = (b * g - a * h) / det;
  r[10]    = (a * e - b * d) / det;
  for (int row = 0; row < 3; ++row)
    r[row * 4 + 3] =
        -(r[row * 4] * m[3] + r[row * 4 + 1] * m[7] + r[row * 4 + 2] * m[11]);
  for (double v : r)
    if (!std::isfinite(v)) fail("invalid inverse camera pose");
  return r;
}
Matrix translation(double x, double y, double z) {
  auto m = identity();
  m[3]   = x;
  m[7]   = y;
  m[11]  = z;
  return m;
}
Matrix scaling(double x, double y, double z) {
  auto m = identity();
  m[0]   = x;
  m[5]   = y;
  m[10]  = z;
  return m;
}
Matrix rotation(double x, double y, double z) {
  const double rad = std::acos(-1.0) / 180.0;
  x *= rad;
  y *= rad;
  z *= rad;
  auto rx = identity(), ry = identity(), rz = identity();
  rx[5] = rx[10] = std::cos(x);
  rx[6]          = -std::sin(x);
  rx[9]          = std::sin(x);
  ry[0] = ry[10] = std::cos(y);
  ry[2]          = std::sin(y);
  ry[8]          = -std::sin(y);
  rz[0] = rz[5] = std::cos(z);
  rz[1]         = -std::sin(z);
  rz[4]         = std::sin(z);
  return multiply(rz, multiply(ry, rx));
}
Vec3 transform(const Matrix &m, const Vec3 &p) {
  Vec3 q{};
  for (int i = 0; i < 3; ++i) {
    q[i] = m[4 * i + 3];
    for (int j = 0; j < 3; ++j) q[i] += m[4 * i + j] * p[j];
    if (!std::isfinite(q[i]) || std::abs(q[i]) > 1e15)
      fail("invalid/excessive transformed coordinate");
  }
  return q;
}
static Vec3 transformNormal(const Matrix &inverseMatrix, const Vec3 &normal) {
  Vec3 result{};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      result[i] += inverseMatrix[4 * j + i] * normal[j];
  return result;
}
Mesh transformed(const Mesh &mesh, const Matrix &m) {
  Mesh out = mesh;
  bool normals      = std::any_of(mesh.triangles.begin(), mesh.triangles.end(),
                                  [](const Triangle &t) { return t.hasNormals; });
  auto normalMatrix = identity();
  if (normals) {
    try {
      normalMatrix = inverse(m);
    } catch (const std::runtime_error &) {
      normals = false;
    }
  }
  for (auto &t : out.triangles) {
    for (auto &p : t.vertices) p = transform(m, p);
    t.hasNormals = t.hasNormals && normals;
    if (t.hasNormals)
      for (auto &n : t.normals) n = transformNormal(normalMatrix, n);
  }
  for (auto &spline : out.splines)
    for (auto &curve : spline.curves)
      for (auto &p : curve.points) p = transform(m, p);
  return out;
}
Mesh projected(const Mesh &mesh, double cx, double cy, double cz, double focus,
               double nearPlane) {
  if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(cz) ||
      !std::isfinite(focus) || !std::isfinite(nearPlane) || focus <= 0 ||
      nearPlane <= 0)
    fail("invalid camera");
  Mesh out;
  out.warnings        = mesh.warnings;
  out.patchGroups     = mesh.patchGroups;
  out.amMaterials     = mesh.amMaterials;
  const double zLimit = focus + cz - nearPlane;
  for (const auto &t : mesh.triangles) {
    std::vector<Vec3> polygon, normals;
    for (int i = 0; i < 3; ++i) {
      const Vec3 a = t.vertices[i], b = t.vertices[(i + 1) % 3];
      const bool inA = a[2] <= zLimit, inB = b[2] <= zLimit;
      if (inA) {
        polygon.push_back(a);
        normals.push_back(t.normals[i]);
      }
      if (inA != inB) {
        const double w = (zLimit - a[2]) / (b[2] - a[2]);
        polygon.push_back(mix(a, b, w));
        normals.push_back(mix(t.normals[i], t.normals[(i + 1) % 3], w));
      }
    }
    for (auto &p : polygon) {
      const double s = (focus + cz) / std::max(nearPlane, focus + cz - p[2]);
      p[0]           = cx + (p[0] - cx) * s;
      p[1]           = cy + (p[1] - cy) * s;
      p[2]           = focus / std::max(nearPlane, focus + cz - p[2]);
    }
    for (std::size_t i = 1; i + 1 < polygon.size(); ++i)
      out.triangles.push_back({{{polygon[0], polygon[i], polygon[i + 1]}},
                               t.color,
                               t.patch,
                               t.colorGroup,
                               {{normals[0], normals[i], normals[i + 1]}},
                               t.hasNormals});
  }
  return out;
}
Mesh projected(const Mesh &mesh, const Matrix &cameraToWorld,
               const Matrix &imageToStage, double focus, double nearPlane) {
  // Reuse the proven near-plane clipper with the optical origin moved to the
  // legacy focus coordinate; projection still happens only after view rotation.
  const auto view = multiply(translation(0, 0, focus), inverse(cameraToWorld));
  auto result = projected(transformed(mesh, view), 0, 0, 0, focus, nearPlane);
  const auto worldNormalMatrix = inverse(cameraToWorld);
  for (auto &t : result.triangles) {
    for (auto &p : t.vertices) p = transform(imageToStage, p);
    // Screen positions belong to the image plane; shading normals remain in
    // world space, so changing the camera cannot rotate authored world lights.
    if (t.hasNormals)
      for (auto &n : t.normals) n = transformNormal(worldNormalMatrix, n);
  }
  return result;
}
std::vector<std::array<float, 4>> rasterize(const Mesh &mesh, int width,
                                            int height) {
  if (width < 0 || height < 0 ||
      std::uint64_t(width) * std::uint64_t(height) > 16000000)
    fail("raster size exceeds limit");
  std::vector<std::array<float, 4>> pixels(std::size_t(width) * height,
                                           {{0, 0, 0, 0}});
  std::vector<double> depths(pixels.size(),
                             -std::numeric_limits<double>::infinity());
  const auto cross = [](const Vec3 &a, const Vec3 &b, double x, double y) {
    return (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]);
  };
  for (const auto &t : mesh.triangles) {
    const auto &a = t.vertices[0], &b = t.vertices[1], &c = t.vertices[2];
    for (const auto &p : t.vertices)
      for (double v : p)
        if (!std::isfinite(v) || std::abs(v) > 1e15)
          fail("invalid raster coordinate");
    const double area = cross(a, b, c[0], c[1]);
    if (std::abs(area) < 1e-12) continue;
    const double left = std::max(0.0, std::floor(std::min({a[0], b[0], c[0]})));
    const double right =
        std::min(double(width - 1), std::ceil(std::max({a[0], b[0], c[0]})));
    const double bottom =
        std::max(0.0, std::floor(std::min({a[1], b[1], c[1]})));
    const double top =
        std::min(double(height - 1), std::ceil(std::max({a[1], b[1], c[1]})));
    if (left > right || bottom > top) continue;
    for (int y = int(bottom); y <= int(top); ++y)
      for (int x = int(left); x <= int(right); ++x) {
        const double w0 = cross(b, c, x + 0.5, y + 0.5) / area;
        const double w1 = cross(c, a, x + 0.5, y + 0.5) / area;
        const double w2 = 1 - w0 - w1;
        if (w0 < -1e-10 || w1 < -1e-10 || w2 < -1e-10) continue;
        const auto i   = std::size_t(y) * width + x;
        const double z = a[2] * w0 + b[2] * w1 + c[2] * w2;
        if (z < depths[i]) continue;
        depths[i] = z;
        pixels[i] = {{t.color[0], t.color[1], t.color[2], 1}};
      }
  }
  return pixels;
}
}  // namespace otam

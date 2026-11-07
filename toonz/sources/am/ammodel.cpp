#include "ammodel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
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
std::vector<std::string> lines(const Document &d, const SourceNode &n) {
  std::string text;
  std::size_t offset = n.contentBegin;
  for (auto child : n.children) {
    const auto &c = d.nodes[child];
    text.append(d.source, offset, c.begin - offset);
    text += '\n';
    offset = c.end;
  }
  text.append(d.source, offset, n.contentEnd - offset);
  std::istringstream in(text);
  std::vector<std::string> result;
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (!line.empty()) result.push_back(line);
  }
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

Document parse(std::string source, const Limits &limits) {
  Document d = parseSource(std::move(source), "MODELFILE", limits);
  if (d.version != "19.5")
    warn(d.warnings,
         "File version is not the 19.5 reference dialect; compatibility is "
         "experimental.");
  std::size_t records = 0;
  for (std::size_t ni = 0; ni < d.nodes.size(); ++ni) {
    if (d.nodes[ni].name != "MODEL") continue;
    Model m;
    for (const auto &line : lines(d, d.nodes[ni]))
      if (line.compare(0, 5, "Name=") == 0) m.name = line.substr(5);
    // Descendants are in source order. Scope every record to its MODEL.
    for (std::size_t j = ni + 1;
         j < d.nodes.size() && d.nodes[j].begin < d.nodes[ni].end; ++j) {
      const auto &n = d.nodes[j];
      if (n.name == "SPLINE") {
        Spline spline;
        for (const auto &line : lines(d, n)) {
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
            cp.attachedTo = number<Id>(w[3]);
            if (cp.attachedTo < 0) fail("invalid attachment identifier");
            biasOffset = 4;
          } else
            fail(
                "unsupported CP record kind (hook/special record); source left "
                "unchanged");
          // A pair of dots is the compact all-default incoming/outgoing record.
          if (!(w.size() == biasOffset + 2 && w[biasOffset] == "." &&
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
      } else if ((n.name == "BONES" || n.name == "RELATIONSHIPS") &&
                 (!n.children.empty() || !lines(d, n).empty())) {
        warn(d.warnings,
             "Rig/relationship data is preserved in the source, not evaluated; "
             "showing model-space geometry.");
      }
    }
    if (m.points.empty()) fail("model has no supported spline geometry");
    resolve(m);
    if (m.patches.empty())
      warn(d.warnings,
           "No explicit patches: native patch discovery is not implemented.");
    d.models.push_back(std::move(m));
  }
  if (d.models.empty()) fail("no spline/patch MODEL was found");
  for (const auto &n : d.nodes)
    if ((n.name == "MATERIALS" || n.name == "ACTIONS" ||
         n.name == "CHOREOGRAPHIES" || n.name == "DECALS") &&
        (!n.children.empty() || !lines(d, n).empty()))
      warn(d.warnings,
           "Materials, decals, actions and choreography are retained as source "
           "records, not evaluated.");
  return d;
}

Mesh previewMesh(const Document &d, std::size_t modelIndex,
                 unsigned subdivisions, const Limits &limits) {
  if (modelIndex >= d.models.size() || subdivisions < 1 || subdivisions > 32)
    fail("invalid preview request");
  const auto &m  = d.models[modelIndex];
  const auto ids = index(m);
  Mesh out;
  out.warnings = d.warnings;
  out.patchGroups.resize(m.patches.size());
  warn(out.warnings,
       "Experimental A:M control-surface preview; Hash patch/bias evaluation "
       "fidelity is not validated.");
  warn(out.warnings,
       "Only explicit three/four-corner patches are displayed; automatic patch "
       "discovery is not implemented.");
  // Connected CP identity (not coincident coordinates) defines boundary edges.
  using Edge = std::pair<std::size_t, std::size_t>;
  struct Curve {
    Vec3 a, b, da, db;
  };
  std::map<Edge, Curve> curves;
  const auto position = [&](std::size_t i) {
    return m.points[m.points[i].head].position;
  };
  for (const auto &s : m.splines) {
    const std::size_t size = s.points.size();
    for (std::size_t i = 0; i + 1 < size || (s.closed && i < size); ++i) {
      std::size_t j = (i + 1) % size;
      const auto ia = s.points[i], ib = s.points[j];
      const Vec3 a = position(ia), b = position(ib);
      const double chord = length(sub(b, a));
      Vec3 ta =
          sub(b, (i > 0 || s.closed) ? position(s.points[(i + size - 1) % size])
                                     : a);
      Vec3 tb = sub(
          (j + 1 < size || s.closed) ? position(s.points[(j + 1) % size]) : b,
          a);
      ta             = scale(ta, length(ta) > 1e-12 ? chord / length(ta) : 0);
      tb             = scale(tb, length(tb) > 1e-12 ? chord / length(tb) : 0);
      const auto &ca = m.points[ia], &cb = m.points[ib];
      // Deliberately approximate: no inferred gamma/alpha rotations.
      if (ca.bias[3] || ca.bias[4] || cb.bias[0] || cb.bias[1])
        warn(out.warnings,
             "Nonzero alpha/gamma is retained but not applied to the "
             "experimental preview.");
      ta = scale(ta, std::max(-100.0, std::min(100.0, ca.bias[5])));
      tb = scale(tb, std::max(-100.0, std::min(100.0, cb.bias[2])));
      curves.emplace(Edge(ca.head, cb.head), Curve{a, b, ta, tb});
      curves.emplace(Edge(cb.head, ca.head),
                     Curve{b, a, scale(tb, -1), scale(ta, -1)});
    }
  }
  const auto edge = [&](std::size_t a, std::size_t b, double t) {
    const auto it = curves.find({a, b});
    if (it == curves.end())
      return mix(m.points[a].position, m.points[b].position, t);
    const auto &c   = it->second;
    const double tt = t * t, ttt = tt * t;
    return add(
        add(scale(c.a, 2 * ttt - 3 * tt + 1), scale(c.b, -2 * ttt + 3 * tt)),
        add(scale(c.da, ttt - 2 * tt + t), scale(c.db, ttt - tt)));
  };
  std::vector<std::set<std::size_t>> memberships;
  for (const auto &g : m.groups) {
    std::set<std::size_t> heads;
    for (auto id : g.points) {
      auto it = ids.find(id);
      if (it != ids.end()) heads.insert(m.points[it->second].head);
    }
    memberships.push_back(std::move(heads));
  }
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
    const std::size_t count =
        heads.size() == 3 ? 1 : 2 * subdivisions * subdivisions;
    if (count > limits.triangles - out.triangles.size())
      fail("preview triangle budget exceeded");
    if (heads.size() == 3) {
      out.triangles.push_back(
          {{{m.points[heads[0]].position, m.points[heads[1]].position,
             m.points[heads[2]].position}},
           color,
           p,
           colorGroup});
      continue;
    }
    const auto sample = [&](double u, double v) {
      Vec3 ruled =
          add(mix(edge(heads[0], heads[1], u), edge(heads[3], heads[2], u), v),
              mix(edge(heads[0], heads[3], v), edge(heads[1], heads[2], v), u));
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

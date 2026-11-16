#include "ammodel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace otam {
namespace {
Vec3 subtract(const Vec3 &a, const Vec3 &b) {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
Vec3 cross(const Vec3 &a, const Vec3 &b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}
double dot(const Vec3 &a, const Vec3 &b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vec3 unit(Vec3 direction) {
  const double length = std::sqrt(dot(direction, direction));
  if (!(length > 1e-30) || !std::isfinite(length)) return {};
  for (double &value : direction) value /= length;
  return direction;
}

using Corner             = std::size_t;
constexpr Corner Missing = std::numeric_limits<Corner>::max();
Corner nextCorner(Corner i) { return i / 3 * 3 + (i % 3 + 1) % 3; }

struct ArrayHash {
  template <class T, std::size_t N>
  std::size_t operator()(const std::array<T, N> &values) const {
    std::size_t hash = 0;
    for (T value : values)
      hash ^= std::hash<T>{}(value) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
    return hash;
  }
};

// A:M samples the same spline from each adjoining patch. Coons evaluation
// and reversed parameterization can round those positions differently.
class WeldedPositions {
  using Cell = std::array<std::int64_t, 3>;
  double m_epsilon;
  std::vector<Vec3> m_positions;
  std::unordered_map<Cell, std::vector<Corner>, ArrayHash> m_cells;

  Corner find(const Cell &cell, const Vec3 &v) const {
    const auto item = m_cells.find(cell);
    if (item == m_cells.end()) return Missing;
    for (Corner i : item->second) {
      const auto &p = m_positions[i];
      if (std::abs(v[0] - p[0]) <= m_epsilon &&
          std::abs(v[1] - p[1]) <= m_epsilon &&
          std::abs(v[2] - p[2]) <= m_epsilon)
        return i;
    }
    return Missing;
  }

public:
  explicit WeldedPositions(double epsilon) : m_epsilon(epsilon) {}

  Corner id(const Vec3 &v) {
    Cell home{};
    for (int d = 0; d < 3; ++d)
      home[d] = std::int64_t(std::floor(v[d] / m_epsilon));
    auto match = find(home, v);
    if (match != Missing) return match;
    for (int z = -1; z <= 1; ++z)
      for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
          if (!x && !y && !z) continue;
          match = find({home[0] + x, home[1] + y, home[2] + z}, v);
          if (match != Missing) return match;
        }
    const Corner result = m_positions.size();
    m_positions.push_back(v);
    m_cells[home].push_back(result);
    return result;
  }
};

struct IncidentEdges {
  Corner first = Missing, second = Missing;
  bool nonManifold = false;

  void add(Corner i) {
    if (first == Missing)
      first = i;
    else if (second == Missing)
      second = i;
    else
      nonManifold = true;
  }
};

class CornerFans {
  std::vector<Corner> m_parent;
  std::vector<unsigned char> m_rank;

public:
  explicit CornerFans(std::size_t count) : m_parent(count), m_rank(count) {
    std::iota(m_parent.begin(), m_parent.end(), Corner(0));
  }
  Corner root(Corner i) {
    while (i != m_parent[i]) {
      m_parent[i] = m_parent[m_parent[i]];
      i           = m_parent[i];
    }
    return i;
  }
  void join(Corner a, Corner b) {
    a = root(a);
    b = root(b);
    if (a == b) return;
    if (m_rank[a] < m_rank[b]) std::swap(a, b);
    m_parent[b] = a;
    if (m_rank[a] == m_rank[b]) ++m_rank[a];
  }
};

}  // namespace

bool smoothNormals(Mesh &mesh, const NormalSmoothing &options,
                   const int *canceled) {
  if (canceled && *canceled) return false;
  if (!std::isfinite(options.strength) || !std::isfinite(options.creaseDegrees))
    throw std::runtime_error("Invalid 3D surface smoothing settings");
  const double strength = std::clamp(options.strength, 0.0, 1.0);
  if (strength == 0 || mesh.triangles.empty()) return true;

  const auto count    = mesh.triangles.size();
  const double crease = std::clamp(options.creaseDegrees, 0.0, 180.0);
  const double cosine = std::cos(crease * std::acos(-1.0) / 180.0);
  double magnitude    = 1.0;
  for (const auto &triangle : mesh.triangles) {
    if (canceled && *canceled) return false;
    for (const auto &v : triangle.vertices)
      for (double c : v) {
        if (!std::isfinite(c))
          throw std::runtime_error("Invalid 3D vertex for surface smoothing");
        magnitude = std::max(magnitude, std::abs(c));
      }
  }

  std::vector<Corner> vertices(count * 3, Missing);
  std::vector<Vec3> faces(count);
  std::unordered_map<std::array<Corner, 2>, IncidentEdges, ArrayHash> edges;
  {
    WeldedPositions weld(64 * std::numeric_limits<double>::epsilon() *
                         magnitude);
    for (std::size_t i = 0; i < count; ++i) {
      if (canceled && *canceled) return false;
      const auto &p = mesh.triangles[i].vertices;
      faces[i]      = unit(cross(subtract(p[1], p[0]), subtract(p[2], p[0])));
      if (faces[i] == Vec3{}) continue;
      for (int j = 0; j < 3; ++j) vertices[3 * i + j] = weld.id(p[j]);
      for (Corner c = 3 * i; c < 3 * i + 3; ++c) {
        const auto [a, b] = std::minmax(vertices[c], vertices[nextCorner(c)]);
        if (a != b) edges[{a, b}].add(c);
      }
    }
  }
  CornerFans fans(vertices.size());
  for (const auto &[edge, incident] : edges) {
    if (canceled && *canceled) return false;
    const auto a = incident.first, b = incident.second;
    if (incident.nonManifold || b == Missing || a / 3 == b / 3) continue;
    // Opposite edge directions identify a consistently oriented surface.
    if (vertices[a] == vertices[b]) continue;
    if (dot(faces[a / 3], faces[b / 3]) + 1e-12 < cosine) continue;

    const auto pa = mesh.triangles[a / 3].patch;
    const auto pb = mesh.triangles[b / 3].patch;
    if (mesh.amMaterials && pa != pb &&
        !std::binary_search(mesh.adjoiningPatches.begin(),
                            mesh.adjoiningPatches.end(),
                            std::make_pair(std::min(pa, pb), std::max(pa, pb))))
      continue;
    fans.join(a, nextCorner(b));
    fans.join(b, nextCorner(a));
  }
  edges.clear();

  std::vector<Vec3> original(vertices.size()), sums(vertices.size());
  for (std::size_t i = 0; i < count; ++i) {
    if (canceled && *canceled) return false;
    if (faces[i] == Vec3{}) continue;
    const auto &triangle = mesh.triangles[i];
    for (int j = 0; j < 3; ++j) {
      const Corner c = 3 * i + j;
      const auto a =
          unit(subtract(triangle.vertices[(j + 1) % 3], triangle.vertices[j]));
      const auto b =
          unit(subtract(triangle.vertices[(j + 2) % 3], triangle.vertices[j]));
      const double angle = std::acos(std::clamp(dot(a, b), -1.0, 1.0));
      auto n = triangle.hasNormals ? unit(triangle.normals[j]) : faces[i];
      if (n == Vec3{}) n = faces[i];
      original[c] = n;
      auto &sum   = sums[fans.root(c)];
      for (int k = 0; k < 3; ++k) sum[k] += angle * n[k];
    }
  }
  for (std::size_t i = 0; i < count; ++i) {
    if (canceled && *canceled) return false;
    if (faces[i] == Vec3{}) continue;
    for (int j = 0; j < 3; ++j) {
      const Corner c    = 3 * i + j;
      const auto target = unit(sums[fans.root(c)]);
      auto blended      = original[c];
      if (target != Vec3{})
        for (int k = 0; k < 3; ++k)
          blended[k] += strength * (target[k] - blended[k]);
      const auto result = unit(blended);
      if (result != Vec3{}) original[c] = result;
    }
  }
  if (canceled && *canceled) return false;
  for (std::size_t i = 0; i < count; ++i) {
    if (faces[i] == Vec3{}) continue;
    auto &triangle = mesh.triangles[i];
    std::copy_n(original.begin() + 3 * i, 3, triangle.normals.begin());
    triangle.hasNormals = true;
  }
  return true;
}

}  // namespace otam

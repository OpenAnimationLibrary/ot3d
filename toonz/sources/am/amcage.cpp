#include "amcage.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace otam {
namespace {
constexpr std::size_t maxKeys = 32768;

bool validIndex(const CageIndex &index, const CageIndex &sections) {
  for (int axis = 0; axis < 3; ++axis)
    if (index[axis] < 0 || index[axis] > sections[axis]) return false;
  return true;
}
}  // namespace

bool DistortionCage::contains(const CageIndex &index) const {
  return validIndex(index, sections);
}

Vec3 DistortionCage::offset(const CageIndex &index, double frame) const {
  const auto point = points.find(index);
  if (point == points.end() || point->second.empty()) return {};
  const auto &keys = point->second;
  if (!std::isfinite(frame) || frame <= keys.begin()->first)
    return keys.begin()->second;
  if (frame >= keys.rbegin()->first) return keys.rbegin()->second;
  const auto after  = keys.upper_bound(int(std::floor(frame)));
  const auto before = std::prev(after);
  const double t    = (frame - before->first) / (after->first - before->first);
  Vec3 result;
  for (int axis = 0; axis < 3; ++axis)
    result[axis] =
        before->second[axis] + (after->second[axis] - before->second[axis]) * t;
  return result;
}

DistortionCage readCage(const std::string &source) {
  DistortionCage cage;
  if (source.empty()) return cage;
  if (source.size() > 4 * 1024 * 1024)
    throw std::runtime_error("Distortion Cage data is too large");
  std::istringstream input(source);
  input.imbue(std::locale::classic());
  int version;
  auto &s = cage.sections;
  if (!(input >> version >> s[0] >> s[1] >> s[2] >>
        std::quoted(cage.bonePath)) ||
      version != 1 || std::any_of(s.begin(), s.end(), [](int count) {
        return count < 1 || count > DistortionCage::maxSections;
      }))
    throw std::runtime_error("Invalid Distortion Cage settings");
  constexpr int max = DistortionCage::maxSections;
  std::size_t count = 0;
  while (input >> std::ws && !input.eof()) {
    CageIndex index;
    int frame;
    Vec3 value;
    if (++count > maxKeys ||
        !(input >> index[0] >> index[1] >> index[2] >> frame >> value[0] >>
          value[1] >> value[2]) ||
        !validIndex(index, {max, max, max}) || frame < -1 ||
        std::any_of(value.begin(), value.end(), [](double v) {
          return !std::isfinite(v) || std::abs(v) > DistortionCage::maxOffset;
        }))
      throw std::runtime_error("Invalid Distortion Cage point key");
    auto &keys = cage.points[index];
    if ((!keys.empty() && (frame == -1 || keys.begin()->first == -1)) ||
        !keys.emplace(frame, value).second)
      throw std::runtime_error("Conflicting Distortion Cage point keys");
  }
  return cage;
}

std::string writeCage(const DistortionCage &cage) {
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::setprecision(std::numeric_limits<double>::max_digits10);
  const auto &s = cage.sections;
  output << 1 << ' ' << s[0] << ' ' << s[1] << ' ' << s[2] << ' '
         << std::quoted(cage.bonePath);
  std::size_t count = 0;
  for (const auto &[index, keys] : cage.points)
    for (const auto &[frame, v] : keys) {
      if (++count > maxKeys)
        throw std::runtime_error("Distortion Cage has too many point keys");
      output << ' ' << index[0] << ' ' << index[1] << ' ' << index[2] << ' '
             << frame << ' ' << v[0] << ' ' << v[1] << ' ' << v[2];
    }
  return output.str();
}

bool applyCage(const DistortionCage &cage, const Model &rest, Model &pose,
               double frame, double strength) {
  if (cage.points.empty() || !std::isfinite(strength) || strength <= 0 ||
      pose.points.size() != rest.points.size())
    return false;
  const auto &s   = cage.sections;
  const int width = s[0] + 1, height = s[1] + 1;
  const auto gridIndex = [&](const CageIndex &i) {
    return i[0] + width * (i[1] + height * i[2]);
  };
  std::vector<Vec3> offsets(width * height * (s[2] + 1));
  bool active = false;
  for (const auto &[index, keys] : cage.points) {
    if (!cage.contains(index)) continue;
    const auto value          = cage.offset(index, frame);
    offsets[gridIndex(index)] = value;
    active |= value != Vec3{};
  }
  if (!active) return false;

  Matrix toLocal = identity(), toModel = identity();
  std::vector<bool> included;
  if (!cage.bonePath.empty()) {
    const auto target = std::find_if(
        rest.bones.begin(), rest.bones.end(),
        [&](const NativeBone &bone) { return bone.path == cage.bonePath; });
    if (target == rest.bones.end()) return false;
    const auto targetIndex = std::distance(rest.bones.begin(), target);
    toLocal                = target->inverseBind;
    toModel                = inverse(toLocal);
    toModel[3] = toModel[7] = toModel[11] = 0;
    included.resize(rest.bones.size());
    // Native bones are stored in parent-before-child order.
    for (std::size_t i = 0; i < rest.bones.size(); ++i) {
      const int parent = rest.bones[i].parent;
      included[i] =
          i == std::size_t(targetIndex) ||
          (parent >= 0 && std::size_t(parent) < i && included[parent]);
    }
  }
  const auto eligible = [&](std::size_t i) {
    if (rest.points[i].head != i) return false;
    if (included.empty()) return true;
    const int bone = i < rest.boneByHead.size() ? rest.boneByHead[i] : -1;
    return bone >= 0 && std::size_t(bone) < included.size() && included[bone];
  };

  const double inf = std::numeric_limits<double>::infinity();
  Vec3 lo{inf, inf, inf}, hi{-inf, -inf, -inf};
  for (std::size_t i = 0; i < rest.points.size(); ++i) {
    if (!eligible(i)) continue;
    const auto point = transform(toLocal, rest.points[i].position);
    for (int axis = 0; axis < 3; ++axis) {
      lo[axis] = std::min(lo[axis], point[axis]);
      hi[axis] = std::max(hi[axis], point[axis]);
    }
  }
  if (lo[0] == inf) return false;
  Vec3 extent{hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]};
  const double minimum = std::max({extent[0], extent[1], extent[2], 1.0}) / 10;
  for (int axis = 0; axis < 3; ++axis) {
    if (extent[axis] >= minimum) continue;
    lo[axis] -= (minimum - extent[axis]) / 2;
    extent[axis] = minimum;
  }

  bool moved = false;
  for (std::size_t i = 0; i < rest.points.size(); ++i) {
    if (!eligible(i)) continue;
    const auto point = transform(toLocal, rest.points[i].position);
    CageIndex cell;
    Vec3 fraction;
    for (int axis = 0; axis < 3; ++axis) {
      const double grid =
          std::clamp((point[axis] - lo[axis]) / extent[axis], 0.0, 1.0) *
          s[axis];
      cell[axis]     = std::min(int(grid), s[axis] - 1);
      fraction[axis] = grid - cell[axis];
    }
    Vec3 delta{};
    for (int corner = 0; corner < 8; ++corner) {
      CageIndex vertex = cell;
      double weight    = 1;
      for (int axis = 0; axis < 3; ++axis) {
        const bool upper = (corner & (1 << axis)) != 0;
        vertex[axis] += upper;
        weight *= upper ? fraction[axis] : (1 - fraction[axis]);
      }
      const auto &offset = offsets[gridIndex(vertex)];
      for (int axis = 0; axis < 3; ++axis) delta[axis] += weight * offset[axis];
    }
    if (delta == Vec3{}) continue;
    for (int axis = 0; axis < 3; ++axis) delta[axis] *= extent[axis] * strength;
    delta = transform(toModel, delta);
    for (int axis = 0; axis < 3; ++axis)
      pose.points[i].position[axis] += delta[axis];
    moved = true;
  }
  return moved;
}

}  // namespace otam

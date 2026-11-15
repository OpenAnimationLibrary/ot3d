#include "ambones.h"

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
constexpr double MaxTranslation = 2e9;

double norm(const Quaternion &q) {
  return std::hypot(std::hypot(q[0], q[1]), std::hypot(q[2], q[3]));
}

Quaternion normalized(Quaternion q) {
  const double length = norm(q);
  if (!(length > 1e-12) || !std::isfinite(length))
    throw std::runtime_error("A:M: invalid bone quaternion");
  for (double &n : q) n /= length;
  return q;
}

Quaternion interpolate(Quaternion a, Quaternion b, double t) {
  a          = normalized(a);
  b          = normalized(b);
  double dot = 0.0;
  for (int i = 0; i < 4; ++i) dot += a[i] * b[i];
  if (dot < 0) {
    for (double &v : b) v = -v;
    dot = -dot;
  }

  double from = 1 - t, to = t;
  if (dot < 0.9995) {
    const double angle = std::acos(dot);
    from               = std::sin((1 - t) * angle) / std::sin(angle);
    to                 = std::sin(t * angle) / std::sin(angle);
  }

  Quaternion q;
  for (int i = 0; i < 4; ++i) q[i] = from * a[i] + to * b[i];
  return normalized(q);
}

bool validKey(const BoneTransform &key) {
  if (!std::all_of(key.translation.begin(), key.translation.end(),
                   [](double v) {
                     return std::isfinite(v) && std::abs(v) <= MaxTranslation;
                   }))
    return false;
  return std::abs(norm(key.rotation) - 1) <= 1e-5;
}

}  // namespace

Matrix rotation(const Quaternion &quaternion) {
  const auto [x, y, z, w] = normalized(quaternion);
  return {{
      1 - 2 * (y * y + z * z),
      2 * (x * y - z * w),
      2 * (x * z + y * w),
      0,
      2 * (x * y + z * w),
      1 - 2 * (x * x + z * z),
      2 * (y * z - x * w),
      0,
      2 * (x * z - y * w),
      2 * (y * z + x * w),
      1 - 2 * (x * x + y * y),
      0,
      0,
      0,
      0,
      1,
  }};
}

BoneTransform sampleBone(const BoneAnimation &animation,
                         const std::string &path, double frame) {
  const auto found = animation.tracks.find(path);
  if (found == animation.tracks.end() || found->second.empty() ||
      !std::isfinite(frame))
    return {};
  const auto &keys = found->second;
  if (frame <= keys.begin()->first) return keys.begin()->second;
  if (frame >= keys.rbegin()->first) return keys.rbegin()->second;
  const auto next = keys.upper_bound(int(std::floor(frame)));
  const auto prev = std::prev(next);
  if (prev->first == frame) return prev->second;
  const double t = (frame - prev->first) / (next->first - prev->first);
  BoneTransform result;
  for (int i = 0; i < 3; ++i)
    result.translation[i] =
        (1 - t) * prev->second.translation[i] + t * next->second.translation[i];
  result.rotation =
      interpolate(prev->second.rotation, next->second.rotation, t);
  return result;
}

void rotateBone(BoneTransform &pose, int axis, double radians) {
  const auto [x, y, z, w] = pose.rotation;
  Vec3 v{};
  v.at(axis)           = std::sin(radians / 2);
  const auto [i, j, k] = v;
  const double s       = std::cos(radians / 2);
  pose.rotation        = normalized(
             {x * s + w * i + y * k - z * j, y * s + w * j + z * i - x * k,
              z * s + w * k + x * j - y * i, w * s - x * i - y * j - z * k});
}

void translateBone(BoneTransform &pose, int axis, double distance) {
  const auto basis = rotation(pose.rotation);
  for (int i = 0; i < 3; ++i)
    pose.translation[i] =
        std::clamp(pose.translation[i] + basis[4 * i + axis] * distance,
                   -MaxTranslation, MaxTranslation);
}

std::vector<PosedBone> poseBones(const Model &model,
                                 const BoneAnimation *animation, double frame) {
  std::vector<PosedBone> result;
  result.reserve(model.bones.size());
  for (const auto &bone : model.bones) {
    const auto key =
        animation ? sampleBone(*animation, bone.path, frame) : BoneTransform{};
    const bool moves = !(key == BoneTransform{});
    auto local       = bone.restLocal;
    if (moves)
      local = multiply(
          local, multiply(translation(key.translation[0], key.translation[1],
                                      key.translation[2]),
                          rotation(key.rotation)));
    const PosedBone *parent = bone.parent < 0 ? nullptr : &result[bone.parent];
    result.push_back({parent ? multiply(parent->frame, local) : local,
                      moves || (parent && parent->animated)});
  }
  return result;
}

bool applyBoneAnimation(Model &model, const BoneAnimation &animation,
                        double frame) {
  if (model.bones.empty() || animation.tracks.empty()) return false;
  const auto bones = poseBones(model, &animation, frame);
  std::vector<Matrix> skin;
  skin.reserve(bones.size());
  for (std::size_t i = 0; i < bones.size(); ++i)
    skin.push_back(bones[i].animated
                       ? multiply(bones[i].frame, model.bones[i].inverseBind)
                       : identity());

  bool moved = false;
  for (std::size_t i = 0; i < model.points.size(); ++i) {
    auto &point    = model.points[i];
    const int bone = model.boneByHead[i];
    if (point.head != i || bone < 0 || !bones[bone].animated) continue;
    point.position = transform(skin[bone], point.position);
    moved          = true;
  }
  return moved;
}

BoneAnimation readBoneAnimation(const std::string &source) {
  if (source.size() > Limits().bytes)
    throw std::runtime_error("A:M: bone animation exceeds size limit");
  std::istringstream in(source);
  in.imbue(std::locale::classic());

  int version;
  if (!(in >> version) || version != 1)
    throw std::runtime_error("A:M: unsupported bone animation version");

  BoneAnimation result;
  std::size_t entries = 0;
  while (in >> std::ws && in.peek() != std::char_traits<char>::eof()) {
    std::string path;
    int row;
    BoneTransform key;
    if (!(in >> std::quoted(path) >> row) || path.empty() || row < 0 ||
        ++entries > Limits().records)
      throw std::runtime_error("A:M: invalid bone animation track");
    for (double &v : key.translation) in >> v;
    for (double &v : key.rotation) in >> v;
    if (!in || !validKey(key))
      throw std::runtime_error("A:M: invalid bone animation key");
    if (!result.tracks[path].emplace(row, key).second)
      throw std::runtime_error("A:M: duplicate bone animation key");
  }
  if (result.tracks.empty())
    throw std::runtime_error("A:M: empty bone animation");
  return result;
}

std::string writeBoneAnimation(const BoneAnimation &animation) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << "1\n" << std::setprecision(std::numeric_limits<double>::max_digits10);
  for (const auto &[path, keys] : animation.tracks)
    for (const auto &[row, key] : keys) {
      out << std::quoted(path) << ' ' << row;
      for (double v : key.translation) out << ' ' << v;
      for (double v : key.rotation) out << ' ' << v;
      out << '\n';
    }
  return out.str();
}

}  // namespace otam

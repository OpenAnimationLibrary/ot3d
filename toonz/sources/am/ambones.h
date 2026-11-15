#pragma once

#include "ammodel.h"

#include <map>

namespace otam {

struct BoneTransform {
  Vec3 translation{};
  Quaternion rotation{0, 0, 0, 1};

  bool operator==(const BoneTransform &other) const {
    return translation == other.translation && rotation == other.rotation;
  }
};

// Keys use zero-based scene rows and are owned by a column instance.
struct BoneAnimation {
  std::map<std::string, std::map<int, BoneTransform>> tracks;
};

struct PosedBone {
  Matrix frame;
  bool animated;
};

BoneTransform sampleBone(const BoneAnimation &animation,
                         const std::string &path, double frame);
void rotateBone(BoneTransform &pose, int axis, double radians);
void translateBone(BoneTransform &pose, int axis, double distance);
std::vector<PosedBone> poseBones(const Model &model,
                                 const BoneAnimation *animation, double frame);
bool applyBoneAnimation(Model &model, const BoneAnimation &animation,
                        double frame);
BoneAnimation readBoneAnimation(const std::string &source);
std::string writeBoneAnimation(const BoneAnimation &animation);

}  // namespace otam

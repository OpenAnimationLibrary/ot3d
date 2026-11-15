#include "ambones.h"
#include "amcage.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace otam;

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void requirePosition(const Vec3 &point, const Vec3 &expected,
                     const char *message) {
  for (int axis = 0; axis < 3; ++axis)
    require(std::abs(point[axis] - expected[axis]) < 1e-8, message);
}

Model makeModel() {
  Model model;
  for (const Vec3 p : {Vec3{0, 0, 0}, Vec3{4, 6, 8}, Vec3{2, 3, 4},
                       Vec3{0, 3, 4}, Vec3{0, 0, 0}}) {
    ControlPoint cp;
    cp.id       = model.points.size() + 1;
    cp.head     = model.points.size();
    cp.position = p;
    model.points.push_back(cp);
  }
  model.points.back().head = 0;
  auto parent = NativeBone{"Root", "Root", -1, identity(), identity(), {}};
  auto child = NativeBone{"Child", "Root/Child", 0, identity(), identity(), {}};
  auto other = NativeBone{"Other", "Other", -1, identity(), identity(), {}};
  model.bones      = {parent, child, other};
  model.boneByHead = {0, 1, 1, 2, 0};
  return model;
}

void testLattice() {
  const auto rest = makeModel();
  auto pose       = rest;
  DistortionCage cage;
  cage.sections              = {1, 2, 3};
  cage.points[{1, 2, 3}][-1] = {0.25, 0.5, 0.125};
  cage.points[{0, 1, 1}][0]  = {0, 0, 0};
  cage.points[{0, 1, 1}][10] = {0, 0, 0.5};
  const auto saved           = writeCage(cage);
  const auto loaded          = readCage(saved);
  require(loaded.sections == cage.sections && loaded.points == cage.points &&
              writeCage(loaded) == saved,
          "Lattice settings did not round-trip");
  require(!applyCage(loaded, rest, pose, 5, 0), "Zero strength should bypass");
  pose.points[2].position[1] += 3;
  require(applyCage(loaded, rest, pose, 5, 1), "Cage did not move the model");
  requirePosition(pose.points[1].position, {5, 9, 9},
                  "XYZ corner displacement");
  requirePosition(pose.points[2].position, {2, 6, 4.5},
                  "Interior interpolation and preexisting Action");
  requirePosition(pose.points[3].position, {0, 3, 5}, "Whole model targeting");
  requirePosition(pose.points[4].position, {0, 0, 0},
                  "An attached occurrence should not move independently");
  requirePosition(rest.points[1].position, {4, 6, 8},
                  "Rest model was modified");
}

void testBoneTarget() {
  auto rest                 = makeModel();
  rest.bones[0].restLocal   = rotation(0, 0, 90);
  rest.bones[0].inverseBind = inverse(rest.bones[0].restLocal);
  rest.bones[1].inverseBind = rest.bones[0].inverseBind;
  auto pose                 = rest;
  DistortionCage cage;
  cage.sections = {1, 1, 1};
  cage.bonePath = "Root";
  for (int x = 0; x < 2; ++x)
    for (int y = 0; y < 2; ++y)
      for (int z = 0; z < 2; ++z) cage.points[{x, y, z}][-1] = {0.5, 0, 0};
  require(applyCage(cage, rest, pose, 0, 1), "Bone cage made no change");
  requirePosition(pose.points[0].position, {0, 3, 0}, "Bone-local axes");
  requirePosition(pose.points[1].position, {4, 9, 8}, "Child not affected");
  requirePosition(pose.points[3].position, {0, 3, 4}, "Sibling was affected");
  BoneAnimation animation;
  rotateBone(animation.tracks["Root"][0], 2, 3.141592653589793 / 2);
  require(applyBoneAnimation(pose, animation, 0), "Native pose did not apply");
  requirePosition(pose.points[0].position, {-3, 0, 0},
                  "Cage displacement did not follow bone rotation");

  cage.bonePath = "Missing";
  require(!applyCage(cage, rest, pose, 0, 1),
          "A missing bone should not fall back to the whole model");
  cage.bonePath = "Quote \" and \\ path";
  require(readCage(writeCage(cage)).bonePath == cage.bonePath,
          "Bone path escaping failed");
}

void testInvalid() {
  bool rejected = false;
  try {
    readCage("1 1 0 2 \"\"");
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected, "Invalid section counts were accepted");
  rejected = false;
  try {
    readCage("1 1 1 1 \"\" 0 0 0 0 0 0 0 0 0 0 0 1 0 0");
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected, "Duplicate point keys were accepted");
}
}  // namespace

int main() {
  try {
    testLattice();
    testBoneTarget();
    testInvalid();
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

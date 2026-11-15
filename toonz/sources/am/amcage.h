#pragma once

#include "ammodel.h"

#include <map>

namespace otam {

using CageIndex = std::array<int, 3>;

// Offsets are fractions of the cage's fitted extents in its local axes.
// A single key at -1 is a constant offset. Animation keys use scene rows.
struct DistortionCage {
  static constexpr int maxSections  = 10;
  static constexpr double maxOffset = 2;
  CageIndex sections{2, 2, 2};
  std::string bonePath;  // Empty selects the model bone and all geometry.
  std::map<CageIndex, std::map<int, Vec3>> points;

  Vec3 offset(const CageIndex &index, double frame) const;
  bool contains(const CageIndex &index) const;
};

DistortionCage readCage(const std::string &source);
std::string writeCage(const DistortionCage &cage);

// Fit to attachment heads in the target's rest space, then add offsets to
// the current CP pose before native bones and Plastic evaluate.
bool applyCage(const DistortionCage &cage, const Model &rest, Model &pose,
               double frame, double strength);

}  // namespace otam

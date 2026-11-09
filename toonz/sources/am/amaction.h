#pragma once

#include "ammodel.h"

#include <map>

namespace otam {

struct MuscleChannel {
  Id reference = 0;             // First CP occurrence in the target's spline.
  std::map<double, Vec3> keys;  // Action-local seconds to model-space offsets.
};

struct Action {
  std::string defaultModel;
  double duration = 0;                   // Seconds.
  std::map<Id, MuscleChannel> channels;  // Keyed by position-owning CP ID.
};

// A:M text ACTIONFILE containing one compact muscle action with position keys.
Action parseAction(std::string source);
std::string writeAction(const Action &action);
double canonicalActionTime(double seconds);
void validateAction(const Action &action, const Model &model);
void applyAction(const Action &action, Model &model, double seconds);
// Offsets for attached occurrences are resolved to their shared position owner.
// Conflicting offsets for the same owner reject the entire edit.
void setPositionKeys(Action &action, const Model &model,
                     const std::map<Id, Vec3> &offsets, double seconds);
void setPositionKey(Action &action, const Model &model, Id point,
                    double seconds, const Vec3 &offset);

}  // namespace otam

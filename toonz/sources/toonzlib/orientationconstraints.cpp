// Aim At is adapted from OT-Dev's add-constraints branch. Spatial orientation
// and Orient Like share the existing model placement path.
#include "toonz/tstageobject.h"
#include "toonz/tstageobjecttree.h"
#include "toonz/stage.h"
#include "amorientation.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <QUuid>

namespace {
bool active(const TStageObject::Constraint &c) {
  return c.enabled && std::isfinite(c.influence) && c.influence > 0;
}
}  // namespace

std::string TStageObject::getConstraintIdentity() {
  if (m_constraintIdentity.empty())
    m_constraintIdentity =
        QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
  return m_constraintIdentity;
}
TStageObject *TStageObject::resolveConstraintTarget(
    const Constraint &link) const {
  auto *target = m_tree->getStageObject(link.target, false);
  if (link.targetIdentity.empty()) return target;
  if (target && target->m_constraintIdentity == link.targetIdentity)
    return target;
  for (int i = 0; i < m_tree->getStageObjectCount(); ++i) {
    auto *candidate = m_tree->getStageObject(i);
    if (candidate->m_constraintIdentity == link.targetIdentity)
      return candidate;
  }
  return nullptr;
}
TStageObjectId TStageObject::getConstraintTargetId(
    const Constraint &link) const {
  const auto *target = resolveConstraintTarget(link);
  return target ? target->getId() : TStageObjectId::NoneId;
}
bool TStageObject::constraintTargetDependsOnThis(
    TStageObjectId id, const std::string &identity) const {
  std::set<const TStageObject *> visited;
  Constraint first;
  first.target         = id;
  first.targetIdentity = identity;
  std::vector<TStageObject *> pending{resolveConstraintTarget(first)};
  while (!pending.empty()) {
    auto *object = pending.back();
    pending.pop_back();
    if (!object) continue;
    if (object == this) return true;
    if (!visited.insert(object).second) continue;
    if (object->m_parent) pending.push_back(object->m_parent);
    for (const auto &link : object->m_constraints)
      if (link.scope == Constraint::Self)
        pending.push_back(object->resolveConstraintTarget(link));
    if (object->m_parent)
      for (const auto &link : object->m_parent->m_constraints)
        if (link.scope == Constraint::Children)
          pending.push_back(object->resolveConstraintTarget(link));
  }
  return false;
}

bool TStageObject::addConstraint(const Constraint &input) {
  auto link    = input;
  auto *target = m_tree ? resolveConstraintTarget(link) : nullptr;
  if (!target || !std::isfinite(link.influence) || link.influence < 0 ||
      link.influence > 1 ||
      (link.type != Constraint::AimAt && link.type != Constraint::OrientLike) ||
      (link.scope != Constraint::Self && link.scope != Constraint::Children) ||
      !otam::orientation(link.offset))
    return false;
  link.targetIdentity = target->getConstraintIdentity();
  if (link.scope == Constraint::Self) {
    if (constraintTargetDependsOnThis(link.target, link.targetIdentity))
      return false;
  } else {
    for (auto *child : m_children)
      if (child->constraintTargetDependsOnThis(link.target,
                                               link.targetIdentity))
        return false;
  }
  m_constraints.push_back(link);
  invalidate();
  return true;
}
void TStageObject::setConstraints(const std::vector<Constraint> &links) {
  m_constraints = links;
  invalidate();
}
bool TStageObject::removeConstraint(int index) {
  if (index < 0 || std::size_t(index) >= m_constraints.size()) return false;
  m_constraints.erase(m_constraints.begin() + index);
  invalidate();
  return true;
}
void TStageObject::clearConstraints() { setConstraints({}); }

bool TStageObject::hasOrientationConstraints() const {
  for (auto *object = this; object; object = object->m_parent)
    for (const auto &link : object->m_constraints)
      if (active(link) && (object != this || link.scope == Constraint::Self))
        return true;
  return false;
}
bool TStageObject::hasSpatialOrientation() const {
  if (!hasOrientationConstraints()) return false;
  std::set<const TStageObject *> visited;
  std::vector<const TStageObject *> pending{this};
  while (!pending.empty()) {
    const auto *object = pending.back();
    pending.pop_back();
    if (!object || !visited.insert(object).second) continue;
    if (object->is3DTransformEnabled()) return true;
    if (object->m_parent) pending.push_back(object->m_parent);
    for (const auto &link : object->m_constraints)
      if (active(link) && link.scope == Constraint::Self)
        pending.push_back(object->resolveConstraintTarget(link));
    if (object->m_parent)
      for (const auto &link : object->m_parent->m_constraints)
        if (active(link) && link.scope == Constraint::Children)
          pending.push_back(object->resolveConstraintTarget(link));
  }
  return false;
}

std::array<double, 16> TStageObject::applyOrientationConstraints(
    const std::array<double, 16> &world, double frame) {
  auto place = world;
  std::vector<const Constraint *> links;
  for (const auto &link : m_constraints)
    if (link.scope == Constraint::Self) links.push_back(&link);
  if (m_parent)
    for (const auto &link : m_parent->m_constraints)
      if (link.scope == Constraint::Children) links.push_back(&link);
  if (links.empty()) return world;
  const bool spatial = is3DTransformEnabled() || hasSpatialOrientation();
  const auto center  = getCenter(frame) * Stage::inch;
  const otam::Vec3 localPivot{
      {center.x, center.y,
       is3DTransformEnabled()
           ? m_amCenterZ->getValue(paramsTime(frame)) * Stage::inch
           : 0}};
  for (const auto *link : links) {
    if (!active(*link) ||
        constraintTargetDependsOnThis(link->target, link->targetIdentity))
      continue;
    auto *target = resolveConstraintTarget(*link);
    if (!target) continue;
    const auto targetWorld = target->getAMPlacement(frame);
    const auto pivot       = otam::transform(place, localPivot);
    std::optional<otam::Matrix> desired;
    if (link->type == Constraint::OrientLike) {
      desired = otam::orientation(targetWorld);
      // Planar objects retain the target's roll only.
      if (desired && !spatial)
        desired = otam::rotation(
            0, 0,
            std::atan2((*desired)[4], (*desired)[0]) * 180 / std::acos(-1.0));
    } else {
      const auto tc          = target->getCenter(frame) * Stage::inch;
      const auto destination = otam::transform(
          targetWorld,
          {{tc.x, tc.y,
            target->is3DTransformEnabled()
                ? target->m_amCenterZ->getValue(target->paramsTime(frame)) *
                      Stage::inch
                : 0}});
      desired = otam::aimOrientation(pivot, destination, spatial);
    }
    if (desired)
      place =
          otam::orientPlacement(place, otam::multiply(*desired, link->offset),
                                pivot, link->influence);
  }
  return place;
}

bool TStageObject::captureConstraintOffset(Constraint &link, double frame) {
  if (link.scope != Constraint::Self || link.type != Constraint::OrientLike ||
      constraintTargetDependsOnThis(link.target, link.targetIdentity))
    return false;
  auto *target = resolveConstraintTarget(link);
  if (!target) return false;
  const auto source = otam::orientation(getAMPlacement(frame));
  const auto goal   = otam::orientation(target->getAMPlacement(frame));
  if (!source || !goal) return false;
  link.offset = otam::multiply(otam::inverse(*goal), *source);
  return true;
}

void TStageObject::remapConstraintTargets(
    const std::map<TStageObjectId, TStageObjectId> &ids) {
  for (auto &link : m_constraints) {
    const auto found = ids.find(link.target);
    if (found != ids.end()) {
      link.target = found->second;
      if (link.target != TStageObjectId::NoneId) {
        auto *target = m_tree->getStageObject(link.target, false);
        if (target) link.targetIdentity = target->getConstraintIdentity();
      }
    }
  }
  invalidate();
}

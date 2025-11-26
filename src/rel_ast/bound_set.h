#ifndef BINDING_BOUND_SET_H
#define BINDING_BOUND_SET_H

#include <unordered_set>
#include <vector>

#include "rel_ast/bound.h"

namespace rel2sql {

// Represents a set of binding bounds, providing set operations and merging capabilities.
struct BoundSet {
  std::unordered_set<Bound> bounds;

  BoundSet() = default;

  // Creates a binding bound set from the given set of bindings.
  explicit BoundSet(std::unordered_set<Bound> bounds) : bounds(std::move(bounds)) {}

  // Returns the number of bindings in this set.
  size_t Size() const;

  // Returns true if this set is empty.
  bool Empty() const;

  // Performs a merge: bindings bounds with matching variables get merged by uniting their domains.
  BoundSet MergeWith(const BoundSet& other) const;

  // Returns the union of this set and another set.
  BoundSet UnionWith(const BoundSet& other);

  // Returns a new set with the specified variables removed from all bindings.
  // May merge compatible bindings that can form a complete table after variable removal.
  BoundSet WithRemovedVariables(const std::vector<std::string>& variables) const;

  // Returns the intersection of this set and another set, including merged compatible domains.
  BoundSet IntersectWith(const BoundSet& other) const;

  // Returns a copy of this set with variables renamed according to the provided map.
  BoundSet Renamed(const std::unordered_map<std::string, std::string>& rename_map) const;

 private:
  // Merges compatible Bound objects that can form a full table in-place.
  // Two or more Bound objects are mergeable if:
  // - Each has exactly one SourceProjection in its domain (restriction: only single source projections)
  // - The source projections share the same source, and this is a table source
  // - The disjoint union of projected_indices forms a complete set [0, 1, ..., arity-1] for the source
  // The variables are reconstructed based on the projection indices positions
  void MergeCompatibleSingleSourceProjections();
};

}  // namespace rel2sql

#endif  // BINDING_BOUND_SET_H

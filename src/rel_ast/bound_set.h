#ifndef BINDING_BOUND_SET_H
#define BINDING_BOUND_SET_H

#include <cstdint>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "rel_ast/bound.h"
#include "rel_ast/projection.h"

namespace rel2sql {

// Represents a set of binding bounds, providing set operations and merging capabilities.
struct BoundSet {
  std::unordered_set<Bound> bounds;

  // Variables that are bound in the set, used and maintained for easy checking
  std::set<std::string> bound_variables;

  BoundSet() = default;

  // Creates a binding bound set from the given set of bindings.
  explicit BoundSet(std::unordered_set<Bound> bounds) : bounds(std::move(bounds)) {
    for (const auto& bound : this->bounds) {
      bound_variables.insert(bound.variables.begin(), bound.variables.end());
    }
  }

  BoundSet(std::unordered_set<Bound> bounds, std::set<std::string> bound_variables)
      : bounds(std::move(bounds)), bound_variables(std::move(bound_variables)) {}

  // Returns the number of bindings in this set.
  size_t Size() const;

  // Returns true if this set is empty.
  bool IsEmpty() const;

  // Inserts a bound into the set.
  void Insert(const Bound& bound);

  // Performs a merge: bindings bounds with matching variables get merged by uniting their domains.
  BoundSet MergeWith(const BoundSet& other) const;

  // Returns the union of this set and another set.
  BoundSet UnionWith(const BoundSet& other) const;

  // Returns a new set with the specified variables removed from all bindings.
  // May merge compatible bindings that can form a complete table after variable removal.
  BoundSet WithRemovedVariables(const std::vector<std::string>& variables) const;

  // Returns a copy of this set with its variables renamed according to the provided map.
  BoundSet Renamed(const std::unordered_map<std::string, std::string>& rename_map) const;

  // Returns a minimal cover of this set, i.e. a set of bounds that is equivalent to this set
  BoundSet SmallCover() const;

  // Removes projections from all bounds that match the predicate, removes bounds with empty domains,
  // and merges compatible bounds that can form a complete table.
  // The predicate should return true for projections that should be removed.
  template <typename Predicate>
  BoundSet WithRemovedProjections(Predicate&& should_remove) const {
    std::unordered_set<Bound> cleaned_bounds;

    for (const auto& bound : bounds) {
      // Filter out projections that match the predicate
      std::unordered_set<Projection> cleaned_domain;
      for (const auto& projection : bound.domain) {
        if (!should_remove(projection)) {
          cleaned_domain.insert(projection);
        }
      }

      // Only keep bounds that have at least one projection remaining
      if (!cleaned_domain.empty()) {
        Bound cleaned(bound.variables, cleaned_domain);
        cleaned.coeffs = bound.coeffs;
        cleaned_bounds.insert(std::move(cleaned));
      }
    }

    // Create a BoundSet with cleaned bounds and merge compatible bounds
    BoundSet result(cleaned_bounds);
    // result.MergeCompatibleSingleSourceProjections();

    return result;
  }

 private:
  // Maximum number of bounds (P + S) for which we enumerate all 2^(k+r) solutions in SmallCover.
  static constexpr size_t kSmallCoverExactThreshold = 20;

  // SmallCover helpers: see SmallCover() docstring for the ILP formulation.
  static bool CoversAllVariables(
      const std::set<std::string>& bound_variables,
      const std::unordered_map<std::string, std::vector<size_t>>& P_indices_for_var,
      const std::unordered_map<std::string, std::vector<size_t>>& S_indices_for_var, size_t k, uint64_t mask);
  static uint64_t SmallCoverObjective(size_t k, size_t r, uint64_t mask);
  static uint64_t SolveSmallCoverExact(
      const std::set<std::string>& bound_variables,
      const std::unordered_map<std::string, std::vector<size_t>>& P_indices_for_var,
      const std::unordered_map<std::string, std::vector<size_t>>& S_indices_for_var, size_t k, size_t r);
  static std::pair<std::vector<size_t>, std::vector<size_t>> GreedySmallCover(
      const std::vector<Bound>& P_bounds, const std::vector<Bound>& S_bounds, size_t k,
      const std::set<std::string>& bound_variables);

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

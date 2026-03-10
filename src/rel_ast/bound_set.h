#ifndef BINDING_BOUND_SET_H
#define BINDING_BOUND_SET_H

#include <cstdint>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "rel_ast/bound.h"

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

  std::string ToString() const;

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
      if (!bound.domain) continue;

      std::unique_ptr<Domain> filtered =
          FilterProjections(bound.domain.get(), std::forward<Predicate>(should_remove));

      if (filtered) {
        Bound cleaned(bound.variables, std::move(filtered));
        cleaned.coeffs = bound.coeffs;
        cleaned_bounds.insert(std::move(cleaned));
      }
    }

    BoundSet result(cleaned_bounds);
    return result;
  }

 private:
  // Recursively filters out Projections that match the predicate. Returns nullptr if
  // the entire domain is removed. Non-Projection domains (ConstantDomain, DefinedDomain,
  // etc.) are kept. DomainUnion is filtered recursively.
  template <typename Predicate>
  static std::unique_ptr<Domain> FilterProjections(const Domain* d, Predicate&& should_remove) {
    if (auto* proj = dynamic_cast<const Projection*>(d)) {
      if (should_remove(*proj)) return nullptr;
      return proj->Clone();
    }
    if (auto* un = dynamic_cast<const DomainUnion*>(d)) {
      auto lhs = FilterProjections(un->lhs.get(), std::forward<Predicate>(should_remove));
      auto rhs = FilterProjections(un->rhs.get(), std::forward<Predicate>(should_remove));
      if (!lhs && !rhs) return nullptr;
      if (!lhs) return rhs;
      if (!rhs) return lhs;
      return std::make_unique<DomainUnion>(std::move(lhs), std::move(rhs));
    }
    // ConstantDomain, DefinedDomain, DomainOperation, IntensionalDomain: keep as-is
    return d->Clone();
  }

  // Maximum number of bounds (P + S) for which we enumerate all 2^(k+r) solutions in SmallCover.
  static constexpr size_t kSmallCoverExactThreshold = 20;

  // SmallCover helpers: see SmallCover() docstring for the ILP formulation.
  static bool CoversAllVariables(const std::set<std::string>& bound_variables,
                                 const std::unordered_map<std::string, std::vector<size_t>>& P_indices_for_var,
                                 const std::unordered_map<std::string, std::vector<size_t>>& S_indices_for_var,
                                 size_t k, uint64_t mask);
  static uint64_t SmallCoverObjective(size_t k, size_t r, uint64_t mask);
  static uint64_t SolveSmallCoverExact(const std::set<std::string>& bound_variables,
                                       const std::unordered_map<std::string, std::vector<size_t>>& P_indices_for_var,
                                       const std::unordered_map<std::string, std::vector<size_t>>& S_indices_for_var,
                                       size_t k, size_t r);
  static std::pair<std::vector<size_t>, std::vector<size_t>> GreedySmallCover(
      const std::vector<std::reference_wrapper<const Bound>>& P_bounds,
      const std::vector<std::reference_wrapper<const Bound>>& S_bounds, size_t k,
      const std::set<std::string>& bound_variables);
};

}  // namespace rel2sql

#endif  // BINDING_BOUND_SET_H

#ifndef BINDING_BOUND_H
#define BINDING_BOUND_H

#include <unordered_set>
#include <vector>

#include "rel_ast/projection.h"
#include "support/utils.h"

namespace rel2sql {

// Represents a binding of variables to a domain of source projections.
// The domain is a union of sources, and each projection must match the variable arity.
struct Bound {
  std::vector<std::string> variables;
  std::unordered_set<Projection> domain;  // The domain of the binding bound will be a union of sources

  Bound() = default;

  // Creates a binding bound with the given variables and an empty domain.
  explicit Bound(std::vector<std::string> variables) : variables(std::move(variables)) {}

  // Creates a binding bound with the given variables and domain.
  Bound(std::vector<std::string> variables, std::unordered_set<Projection> domain)
      : variables(std::move(variables)), domain(std::move(domain)) {}

  // Adds a source projection to the domain.
  void Add(const Projection& projection);

  // Adds a constant source projection to the domain.
  void Add(const ConstantSource& constant);

  // Checks if all projections in the domain have arity matching the number of variables.
  bool IsCorrect() const;

  // Returns true if there are no variables (empty binding).
  bool Empty() const { return variables.empty(); }

  // Returns a copy of this binding with the specified variable indices removed.
  Bound WithRemovedIndices(const std::vector<size_t>& indices) const;

  // Returns a new binding that unites this domain with another binding's domain.
  Bound MergeWith(const Bound& other) const;

  // Checks if this binding equals another (by variables and domain).
  bool operator==(const Bound& other) const;
};

}  // namespace rel2sql

namespace std {

template <>
struct hash<rel2sql::Bound> {
  std::size_t operator()(const rel2sql::Bound& tb) const {
    std::size_t seed = 0;
    utl::hash_range(seed, tb.variables.begin(), tb.variables.end());
    utl::hash_range(seed, tb.domain.begin(), tb.domain.end());
    return seed;
  }
};

}  // namespace std

#endif  // BINDING_BOUND_H

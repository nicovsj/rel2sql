#ifndef BINDING_BOUND_H
#define BINDING_BOUND_H

#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rel_ast/projection.h"

namespace rel2sql {

// Represents a binding of variables to a domain of source projections.
// The domain is a union of projections, and each projection must match the variable arity.
// Example: (x,y) in {π_0(R); π_1(S)} means that x is in π_0(R) and y is in π_1(S).
struct Bound {
  std::vector<std::string> variables;

  std::unique_ptr<Domain> domain;  // The source of the binding bound

  // Optional affine coefficients (a, b) per variable slot, representing a * x + b.
  // When std::nullopt, the slot is treated as identity (1 * x + 0).
  std::vector<std::optional<std::pair<double, double>>> coeffs;

  Bound() = default;

  Bound(const Bound& other)
      : variables(other.variables), domain(other.domain ? other.domain->Clone() : nullptr), coeffs(other.coeffs) {}

  Bound& operator=(const Bound& other) {
    if (this != &other) {
      variables = other.variables;
      domain = other.domain ? other.domain->Clone() : nullptr;
      coeffs = other.coeffs;
    }
    return *this;
  }

  // Creates a binding bound with the given variables and domain.
  Bound(std::vector<std::string> variables, std::unique_ptr<Domain> source)
      : variables(std::move(variables)), domain(std::move(source)), coeffs(variables.size()) {}

  // Returns true if there are no variables (empty bound).
  bool Empty() const { return variables.empty(); }

  // Returns true if any variable slot has non-identity affine coefficients (a,b) != (1,0).
  bool HasNonTrivialAffine() const;

  // Returns a copy of this bound with the specified variable indices removed.
  Bound WithRemovedIndices(const std::vector<size_t>& indices) const;

  // Returns a copy of this bound with the specified variable indices projected.
  Bound WithProjectedIndices(const std::vector<size_t>& indices) const;

  // Returns a new bound that merges this bound with another bound.
  Bound MergeWith(const Bound& other) const;

  // Returns a copy with variables renamed according to the provided map.
  Bound Renamed(const std::unordered_map<std::string, std::string>& rename_map) const;

  // Checks if this binding equals another (by variables and domain).
  bool operator==(const Bound& other) const;

  std::size_t Hash() const;

  std::string ToString() const;
};

}  // namespace rel2sql

namespace std {

template <>
struct hash<rel2sql::Bound> {
  std::size_t operator()(const rel2sql::Bound& tb) const { return tb.Hash(); }
};

}  // namespace std

#endif  // BINDING_BOUND_H

#ifndef BINDING_BOUND_H
#define BINDING_BOUND_H

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <optional>
#include <utility>

#include "rel_ast/projection.h"
#include "support/utils.h"

namespace rel2sql {

// Represents a binding of variables to a domain of source projections.
// The domain is a union of projections, and each projection must match the variable arity.
// Example: (x,y) in {π_0(R); π_1(S)} means that x is in π_0(R) and y is in π_1(S).
struct Bound {
  std::vector<std::string> variables;
  std::unordered_set<Projection> domain;  // The domain of the binding bound will be a union of sources
  // Optional affine coefficients (a, b) per variable slot, representing a * x + b.
  // When std::nullopt, the slot is treated as identity (1 * x + 0).
  std::vector<std::optional<std::pair<double, double>>> coeffs;

  Bound() = default;

  // Creates a binding bound with the given variables and an empty domain.
  explicit Bound(std::vector<std::string> variables)
      : variables(std::move(variables)), coeffs(variables.size()) {}

  // Creates a binding bound with the given variables and domain.
  Bound(std::vector<std::string> variables, std::unordered_set<Projection> domain)
      : variables(std::move(variables)), domain(std::move(domain)), coeffs(variables.size()) {}

  // Adds a source projection to the domain.
  void Add(const Projection& projection);

  // Adds a constant source projection to the domain.
  void Add(const ConstantSource& constant);

  // Checks if all projections in the domain have arity matching the number of variables.
  bool IsCorrect() const;

  // Returns true if there are no variables (empty binding).
  bool Empty() const { return variables.empty(); }

  // Returns true if any variable slot has non-identity affine coefficients (a,b) != (1,0).
  bool HasNonTrivialAffine() const;

  // Returns a copy of this binding with the specified variable indices removed.
  Bound WithRemovedIndices(const std::vector<size_t>& indices) const;

  // Returns a new binding that unites this domain with another binding's domain.
  Bound MergeWith(const Bound& other) const;

  // Returns a copy with variables renamed according to the provided map.
  Bound Renamed(const std::unordered_map<std::string, std::string>& rename_map) const;

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
    // Hash affine coefficients per variable slot.
    for (const auto& coeff_opt : tb.coeffs) {
      if (!coeff_opt.has_value()) {
        utl::hash_combine(seed, 0);
        continue;
      }
      utl::hash_combine(seed, 1);
      utl::hash_combine(seed, coeff_opt->first);
      utl::hash_combine(seed, coeff_opt->second);
    }
    return seed;
  }
};

}  // namespace std

#endif  // BINDING_BOUND_H

#include "rel_ast/bound.h"

#include <algorithm>

namespace rel2sql {

// BindingBound methods
void Bound::Add(const Projection& projection) { domain.insert(projection); }

void Bound::Add(const ConstantSource& constant) { domain.insert(Projection(constant)); }

bool Bound::IsCorrect() const {
  for (auto& projection : domain) {
    if (projection.Arity() != variables.size()) return false;
  }
  return true;
}

bool Bound::HasNonTrivialAffine() const {
  for (const auto& coeff_opt : coeffs) {
    if (!coeff_opt.has_value()) continue;
    auto [a, b] = *coeff_opt;
    if (a != 1.0 || b != 0.0) {
      return true;
    }
  }
  return false;
}

Bound Bound::WithRemovedIndices(const std::vector<size_t>& indices) const {
  std::vector<std::string> new_variables = variables;
  std::vector<std::optional<std::pair<double, double>>> new_coeffs = coeffs;
  // Sort indices in descending order to safely erase from end to beginning
  std::vector<size_t> sorted_indices = indices;
  std::sort(sorted_indices.rbegin(), sorted_indices.rend());
  for (auto index : sorted_indices) {
    if (index < new_variables.size()) {
      new_variables.erase(new_variables.begin() + index);
    }
    if (index < new_coeffs.size()) {
      new_coeffs.erase(new_coeffs.begin() + index);
    }
  }
  std::unordered_set<Projection> new_domain;
  for (auto& projection : domain) {
    new_domain.insert(projection.WithRemovedProjectionIndices(indices));
  }
  Bound result(new_variables, new_domain);
  result.coeffs = std::move(new_coeffs);
  return result;
}

Bound Bound::MergeWith(const Bound& other) const {
  std::unordered_set<Projection> merged = domain;
  merged.insert(domain.begin(), domain.end());
  merged.insert(other.domain.begin(), other.domain.end());
  Bound result{variables, std::move(merged)};
  // For now, assume coeffs are either identical or default; prefer this bound's coeffs.
  if (coeffs.size() == other.coeffs.size() && coeffs == other.coeffs) {
    result.coeffs = coeffs;
  } else if (!coeffs.empty() && other.coeffs.empty()) {
    result.coeffs = coeffs;
  } else if (coeffs.empty() && !other.coeffs.empty()) {
    result.coeffs = other.coeffs;
  } else {
    // In ambiguous cases, keep this bound's coefficients; disjunction guards will
    // prevent unsafe use of non-trivial affine metadata.
    result.coeffs = coeffs;
  }
  return result;
}

Bound Bound::Renamed(const std::unordered_map<std::string, std::string>& rename_map) const {
  std::vector<std::string> new_variables;
  new_variables.reserve(variables.size());
  for (const auto& variable : variables) {
    if (auto it = rename_map.find(variable); it != rename_map.end()) {
      new_variables.push_back(it->second);
    } else {
      new_variables.push_back(variable);
    }
  }
  Bound result(new_variables, domain);
  result.coeffs = coeffs;
  return result;
}

bool Bound::operator==(const Bound& other) const {
  return variables == other.variables && domain == other.domain && coeffs == other.coeffs;
}

}  // namespace rel2sql

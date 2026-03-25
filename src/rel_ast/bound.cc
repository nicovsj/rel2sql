#include "rel_ast/bound.h"

#include <sstream>
#include <unordered_set>

#include "support/utils.h"

namespace rel2sql {

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
  if (indices.empty()) return Bound(*this);
  std::unordered_set<size_t> to_remove(indices.begin(), indices.end());
  std::vector<size_t> complement_indices;
  std::vector<std::string> new_variables;
  std::vector<std::optional<std::pair<double, double>>> new_coeffs;
  for (size_t i = 0; i < variables.size(); i++) {
    if (to_remove.count(i) == 0) {
      complement_indices.push_back(i);
      new_variables.push_back(variables[i]);
      if (i < coeffs.size()) {
        new_coeffs.push_back(coeffs[i]);
      }
    }
  }
  auto new_source = std::make_unique<Projection>(complement_indices, domain->Clone());
  Bound result(std::move(new_variables), std::move(new_source));
  result.coeffs = std::move(new_coeffs);
  return result;
}

Bound Bound::WithProjectedIndices(const std::vector<size_t>& indices) const {
  std::vector<std::string> new_variables;
  new_variables.reserve(indices.size());
  for (size_t idx : indices) {
    if (idx < variables.size()) {
      new_variables.push_back(variables[idx]);
    }
  }
  auto new_source = std::make_unique<Projection>(indices, domain->Clone());
  Bound result(std::move(new_variables), std::move(new_source));
  result.coeffs = coeffs;
  return result;
}

Bound Bound::MergeWith(const Bound& other) const {
  auto merged = std::make_unique<DomainUnion>(domain->Clone(), other.domain->Clone());
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
  Bound result(new_variables, domain->Clone());
  result.coeffs = coeffs;
  return result;
}

bool Bound::operator==(const Bound& other) const {
  return variables == other.variables && *domain == *other.domain && coeffs == other.coeffs;
}

std::size_t Bound::Hash() const {
  std::size_t seed = 0;
  seed = utl::hash_range(seed, variables.begin(), variables.end());
  if (domain) {
    seed = utl::hash_combine(seed, domain->Hash());
  }
  for (const auto& coeff_opt : coeffs) {
    if (!coeff_opt.has_value()) {
      seed = utl::hash_combine(seed, 0);
      continue;
    }
    seed = utl::hash_combine(seed, 1);
    seed = utl::hash_combine(seed, coeff_opt->first);
    seed = utl::hash_combine(seed, coeff_opt->second);
  }
  return seed;
}

std::string Bound::ToString() const {
  std::ostringstream oss;
  oss << "(";
  for (size_t i = 0; i < variables.size(); ++i) {
    if (i > 0) oss << ", ";
    oss << variables[i];
  }
  oss << ") in " << (domain ? domain->ToString() : "?");
  return oss.str();
}

}  // namespace rel2sql

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

Bound Bound::WithRemovedIndices(const std::vector<size_t>& indices) const {
  std::vector<std::string> new_variables = variables;
  // Sort indices in descending order to safely erase from end to beginning
  std::vector<size_t> sorted_indices = indices;
  std::sort(sorted_indices.rbegin(), sorted_indices.rend());
  for (auto index : sorted_indices) {
    if (index < new_variables.size()) {
      new_variables.erase(new_variables.begin() + index);
    }
  }
  std::unordered_set<Projection> new_domain;
  for (auto& projection : domain) {
    new_domain.insert(projection.WithRemovedProjectionIndices(indices));
  }
  return Bound(new_variables, new_domain);
}

Bound Bound::MergeWith(const Bound& other) const {
  std::unordered_set<Projection> merged = domain;
  merged.insert(domain.begin(), domain.end());
  merged.insert(other.domain.begin(), other.domain.end());
  return Bound{variables, std::move(merged)};
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
  return Bound(new_variables, domain);
}

bool Bound::operator==(const Bound& other) const { return variables == other.variables && domain == other.domain; }

}  // namespace rel2sql

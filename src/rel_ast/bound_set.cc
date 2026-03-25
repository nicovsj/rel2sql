#include "rel_ast/bound_set.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rel2sql {

namespace {

// Finds the common variables between two variable vectors and returns:
// 1. The common variables in order (maximal shared tuple)
// 2. The indices in the first vector where common variables appear
// 3. The indices in the second vector where common variables appear
std::tuple<std::vector<std::string>, std::vector<size_t>, std::vector<size_t>> FindCommonVariables(
    const std::vector<std::string>& vars1, const std::vector<std::string>& vars2) {
  std::vector<std::string> common_vars;
  std::vector<size_t> indices1;
  std::vector<size_t> indices2;

  // Create a map from variable to index in vars2 for quick lookup
  std::unordered_map<std::string, size_t> var_to_index2;
  for (size_t i = 0; i < vars2.size(); i++) {
    var_to_index2[vars2[i]] = i;
  }

  // Find common variables in order of appearance in vars1
  for (size_t i = 0; i < vars1.size(); i++) {
    const auto& var = vars1[i];
    if (var_to_index2.find(var) != var_to_index2.end()) {
      common_vars.push_back(var);
      indices1.push_back(i);
      indices2.push_back(var_to_index2[var]);
    }
  }

  return std::make_tuple(common_vars, indices1, indices2);
}

}  // namespace

std::string BoundSet::ToString() const {
  std::ostringstream oss;
  oss << "{ ";
  bool first = true;
  for (const auto& bound : bounds) {
    if (!first) oss << " ; ";
    oss << bound.ToString();
    first = false;
  }
  oss << " }";
  return oss.str();
}

size_t BoundSet::Size() const { return bounds.size(); }

bool BoundSet::IsEmpty() const { return bounds.empty(); }

void BoundSet::Insert(const Bound& bound) {
  bounds.insert(bound);
  bound_variables.insert(bound.variables.begin(), bound.variables.end());
}

BoundSet BoundSet::MergeWith(const BoundSet& other) const {
  std::unordered_set<Bound> merged;
  std::set<std::string> merged_variables;
  for (const auto& bound : bounds) {
    for (const auto& other_bound : other.bounds) {
      // Find common variables (maximal shared tuple)
      auto [common_vars, indices1, indices2] = FindCommonVariables(bound.variables, other_bound.variables);

      // If there are no common variables, skip this pair
      if (common_vars.empty()) continue;

      // Project both bounds to the common variables
      auto projected_bound = bound.WithProjectedIndices(indices1);
      auto projected_other = other_bound.WithProjectedIndices(indices2);

      auto merged_bound = projected_bound.MergeWith(projected_other);
      merged.insert(merged_bound);

      // Store the common variables
      merged_variables.insert(common_vars.begin(), common_vars.end());
    }
  }
  return BoundSet(merged, merged_variables);
}

BoundSet BoundSet::UnionWith(const BoundSet& other) const {
  std::unordered_set<Bound> merged;
  std::set<std::string> merged_variables;

  merged.insert(bounds.begin(), bounds.end());
  merged.insert(other.bounds.begin(), other.bounds.end());

  merged_variables.insert(bound_variables.begin(), bound_variables.end());
  merged_variables.insert(other.bound_variables.begin(), other.bound_variables.end());

  return BoundSet(merged, merged_variables);
}

BoundSet BoundSet::WithRemovedVariables(const std::vector<std::string>& variables) const {
  std::unordered_set<Bound> result;

  for (auto& binding_bound : bounds) {
    std::vector<size_t> indices_to_remove;
    for (auto& variable : variables) {
      auto it = std::find(binding_bound.variables.begin(), binding_bound.variables.end(), variable);
      if (it == binding_bound.variables.end()) continue;

      int index = std::distance(binding_bound.variables.begin(), it);
      indices_to_remove.push_back(index);
    }

    auto new_bindings_bound = binding_bound.WithRemovedIndices(indices_to_remove);

    // If the new bindings bound is empty, we skip it
    if (new_bindings_bound.Empty()) continue;

    result.insert(new_bindings_bound);
  }

  std::set<std::string> result_variables(bound_variables.begin(), bound_variables.end());
  for (const auto& variable : variables) {
    result_variables.erase(variable);
  }

  return BoundSet(result, result_variables);
}

BoundSet BoundSet::Renamed(const std::unordered_map<std::string, std::string>& rename_map) const {
  std::unordered_set<Bound> renamed;
  renamed.reserve(bounds.size());
  for (const auto& bound : bounds) {
    renamed.insert(bound.Renamed(rename_map));
  }
  std::set<std::string> renamed_variables;

  for (const auto& variable : bound_variables) {
    if (auto it = rename_map.find(variable); it != rename_map.end()) {
      renamed_variables.insert(it->second);
    } else {
      renamed_variables.insert(variable);
    }
  }

  return BoundSet(renamed, renamed_variables);
}

bool BoundSet::CoversAllVariables(const std::set<std::string>& bound_variables,
                                  const std::unordered_map<std::string, std::vector<size_t>>& P_indices_for_var,
                                  const std::unordered_map<std::string, std::vector<size_t>>& S_indices_for_var,
                                  size_t k, uint64_t mask) {
  for (const auto& var : bound_variables) {
    int cover_count = 0;
    auto it_p = P_indices_for_var.find(var);
    if (it_p != P_indices_for_var.end()) {
      for (size_t j : it_p->second) {
        if (mask & (uint64_t{1} << j)) cover_count++;
      }
    }
    auto it_s = S_indices_for_var.find(var);
    if (it_s != S_indices_for_var.end()) {
      for (size_t j : it_s->second) {
        if (mask & (uint64_t{1} << (k + j))) cover_count++;
      }
    }
    if (cover_count < 1) return false;
  }
  return true;
}

uint64_t BoundSet::SmallCoverObjective(size_t k, size_t r, uint64_t mask) {
  uint64_t cost = 0;
  for (size_t j = 0; j < k; j++) {
    if (mask & (uint64_t{1} << j)) cost += 1;
  }
  for (size_t j = 0; j < r; j++) {
    if (mask & (uint64_t{1} << (k + j))) cost += (k + 1);
  }
  return cost;
}

uint64_t BoundSet::SolveSmallCoverExact(const std::set<std::string>& bound_variables,
                                        const std::unordered_map<std::string, std::vector<size_t>>& P_indices_for_var,
                                        const std::unordered_map<std::string, std::vector<size_t>>& S_indices_for_var,
                                        size_t k, size_t r) {
  const uint64_t num_assignments = (k + r <= 64) ? (uint64_t{1} << (k + r)) : 0;
  if (num_assignments == 0) return UINT64_MAX;

  uint64_t best_mask = UINT64_MAX;
  uint64_t best_cost = UINT64_MAX;

  for (uint64_t mask = 0; mask < num_assignments; mask++) {
    if (!CoversAllVariables(bound_variables, P_indices_for_var, S_indices_for_var, k, mask)) continue;
    uint64_t cost = SmallCoverObjective(k, r, mask);
    if (cost < best_cost) {
      best_cost = cost;
      best_mask = mask;
    }
  }
  return best_mask;
}

std::pair<std::vector<size_t>, std::vector<size_t>> BoundSet::GreedySmallCover(
    const std::vector<std::reference_wrapper<const Bound>>& P_bounds,
    const std::vector<std::reference_wrapper<const Bound>>& S_bounds, size_t k,
    const std::set<std::string>& bound_variables) {
  std::set<std::string> covered;
  std::vector<size_t> selected_P;
  std::vector<size_t> selected_S;
  std::vector<bool> used_P(k, false);
  std::vector<bool> used_S(S_bounds.size(), false);

  while (covered.size() < bound_variables.size()) {
    int best_new = 0;
    uint64_t best_cost = UINT64_MAX;
    bool best_is_P = true;
    size_t best_j = 0;

    for (size_t j = 0; j < k; j++) {
      if (used_P[j]) continue;
      int new_covered = 0;
      for (const std::string& var : P_bounds[j].get().variables) {
        if (bound_variables.count(var) && !covered.count(var)) new_covered++;
      }
      if (new_covered > 0) {
        uint64_t cost = 1;
        if (cost < best_cost || (cost == best_cost && new_covered > best_new)) {
          best_new = new_covered;
          best_cost = cost;
          best_is_P = true;
          best_j = j;
        }
      }
    }
    for (size_t j = 0; j < S_bounds.size(); j++) {
      if (used_S[j]) continue;
      int new_covered = 0;
      for (const std::string& var : S_bounds[j].get().variables) {
        if (bound_variables.count(var) && !covered.count(var)) new_covered++;
      }
      if (new_covered > 0) {
        uint64_t cost = k + 1;
        if (cost < best_cost || (cost == best_cost && new_covered > best_new)) {
          best_new = new_covered;
          best_cost = cost;
          best_is_P = false;
          best_j = j;
        }
      }
    }

    if (best_new <= 0) break;

    if (best_is_P) {
      used_P[best_j] = true;
      selected_P.push_back(best_j);
      for (const std::string& var : P_bounds[best_j].get().variables)
        if (bound_variables.count(var)) covered.insert(var);
    } else {
      used_S[best_j] = true;
      selected_S.push_back(best_j);
      for (const std::string& var : S_bounds[best_j].get().variables)
        if (bound_variables.count(var)) covered.insert(var);
    }
  }

  return {selected_P, selected_S};
}

// SmallCover returns a minimal set of bounds that still covers all bound_variables.
// For this we define two types of bounds:
//  - P bound: single-projection bound (pi(ID)-style)
//  - S bound: union of projections bound
// We model this as a 0-1 ILP:
//  Variables:
//    - z_j in {0,1}: select P_j if the j-th P bound is selected
//    - w_j in {0,1}: select S_j if the j-th S bound is selected
//  Constraints:
//    - for each variable x_i, the sum of z_j over P_j that bind x_i plus the sum of
//      w_j over S_j that bind x_i must be >= 1.
//  Objective:
//    minimize sum_j z_j + (k+1)*sum_j w_j,
//    so we prefer P's over S's.
// For small k+r we enumerate all 2^(k+r) assignments and pick a feasible solution with minimum cost;
// for larger instances we use a greedy set cover with the same costs (1 for P, k+1 for S).
BoundSet BoundSet::SmallCover() const {
  if (bounds.empty()) {
    return BoundSet(std::unordered_set<Bound>{}, bound_variables);
  }

  std::vector<std::reference_wrapper<const Bound>> P_bounds;
  std::vector<std::reference_wrapper<const Bound>> S_bounds;
  P_bounds.reserve(bounds.size());
  S_bounds.reserve(bounds.size());
  for (const auto& bound : bounds) {
    if (dynamic_cast<const Projection*>(bound.domain.get()) != nullptr)
      P_bounds.push_back(bound);
    else
      S_bounds.push_back(bound);
  }
  const size_t k = P_bounds.size();
  const size_t r = S_bounds.size();

  if (bound_variables.empty()) {
    std::unordered_set<Bound> all_bounds(bounds.begin(), bounds.end());
    return BoundSet(all_bounds, bound_variables);
  }

  std::unordered_map<std::string, std::vector<size_t>> P_indices_for_var;
  std::unordered_map<std::string, std::vector<size_t>> S_indices_for_var;
  for (const auto& var : bound_variables) {
    for (size_t j = 0; j < k; j++) {
      if (std::find(P_bounds[j].get().variables.begin(), P_bounds[j].get().variables.end(), var) !=
          P_bounds[j].get().variables.end()) {
        P_indices_for_var[var].push_back(j);
      }
    }
    for (size_t j = 0; j < r; j++) {
      if (std::find(S_bounds[j].get().variables.begin(), S_bounds[j].get().variables.end(), var) !=
          S_bounds[j].get().variables.end()) {
        S_indices_for_var[var].push_back(j);
      }
    }
  }

  bool feasible = true;
  for (const auto& var : bound_variables) {
    auto it_p = P_indices_for_var.find(var);
    auto it_s = S_indices_for_var.find(var);
    bool covered = (it_p != P_indices_for_var.end() && !it_p->second.empty()) ||
                   (it_s != S_indices_for_var.end() && !it_s->second.empty());
    if (!covered) {
      feasible = false;
      break;
    }
  }
  if (!feasible) {
    std::unordered_set<Bound> full_set(bounds.begin(), bounds.end());
    return BoundSet(full_set, bound_variables);
  }

  std::unordered_set<Bound> small_cover;
  const size_t total = k + r;

  if (total <= kSmallCoverExactThreshold && total <= 64) {
    uint64_t best_mask = SolveSmallCoverExact(bound_variables, P_indices_for_var, S_indices_for_var, k, r);
    if (best_mask != UINT64_MAX) {
      for (size_t j = 0; j < k; j++) {
        if (best_mask & (uint64_t{1} << j)) small_cover.insert(P_bounds[j]);
      }
      for (size_t j = 0; j < r; j++) {
        if (best_mask & (uint64_t{1} << (k + j))) small_cover.insert(S_bounds[j]);
      }
    } else {
      small_cover.insert(bounds.begin(), bounds.end());
    }
  } else {
    auto [selected_P, selected_S] = GreedySmallCover(P_bounds, S_bounds, k, bound_variables);
    for (size_t j : selected_P) small_cover.insert(P_bounds[j]);
    for (size_t j : selected_S) small_cover.insert(S_bounds[j]);
    if (small_cover.empty()) {
      small_cover.insert(bounds.begin(), bounds.end());
    }
  }

  return BoundSet(small_cover, bound_variables);
}

}  // namespace rel2sql

#include "rel_ast/bound_set.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rel2sql {

namespace {

// Checks if the union of projection indices from a set of bindings forms a complete table.
// Returns true if union of projected_indices = [0, 1, ..., table_arity-1] with no clashes.
std::vector<std::string> FormsCompleteTableByProjections(const std::vector<Bound>& bindings,
                                                         size_t table_arity) {
  std::unordered_set<size_t> all_indices;
  std::vector<std::string> variables(table_arity);

  for (const auto& binding : bindings) {
    if (binding.domain.size() != 1) return {};
    const Projection& projection = *binding.domain.begin();

    for (size_t i = 0; i < projection.projected_indices.size(); i++) {
      size_t index = projection.projected_indices[i];
      if (index >= table_arity) return {};
      if (all_indices.contains(index)) return {};  // Clash found

      all_indices.insert(index);
      variables[index] = binding.variables[i];
    }
  }

  // Check if we have exactly all indices [0..arity-1]
  if (all_indices.size() != table_arity) return {};

  return variables;
}

// Merges bindings that form a complete table into a single BindingBound.
// Precondition: FormsCompleteTableByProjections(bindings, table_arity) && HaveSameVariables(bindings)
Bound MergeBindingsToCompleteTable(const std::vector<Bound>& bindings,
                                           const std::vector<std::string>& variables) {
  size_t table_arity = variables.size();
  // Get the table source from the first binding
  const Projection& first_projection = *bindings[0].domain.begin();
  std::shared_ptr<TableSource> table_source = std::dynamic_pointer_cast<TableSource>(first_projection.source);

  // Create full projection [0, 1, ..., arity-1]
  std::vector<size_t> all_indices;
  all_indices.reserve(table_arity);
  for (size_t i = 0; i < table_arity; i++) {
    all_indices.push_back(i);
  }

  Projection full_projection(all_indices, table_source);
  std::unordered_set<Projection> merged_domain;
  merged_domain.insert(full_projection);

  return Bound(variables, merged_domain);
}

}  // namespace

// BindingBoundSet methods
size_t BoundSet::Size() const { return bounds.size(); }

bool BoundSet::Empty() const { return bounds.empty(); }

BoundSet BoundSet::MergeWith(const BoundSet& other) const {
  std::unordered_set<Bound> merged;
  for (const auto& bound : bounds) {
    for (const auto& other_bound : other.bounds) {
      if (bound.variables != other_bound.variables) continue;
      merged.insert(bound.MergeWith(other_bound));
    }
  }
  return BoundSet(merged);
}

BoundSet BoundSet::UnionWith(const BoundSet& other) {
  std::unordered_set<Bound> merged;
  merged.insert(bounds.begin(), bounds.end());
  merged.insert(other.bounds.begin(), other.bounds.end());
  return BoundSet(merged);
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

  auto return_set = BoundSet(result);

  // Merge compatible bindings that can form a full table, as the removal
  // of variables may have created new compatible bindings that can form a full table
  return_set.MergeCompatibleSingleSourceProjections();

  return return_set;
}

BoundSet BoundSet::IntersectWith(const BoundSet& other) const {
  std::unordered_set<Bound> result;

  // Start with normal intersection
  for (const auto& bound : bounds) {
    if (other.bounds.contains(bound)) {
      result.insert(bound);
    }
  }

  // Then union compatible domains
  for (const auto& bound : bounds) {
    for (const auto& other_bound : other.bounds) {
      if (bound.variables == other_bound.variables) {
        result.insert(bound.MergeWith(other_bound));
      }
    }
  }

  return BoundSet(result);
}

void BoundSet::MergeCompatibleSingleSourceProjections() {
  std::unordered_set<Bound> result;

  // Group by TableSource - focus on projection indices compatibility first
  std::unordered_map<TableSource, std::vector<Bound>> bounds_by_source;

  // Separate bindings with single projections from others
  for (const auto& bound : bounds) {
    if (bound.domain.size() != 1) {
      // Multi-projection bindings are not mergeable, add as-is
      result.insert(bound);
      continue;
    }

    const Projection& projection = *bound.domain.begin();
    auto table_source = std::dynamic_pointer_cast<TableSource>(projection.source);

    if (!table_source) {
      // Non-TableSource (e.g., ConstantSource) are not mergeable, add as-is
      result.insert(bound);
      continue;
    }

    bounds_by_source[*table_source].push_back(bound);
  }

  // Process each TableSource group to find mergeable bindings based on projection indices
  for (auto& [table_source, source_bindings] : bounds_by_source) {
    size_t table_arity = static_cast<size_t>(table_source.Arity());

    // If only one binding with this source, add it as-is
    if (source_bindings.size() == 1) {
      result.insert(source_bindings[0]);
      continue;
    }

    // Check if all bindings together form a complete table based on projection indices
    auto variables = FormsCompleteTableByProjections(source_bindings, table_arity);

    if (!variables.empty()) {
      result.insert(MergeBindingsToCompleteTable(source_bindings, variables));
    } else {
      // Projections don't form complete table - cannot merge
      for (const auto& binding : source_bindings) {
        result.insert(binding);
      }
    }
  }

  // Replace bounds with the merged result
  bounds = std::move(result);
}

}  // namespace rel2sql

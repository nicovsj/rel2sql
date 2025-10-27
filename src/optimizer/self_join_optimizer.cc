#include "self_join_optimizer.h"

#include <unordered_set>

#include "replacers.h"

namespace rel2sql {
namespace sql::ast {

void SelfJoinOptimizer::Visit(SelectStatement& select_statement) {
  // First navigate depth-first through possible subqueries in FROM statements
  if (select_statement.from.has_value()) {
    for (auto& source : select_statement.from.value()->sources) {
      Visit(*source);
    }
  }
  // Then try to eliminate redundant self-joins
  EliminateRedundantSelfJoins(select_statement);
}

bool SelfJoinOptimizer::EliminateRedundantSelfJoins(SelectStatement& select_statement) {
  if (!select_statement.from.has_value()) {
    return false;
  }

  auto& from_statement = *select_statement.from.value();
  bool simplified = false;

  auto grouped_sources = GroupSourcesByTableName(from_statement.sources);

  // Check for complete self-join in WHERE clause
  if (!from_statement.where.has_value()) return false;

  auto where_condition = from_statement.where.value();

  auto comparisons = CollectComparisonConditions(where_condition);

  // If no comparisons are found, there's no possible self-join.
  if (comparisons.empty()) return false;

  for (const auto& [_, candidate_sources] : grouped_sources) {
    // No possible self-join if there's only one source.
    if (candidate_sources.size() <= 1) continue;

    // Filter comparisons to only include those between candidate sources
    auto filtered_comparisons = FilterComparisonsForSources(comparisons, candidate_sources);
    if (filtered_comparisons.empty()) continue;

    auto matcher_map = BuildSelfJoinMatcherMap(filtered_comparisons);

    if (EliminateSelfJoins(matcher_map, from_statement)) {
      simplified = true;
    }
  }

  return simplified;
}

SelfJoinOptimizer::SourcesByTable SelfJoinOptimizer::GroupSourcesByTableName(
    const std::vector<std::shared_ptr<Source>>& sources) {
  SourcesByTable grouped_sources;

  for (const auto& source : sources) {
    if (auto table = std::dynamic_pointer_cast<Table>(source->sourceable)) {
      grouped_sources[table->name].push_back(source);
    }
  }

  return grouped_sources;
}

std::vector<std::shared_ptr<ComparisonCondition>> SelfJoinOptimizer::CollectComparisonConditions(
    const std::shared_ptr<Condition>& condition) {
  std::vector<std::shared_ptr<ComparisonCondition>> comparisons;

  if (auto comp_condition = std::dynamic_pointer_cast<ComparisonCondition>(condition)) {
    comparisons.push_back(comp_condition);
  } else if (auto logical_condition = std::dynamic_pointer_cast<LogicalCondition>(condition)) {
    if (logical_condition->op != LogicalOp::AND) {
      // Return empty vector for non-AND logical conditions (OR, NOT)
      // We can't safely eliminate self-joins with these operations
      return {};
    }
    for (const auto& sub_condition : logical_condition->conditions) {
      auto sub_comparisons = CollectComparisonConditions(sub_condition);
      // If any sub-condition returns empty (due to non-AND), stop processing
      if (sub_comparisons.empty()) {
        return {};
      }
      comparisons.insert(comparisons.end(), sub_comparisons.begin(), sub_comparisons.end());
    }
  }

  return comparisons;
}

std::vector<std::shared_ptr<ComparisonCondition>> SelfJoinOptimizer::FilterComparisonsForSources(
    const std::vector<std::shared_ptr<ComparisonCondition>>& comparisons,
    const std::vector<std::shared_ptr<Source>>& candidate_sources) {
  std::vector<std::shared_ptr<ComparisonCondition>> filtered_comparisons;

  // Create a set of candidate source aliases for fast lookup
  std::unordered_set<std::string> candidate_aliases;
  for (const auto& source : candidate_sources) {
    candidate_aliases.insert(source->Alias());
  }

  for (const auto& comp_condition : comparisons) {
    if (comp_condition->op != CompOp::EQ) continue;

    auto left_col = std::dynamic_pointer_cast<Column>(comp_condition->lhs);
    auto right_col = std::dynamic_pointer_cast<Column>(comp_condition->rhs);

    if (!left_col || !right_col) continue;
    if (!left_col->source.has_value() || !right_col->source.has_value()) continue;

    std::string left_alias = left_col->source.value()->Alias();
    std::string right_alias = right_col->source.value()->Alias();

    // Check if both columns belong to different candidate sources
    if (left_alias != right_alias && candidate_aliases.count(left_alias) > 0 &&
        candidate_aliases.count(right_alias) > 0) {
      filtered_comparisons.push_back(comp_condition);
    }
  }

  return filtered_comparisons;
}

SelfJoinOptimizer::SelfJoinMatcherMap SelfJoinOptimizer::BuildSelfJoinMatcherMap(
    const std::vector<std::shared_ptr<ComparisonCondition>>& comparisons) {
  // Group equality conditions by source pairs. The inner map is a map of column names to sources.
  // The outer map is a map of unique alias pairs (self-join candidates) to inner maps.
  // Multiple aliases can be at play here. If they were only two we wouldn't need the outer map.
  SelfJoinMatcherMap matcher_map;

  for (const auto& comp_condition : comparisons) {
    auto left_col = std::dynamic_pointer_cast<Column>(comp_condition->lhs);
    auto right_col = std::dynamic_pointer_cast<Column>(comp_condition->rhs);

    std::shared_ptr<Column> primary_col, modified_col;

    std::string left_alias = left_col->source.value()->Alias();
    std::string right_alias = right_col->source.value()->Alias();

    // Make sure the primary column is the one with the smaller alias to avoid duplicate matches.
    if (left_alias <= right_alias) {
      primary_col = left_col;
      modified_col = right_col;
    } else {
      primary_col = right_col;
      modified_col = left_col;
    }

    if (primary_col->name == modified_col->name) {  // Match!
      auto source_pair = std::make_pair(primary_col->source.value()->Alias(), modified_col->source.value()->Alias());

      auto it = matcher_map.find(source_pair);
      if (it == matcher_map.end()) {
        matcher_map[source_pair] = SelfJoin(primary_col->source.value(), modified_col->source.value());
        it = matcher_map.find(source_pair);
      }

      it->second.AddJoinCondition(comp_condition);
      it->second.AddColumn(primary_col);
    }
  }

  return matcher_map;
}

void SelfJoinOptimizer::RemoveConditionsFromWhere(
    const std::vector<std::shared_ptr<ComparisonCondition>>& conditions_to_remove,
    std::shared_ptr<Condition>& where_condition) {
  // FIX: This is flawed. This assumes that the WHERE clause is a flat LogicalCondition.
  if (where_condition) {
    if (auto logical_condition = std::dynamic_pointer_cast<LogicalCondition>(where_condition)) {
      // Create a set for fast lookup of conditions to remove
      std::unordered_set<std::shared_ptr<ComparisonCondition>> to_remove_set(conditions_to_remove.begin(),
                                                                             conditions_to_remove.end());

      // Remove conditions using iterator
      for (auto it = logical_condition->conditions.begin(); it != logical_condition->conditions.end();) {
        if (auto comp_condition = std::dynamic_pointer_cast<ComparisonCondition>(*it)) {
          if (to_remove_set.count(comp_condition) > 0) {
            it = logical_condition->conditions.erase(it);
          } else {
            ++it;
          }
        } else {
          ++it;
        }
      }
    }
  }
}

void SelfJoinOptimizer::RemoveRedundantSource(const SelfJoin& self_join_candidate, FromStatement& from_statement) {
  // Remove the redundant source from the FROM clause
  for (auto it = from_statement.sources.begin(); it != from_statement.sources.end();) {
    if (*it == self_join_candidate.redundant_source) {
      it = from_statement.sources.erase(it);
    } else {
      ++it;
    }
  }

  // FIX: This is flawed. This assumes that the WHERE clause is a flat LogicalCondition.
  // Remove join conditions from the WHERE clause
  if (from_statement.where.has_value()) {
    RemoveConditionsFromWhere(self_join_candidate.join_conditions, from_statement.where.value());
  }
}

bool SelfJoinOptimizer::EliminateSelfJoins(const SelfJoinMatcherMap& matcher_map, FromStatement& from_statement) {
  bool simplified = false;

  for (const auto& [source_pair, self_join_candidate] : matcher_map) {
    if (self_join_candidate.IsComplete()) {
      // Remove the redundant source and its associated conditions
      RemoveRedundantSource(self_join_candidate, from_statement);

      // Replace all references to the redundant source with the primary source
      SourceAndColumnReplacer replacer(self_join_candidate.redundant_source->Alias(),
                                       self_join_candidate.primary_source, self_join_candidate.column_map);
      base_expr_->Accept(replacer);

      simplified = true;
    }
  }

  return simplified;
}

}  // namespace sql::ast
}  // namespace rel2sql

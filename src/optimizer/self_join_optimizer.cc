#include "self_join_optimizer.h"

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

  auto comparisons = CollectEquivalenceConditions(where_condition);

  // If no comparisons are found, there's no possible self-join.
  if (comparisons.empty()) return false;

  // Compute transitive closure to find equivalence classes
  auto equivalence_classes = ComputeColumnEquivalenceClasses(comparisons);

  for (const auto& [_, candidate_sources] : grouped_sources) {
    // No possible self-join if there's only one source.
    if (candidate_sources.size() <= 1) continue;

    // Generate all possible pairs from candidate sources
    auto source_pairs = GenerateSourcePairs(candidate_sources);

    // Process each source pair to check for complete equivalence classes
    for (const auto& source_pair : source_pairs) {
      // Check if this source pair forms a self join in the query
      if (IsSelfJoin(source_pair, equivalence_classes)) {
        SourceReplacer replacer(source_pair.second->Alias(), source_pair.first);
        base_expr_->Accept(replacer);

        simplified = true;
      }
    }
  }

  RedundancyReplacer redundancy_replacer;
  base_expr_->Accept(redundancy_replacer);

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

SelfJoinOptimizer::SourcePairs SelfJoinOptimizer::GenerateSourcePairs(
    const std::vector<std::shared_ptr<Source>>& candidate_sources) {
  SourcePairs source_pairs;

  // Generate all possible pairs (i, j) where i < j to avoid duplicates
  for (size_t i = 0; i < candidate_sources.size(); ++i) {
    for (size_t j = i + 1; j < candidate_sources.size(); ++j) {
      source_pairs.emplace_back(candidate_sources[i], candidate_sources[j]);
    }
  }

  return source_pairs;
}

SelfJoinOptimizer::EquivalenceClassesMap SelfJoinOptimizer::ComputeColumnEquivalenceClasses(
    const std::vector<std::shared_ptr<ComparisonCondition>>& equivalences) {
  EquivalenceClassesMap result;
  size_t next_class_id = 0;

  // Process each equality condition
  for (const auto& equivalence : equivalences) {
    auto left_col = std::dynamic_pointer_cast<Column>(equivalence->lhs);
    auto right_col = std::dynamic_pointer_cast<Column>(equivalence->rhs);

    if (!left_col || !right_col) continue;
    if (!left_col->source.has_value() || !right_col->source.has_value()) continue;

    std::string left_alias = left_col->source.value()->Alias();
    std::string right_alias = right_col->source.value()->Alias();

    ColumnId left_id = {left_alias, left_col->name};
    ColumnId right_id = {right_alias, right_col->name};

    // Find existing classes for both columns
    auto left_class_it = result.column_to_class.find(left_id);
    auto right_class_it = result.column_to_class.find(right_id);

    if (left_class_it == result.column_to_class.end() && right_class_it == result.column_to_class.end()) {
      // Neither column is in any class, create a new one
      size_t new_class_id = next_class_id++;
      result.column_to_class[left_id] = new_class_id;
      result.column_to_class[right_id] = new_class_id;
      result.class_to_columns[new_class_id].insert(left_id);
      result.class_to_columns[new_class_id].insert(right_id);
    } else if (left_class_it == result.column_to_class.end()) {
      // Left column is new, add it to right column's class
      size_t class_id = right_class_it->second;
      result.column_to_class[left_id] = class_id;
      result.class_to_columns[class_id].insert(left_id);
    } else if (right_class_it == result.column_to_class.end()) {
      // Right column is new, add it to left column's class
      size_t class_id = left_class_it->second;
      result.column_to_class[right_id] = class_id;
      result.class_to_columns[class_id].insert(right_id);
    } else if (left_class_it->second != right_class_it->second) {
      // Both columns are in different classes, merge them
      size_t left_class_id = left_class_it->second;
      size_t right_class_id = right_class_it->second;

      // Merge right class into left class
      for (const auto& col_id : result.class_to_columns[right_class_id]) {
        result.column_to_class[col_id] = left_class_id;
        result.class_to_columns[left_class_id].insert(col_id);
      }

      // Remove the right class
      result.class_to_columns.erase(right_class_id);
    }
    // If both columns are already in the same class, do nothing
  }

  return result;
}

bool SelfJoinOptimizer::IsSelfJoin(const SourcePair& source_pair, const EquivalenceClassesMap& eq_class_map) {
  // Get table information to know the expected arity
  auto table1 = std::dynamic_pointer_cast<Table>(source_pair.first->sourceable);
  auto table2 = std::dynamic_pointer_cast<Table>(source_pair.second->sourceable);

  if (!table1 || !table2) return false;

  // Both tables should have the same arity for a valid self-join
  if (table1->arity != table2->arity) return false;

  std::string source1_alias = source_pair.first->Alias();
  std::string source2_alias = source_pair.second->Alias();

  // Iterate through both tables' attribute names simultaneously
  for (size_t i = 0; i < table1->attribute_names.size(); ++i) {
    const auto& attribute_name1 = table1->attribute_names[i];
    const auto& attribute_name2 = table2->attribute_names[i];

    // Check if the corresponding columns are in the same equivalence class
    auto eq_class_1 = eq_class_map.column_to_class.find({source1_alias, attribute_name1});
    auto eq_class_2 = eq_class_map.column_to_class.find({source2_alias, attribute_name2});

    // If either column is not found in any equivalence class, it's not complete
    if (eq_class_1 == eq_class_map.column_to_class.end() || eq_class_2 == eq_class_map.column_to_class.end()) {
      return false;
    }

    // If the columns are not in the same equivalence class, it's not complete
    if (eq_class_1->second != eq_class_2->second) {
      return false;
    }
  }

  return true;
}

bool SelfJoinOptimizer::IsEquivalenceCandidate(const std::shared_ptr<ComparisonCondition>& comp_condition) {
  if (!comp_condition) return false;
  if (comp_condition->op != CompOp::EQ) return false;

  auto left_col = std::dynamic_pointer_cast<Column>(comp_condition->lhs);
  auto right_col = std::dynamic_pointer_cast<Column>(comp_condition->rhs);

  if (!left_col || !right_col) return false;
  if (!left_col->source.has_value() || !right_col->source.has_value()) return false;

  return true;
}

std::vector<std::shared_ptr<ComparisonCondition>> SelfJoinOptimizer::CollectEquivalenceConditions(
    const std::shared_ptr<Condition>& where_condition) {
  // We can assume that the LogicalConditions are already flattened. This is guaranteed by the flattener optimizer.
  // So we need to check if the WHERE condition is a LogicalCondition with AND operator and
  // then collect all the equivalences in it.
  // If the WHERE condition is a single ComparisonCondition, we should check it as if it was
  // contained inside an AND LogicalCondition.

  std::vector<std::shared_ptr<ComparisonCondition>> equivalences;

  // First check if it's a single ComparisonCondition
  auto comp_condition = std::dynamic_pointer_cast<ComparisonCondition>(where_condition);
  if (comp_condition) {
    if (IsEquivalenceCandidate(comp_condition)) {
      equivalences.push_back(comp_condition);
    }
    return equivalences;
  }

  // Otherwise, check if it's a LogicalCondition with AND operator
  auto logical_condition = std::dynamic_pointer_cast<LogicalCondition>(where_condition);
  if (!logical_condition) return {};
  if (logical_condition->op != LogicalOp::AND) return {};

  for (const auto& sub_condition : logical_condition->conditions) {
    auto sub_comp_condition = std::dynamic_pointer_cast<ComparisonCondition>(sub_condition);
    if (IsEquivalenceCandidate(sub_comp_condition)) {
      equivalences.push_back(sub_comp_condition);
    }
  }
  return equivalences;
}

}  // namespace sql::ast
}  // namespace rel2sql

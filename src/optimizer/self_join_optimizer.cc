#include "self_join_optimizer.h"

#include "replacers.h"

namespace rel2sql {
namespace sql::ast {

void SelfJoinOptimizer::Visit(SelectStatement& select_statement) {
  if (select_statement.from.has_value()) {
    for (auto& source : select_statement.from.value()->sources) {
      Visit(*source);
    }
  }

  EliminateRedundantSelfJoins(select_statement);
}

bool SelfJoinOptimizer::EliminateRedundantSelfJoins(SelectStatement& select_statement) {
  if (!select_statement.from.has_value()) {
    return false;
  }

  std::stringstream base_expr_stream, select_stmt_stream;
  base_expr_->Print(base_expr_stream);
  select_statement.Print(select_stmt_stream);
  std::string base_expr = base_expr_stream.str();
  std::string select_stmt = select_stmt_stream.str();

  auto& from_statement = *select_statement.from.value();
  bool simplified = false;

  auto grouped_sources = GroupSourcesByTable(from_statement.sources);
  std::unordered_map<std::string, std::shared_ptr<Source>> unique_sources;
  std::unordered_map<std::string, std::shared_ptr<Source>> alias_map;

  for (const auto& [table_name, sources] : grouped_sources) {
    if (sources.size() <= 1) continue;

    // Get table arity (number of columns)
    auto table = std::dynamic_pointer_cast<Table>(sources[0]->sourceable);
    size_t table_arity = table->arity;

    // Check for complete self-join in WHERE clause
    if (!from_statement.where.has_value()) continue;

    auto where_condition = from_statement.where.value();
    auto logical_condition = std::dynamic_pointer_cast<LogicalCondition>(where_condition);
    if (!logical_condition) continue;

    std::vector<std::shared_ptr<ComparisonCondition>> comparisons;
    CollectComparisonConditions(where_condition, comparisons);

    // Group equality conditions by source pairs. The inner map is a map of column names to sources.
    // The outer map is a map of unique source pairs (self-join candidates) to inner maps.
    // Multiple sources can be at play here. If they were only two we wouldn't need the outer map.
    std::map<std::pair<std::string, std::string>, std::unordered_map<std::string, std::shared_ptr<Column>>> matcher_map;

    // TODO: This is O(n^2) in the number of conditions. We can do better by using a more efficient matching algorithm.
    for (const auto& comp_condition : comparisons) {
      std::stringstream cond_stream;
      comp_condition->Print(cond_stream);
      std::string cond_str = cond_stream.str();

      if (comp_condition->op != CompOp::EQ) continue;

      auto left_col = std::dynamic_pointer_cast<Column>(comp_condition->lhs);
      auto right_col = std::dynamic_pointer_cast<Column>(comp_condition->rhs);

      if (!left_col || !right_col) continue;

      if (!left_col->source.has_value() || !right_col->source.has_value()) continue;

      std::shared_ptr<Column> primary_col, modified_col;

      std::string left_source_alias = left_col->source.value()->Alias(),
                  right_source_alias = right_col->source.value()->Alias();

      if (left_source_alias == sources[0]->Alias() &&
          std::any_of(sources.begin() + 1, sources.end(),
                      [&](const auto& source) { return right_source_alias == source->Alias(); })) {
        primary_col = left_col;
        modified_col = right_col;
      } else if (right_source_alias == sources[0]->Alias() &&
                 std::any_of(sources.begin() + 1, sources.end(),
                             [&](const auto& source) { return left_source_alias == source->Alias(); })) {
        primary_col = right_col;
        modified_col = left_col;
      } else {
        continue;
      }

      if (primary_col->name == modified_col->name) {                         // Match!
        std::string primary_alias = primary_col->source.value()->Alias();    // Primary source
        std::string modified_alias = modified_col->source.value()->Alias();  // Source to be modified

        auto source_pair = std::make_pair(primary_alias, modified_alias);

        matcher_map[source_pair][modified_col->name] = modified_col;
      }
    }

    for (const auto& [source_pair, column_map] : matcher_map) {
      if (column_map.size() == table_arity) {  // A self-join is possible if all columns are matched
        SourceAndColumnReplacer replacer(source_pair.second, column_map);
        base_expr_->Accept(replacer);
        simplified = true;
      }
    }
  }

  return simplified;
}

std::unordered_map<std::string, std::vector<std::shared_ptr<Source>>> SelfJoinOptimizer::GroupSourcesByTable(
    const std::vector<std::shared_ptr<Source>>& sources) {
  std::unordered_map<std::string, std::vector<std::shared_ptr<Source>>> grouped_sources;

  for (const auto& source : sources) {
    if (auto table = std::dynamic_pointer_cast<Table>(source->sourceable)) {
      grouped_sources[table->name].push_back(source);
    }
  }

  return grouped_sources;
}

void SelfJoinOptimizer::CollectComparisonConditions(const std::shared_ptr<Condition>& condition,
                                                    std::vector<std::shared_ptr<ComparisonCondition>>& comparisons) {
  if (auto comp_condition = std::dynamic_pointer_cast<ComparisonCondition>(condition)) {
    comparisons.push_back(comp_condition);
  } else if (auto logical_condition = std::dynamic_pointer_cast<LogicalCondition>(condition)) {
    if (logical_condition->op != LogicalOp::AND) {
      throw std::runtime_error("Logical condition is not an AND");
    }
    for (const auto& sub_condition : logical_condition->conditions) {
      CollectComparisonConditions(sub_condition, comparisons);
    }
  }
}

}  // namespace sql::ast
}  // namespace rel2sql

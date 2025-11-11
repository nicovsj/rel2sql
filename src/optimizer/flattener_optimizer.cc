#include "flattener_optimizer.h"

#include "replacers.h"

namespace rel2sql {

namespace sql::ast {

void FlattenerOptimizer::Visit(SelectStatement& select_statement) {
  if (select_statement.from.has_value()) {
    for (auto& source : select_statement.from.value()->sources) {
      Visit(*source);
    }
  }

  TryFlattenSubquery(select_statement);
  return;
}

bool FlattenerOptimizer::CanFlattenSubquery(const std::shared_ptr<Source>& source) {
  if (source->is_cte) return false;
  auto select_subquery = std::dynamic_pointer_cast<SelectStatement>(source->sourceable);
  if (!select_subquery) return false;
  if (!select_subquery->from.has_value()) return false;
  if (select_subquery->group_by.has_value()) return false;
  return true;
}

std::unordered_map<std::string, std::shared_ptr<Column>> FlattenerOptimizer::BuildColumnMap(
    const std::shared_ptr<SelectStatement>& subquery) {
  std::unordered_map<std::string, std::shared_ptr<Column>> column_map;

  for (auto& column : subquery->columns) {
    auto term_selectable = std::dynamic_pointer_cast<TermSelectable>(column);
    if (!term_selectable) {
      continue;
    }
    auto term = std::dynamic_pointer_cast<Column>(term_selectable->term);
    if (!term) {
      continue;
    }
    column_map[term_selectable->Alias()] = term;
  }

  return column_map;
}

void FlattenerOptimizer::MergeWhereConditions(FromStatement& outer_from,
                                              const std::shared_ptr<Condition>& subquery_where) {
  if (!subquery_where) return;

  if (!outer_from.where.has_value()) {
    outer_from.where = subquery_where;
    return;
  }

  // Combine existing WHERE condition with subquery's WHERE condition using AND
  std::vector<std::shared_ptr<Condition>> conditions;
  conditions.push_back(outer_from.where.value());
  conditions.push_back(subquery_where);
  outer_from.where = std::make_shared<LogicalCondition>(conditions, LogicalOp::AND);
}

bool FlattenerOptimizer::TryFlattenSubquery(SelectStatement& select_statement) {
  if (!select_statement.from.has_value()) return false;

  auto& from_statement = *select_statement.from.value();
  std::vector<std::shared_ptr<Source>> new_sources;
  bool flattened = false;

  for (auto& source : from_statement.sources) {
    if (!CanFlattenSubquery(source)) {
      new_sources.push_back(source);
      continue;
    }

    auto select_subquery = std::dynamic_pointer_cast<SelectStatement>(source->sourceable);
    const std::string old_alias = source->Alias();
    auto column_map = BuildColumnMap(select_subquery);

    // Replace the subquery with its inner sources
    for (auto& inner_source : select_subquery->from.value()->sources) {
      new_sources.push_back(inner_source);
    }

    // Update references in the outer query
    SourceAndColumnReplacer replacer(old_alias, column_map);
    base_expr_->Accept(replacer);

    // Merge WHERE conditions if subquery has one
    if (select_subquery->from.value()->where.has_value()) {
      MergeWhereConditions(from_statement, select_subquery->from.value()->where.value());
    }

    flattened = true;
  }

  if (!flattened) return false;

  from_statement.sources = std::move(new_sources);

  LogicalConditionFlattener flattener;
  select_statement.Accept(flattener);

  return true;
}

}  // namespace sql::ast
}  // namespace rel2sql

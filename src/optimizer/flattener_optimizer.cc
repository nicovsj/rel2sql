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
}

bool FlattenerOptimizer::TryFlattenSubquery(SelectStatement& select_statement) {
  if (!select_statement.from.has_value()) {
    return false;
  }

  auto& from_statement = *select_statement.from.value();
  bool flattened = false;

  std::vector<std::shared_ptr<Source>> new_sources;

  for (auto& source : from_statement.sources) {
    auto select_subquery = std::dynamic_pointer_cast<SelectStatement>(source->sourceable);

    if (!select_subquery || !select_subquery->from.has_value() || select_subquery->group_by.has_value()) {
      new_sources.push_back(source);
      continue;
    }

    // Map aliases of columns to the new Column objects that need to be replaced
    std::unordered_map<std::string, std::shared_ptr<Column>> column_map;

    std::string old_alias = source->Alias();

    for (auto& inner_source : select_subquery->from.value()->sources) {
      for (auto& column : select_subquery->columns) {
        if (auto term_selectable = std::dynamic_pointer_cast<TermSelectable>(column)) {
          if (auto term = std::dynamic_pointer_cast<Column>(term_selectable->term)) {
            column_map[term_selectable->Alias()] = term;
          }
        }
      }
      // Replace the subquery with its inner source
      new_sources.push_back(inner_source);
    }

    // Update references in the outer query
    SourceAndColumnReplacer replacer(old_alias, column_map);

    base_expr_->Accept(replacer);

    if (select_subquery->from.value()->where.has_value()) {
      auto subquery_where = select_subquery->from.value()->where.value();

      if (from_statement.where.has_value()) {
        // If the outer query already has a WHERE condition, combine them with AND
        std::vector<std::shared_ptr<Condition>> conditions;
        conditions.push_back(from_statement.where.value());
        conditions.push_back(subquery_where);
        auto new_where = std::make_shared<LogicalCondition>(conditions, LogicalOp::AND);
        from_statement.where = new_where;
      } else {
        // If the outer query doesn't have a WHERE condition, use the subquery's
        from_statement.where = subquery_where;
      }
    }
    flattened = true;
  }

  from_statement.sources = new_sources;

  if (flattened) {
    LogicalConditionFlattener flattener;
    select_statement.Accept(flattener);
  }

  return flattened;
}

}  // namespace sql::ast
}  // namespace rel2sql

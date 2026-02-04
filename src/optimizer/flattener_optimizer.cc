#include "flattener_optimizer.h"

#include "replacers.h"
#include "validator/validator.h"

namespace rel2sql {

namespace sql::ast {

void FlattenerOptimizer::Visit(Select& select) {
  // Visit children first
  ExpressionVisitor::Visit(select);

  TryFlattenSubquery(select);
}

bool FlattenerOptimizer::CanFlattenSubquery(const std::shared_ptr<Source>& source) {
  if (source->is_cte) return false;
  auto select_subquery = std::dynamic_pointer_cast<Select>(source->sourceable);
  if (!select_subquery) return false;
  if (select_subquery->ctes_are_recursive) return false;
  if (!select_subquery->from.has_value()) return false;
  if (select_subquery->group_by.has_value()) return false;
  return true;
}

std::unordered_map<std::string, std::shared_ptr<Term>> FlattenerOptimizer::BuildTermMap(
    const std::shared_ptr<Select>& subquery) {
  std::unordered_map<std::string, std::shared_ptr<Term>> column_map;

  for (auto& column : subquery->columns) {
    auto term_selectable = std::dynamic_pointer_cast<TermSelectable>(column);
    if (!term_selectable) {
      continue;
    }
    auto column_cast = std::dynamic_pointer_cast<Column>(term_selectable->term);
    auto case_when_cast = std::dynamic_pointer_cast<CaseWhen>(term_selectable->term);
    // We can flatten simple projections (plain columns), CASE WHENs, and
    // general term expressions like arithmetic operations, as long as they
    // have an alias. For non-column terms we fall back to the alias name.
    if (!column_cast && !case_when_cast && !term_selectable->HasAlias()) {
      continue;
    }

    std::string aliased_column_name;
    if (column_cast) {
      aliased_column_name = term_selectable->HasAlias() ? term_selectable->Alias() : column_cast->name;
    } else if (case_when_cast) {
      aliased_column_name = term_selectable->Alias();
    } else {
      // General term (e.g., arithmetic expression); must have an alias to be referenced.
      aliased_column_name = term_selectable->Alias();
    }
    column_map[aliased_column_name] = term_selectable->term;
  }

  return column_map;
}

void FlattenerOptimizer::MergeWhereConditions(From& outer_from, const std::shared_ptr<Condition>& subquery_where) {
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

void FlattenerOptimizer::MergeCTEs(Select& outer_select, const std::shared_ptr<Select>& subquery) {
  if (!subquery) return;
  if (subquery->ctes.empty()) return;

  // Assumption: merged CTEs always have unique names (no conflicts).
  outer_select.ctes.insert(outer_select.ctes.end(), subquery->ctes.begin(), subquery->ctes.end());
}

bool FlattenerOptimizer::TryFlattenSubquery(Select& select_statement) {
  if (!select_statement.from.has_value()) return false;

  auto& from_statement = *select_statement.from.value();
  std::vector<std::shared_ptr<Source>> new_sources;
  bool flattened = false;

  for (auto& source : from_statement.sources) {
    if (!CanFlattenSubquery(source)) {
      new_sources.push_back(source);
      continue;
    }

    auto select_subquery = std::dynamic_pointer_cast<Select>(source->sourceable);
    const std::string old_alias = source->Alias();
    auto term_map = BuildTermMap(select_subquery);

    // If the subquery defines CTEs, lift them into the outer SELECT scope so that
    // any references remain valid after flattening.
    MergeCTEs(select_statement, select_subquery);

    // Replace the subquery with its inner sources
    for (auto& inner_source : select_subquery->from.value()->sources) {
      new_sources.push_back(inner_source);
    }

    // Update references in the outer query
    SourceAndColumnReplacer replacer(old_alias, term_map);
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

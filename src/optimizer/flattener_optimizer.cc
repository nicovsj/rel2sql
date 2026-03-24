#include "flattener_optimizer.h"

#include "replacers.h"

namespace rel2sql {

namespace sql::ast {

namespace {

// Builds a term map from a Union member's columns (column alias -> term).
std::unordered_map<std::string, std::shared_ptr<Term>> BuildTermMapFromUnionMember(
    const std::shared_ptr<Select>& member) {
  std::unordered_map<std::string, std::shared_ptr<Term>> column_map;
  for (auto& column : member->columns) {
    auto term_selectable = std::dynamic_pointer_cast<TermSelectable>(column);
    if (!term_selectable) continue;
    std::string aliased_column_name = term_selectable->Alias();
    column_map[aliased_column_name] = term_selectable->term;
  }
  return column_map;
}

}  // namespace

void FlattenerOptimizer::Visit(Select& select) {
  // Visit children first
  ExpressionVisitor::Visit(select);

  TryFlattenSubquery(select);
}

std::shared_ptr<Expression> FlattenerOptimizer::TryFlattenUnionSubquery(
    const std::shared_ptr<Select>& select) {
  if (!select || !select->from.has_value() || select->group_by.has_value()) return nullptr;

  auto& from = *select->from.value();
  if (from.sources.size() != 1) return nullptr;
  if (from.where.has_value()) return nullptr;  // TODO: push WHERE into members

  auto& source = from.sources[0];
  if (source->is_cte) return nullptr;

  auto union_query = std::dynamic_pointer_cast<Union>(source->sourceable);
  auto union_all_query = std::dynamic_pointer_cast<UnionAll>(source->sourceable);
  if (!union_query && !union_all_query) return nullptr;

  auto& members = union_query ? union_query->members : union_all_query->members;
  if (members.empty()) return nullptr;

  if (union_query && union_query->ctes_are_recursive) return nullptr;
  if (union_all_query && union_all_query->ctes_are_recursive) return nullptr;

  const std::string source_alias = source->Alias();

  // Check that all outer columns reference only the source.
  std::unordered_map<std::string, std::string> outer_to_subquery_column;
  for (auto& col : select->columns) {
    auto term_sel = std::dynamic_pointer_cast<TermSelectable>(col);
    if (!term_sel) return nullptr;  // Wildcard or other - skip

    auto column_term = std::dynamic_pointer_cast<Column>(term_sel->term);
    if (!column_term) return nullptr;  // Non-column expression - skip

    if (!column_term->source || column_term->source.value()->Alias() != source_alias) return nullptr;

    std::string outer_alias = term_sel->HasAlias() ? term_sel->Alias() : column_term->name;
    outer_to_subquery_column[outer_alias] = column_term->name;
  }

  if (outer_to_subquery_column.empty()) return nullptr;

  // Build projected members: for each member Select, push the projection down.
  std::vector<std::shared_ptr<Sourceable>> new_members;
  for (auto& member : members) {
    auto member_select = std::dynamic_pointer_cast<Select>(member);
    if (!member_select) return nullptr;  // Non-Select member - skip

    auto member_term_map = BuildTermMapFromUnionMember(member_select);

    std::vector<std::shared_ptr<Selectable>> new_columns;
    for (auto& [outer_alias, subquery_col] : outer_to_subquery_column) {
      auto it = member_term_map.find(subquery_col);
      if (it == member_term_map.end()) return nullptr;

      new_columns.push_back(std::make_shared<TermSelectable>(it->second, outer_alias));
    }

    std::shared_ptr<From> new_from = nullptr;
    if (member_select->from.has_value()) {
      new_from = std::make_shared<From>(*member_select->from.value());
    }

    auto new_select = std::make_shared<Select>(new_columns, new_from, member_select->is_distinct);
    new_members.push_back(new_select);
  }

  if (union_query) {
    auto result = std::make_shared<Union>(new_members);
    if (!union_query->ctes.empty()) {
      result->ctes = union_query->ctes;
      result->ctes_are_recursive = union_query->ctes_are_recursive;
    }
    return result;
  } else {
    auto result = std::make_shared<UnionAll>(new_members);
    if (!union_all_query->ctes.empty()) {
      result->ctes = union_all_query->ctes;
      result->ctes_are_recursive = union_all_query->ctes_are_recursive;
    }
    return result;
  }
}

bool FlattenerOptimizer::CanFlattenSubquery(const std::shared_ptr<Source>& source) {
  if (source->is_cte) return false;
  auto select_subquery = std::dynamic_pointer_cast<Select>(source->sourceable);
  if (!select_subquery) return false;
  if (select_subquery->ctes_are_recursive) return false;
  if (select_subquery->group_by.has_value()) return false;
  // Subqueries with FROM: flatten by inlining inner sources
  if (select_subquery->from.has_value()) return true;
  // Constant-only subqueries (no FROM): flatten by inlining the constant
  return CanFlattenConstantSubquery(source);
}

bool FlattenerOptimizer::CanFlattenConstantSubquery(const std::shared_ptr<Source>& source) {
  auto select_subquery = std::dynamic_pointer_cast<Select>(source->sourceable);
  if (!select_subquery || select_subquery->from.has_value()) return false;
  if (select_subquery->columns.empty()) return false;
  for (const auto& col : select_subquery->columns) {
    auto term_selectable = std::dynamic_pointer_cast<TermSelectable>(col);
    if (!term_selectable || !term_selectable->HasAlias()) return false;
    if (!std::dynamic_pointer_cast<Constant>(term_selectable->term)) return false;
  }
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

    // Replace the subquery with its inner sources (or none for constant subqueries)
    if (select_subquery->from.has_value()) {
      for (auto& inner_source : select_subquery->from.value()->sources) {
        new_sources.push_back(inner_source);
      }
      if (select_subquery->from.value()->where.has_value()) {
        MergeWhereConditions(from_statement, select_subquery->from.value()->where.value());
      }
    }

    // Update references in the outer query
    SourceAndColumnReplacer replacer(old_alias, term_map);
    base_expr_->Accept(replacer);

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

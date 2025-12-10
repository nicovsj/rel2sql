#include "cte_optimizer.h"

#include "replacers.h"

namespace rel2sql {
namespace sql::ast {

void CTEOptimizer::Visit(SelectStatement& select_statement) {
  // Visit children first
  ExpressionVisitor::Visit(select_statement);

  // Do not attempt to inline / move recursive CTEs into the FROM clause.
  // Recursive CTEs rely on the SQL engine's recursive WITH support, and
  // rewriting them as plain subqueries would break semantics.
  if (select_statement.ctes_are_recursive) {
    return;
  }

  std::vector<std::shared_ptr<Source>> new_ctes;

  for (auto& cte : select_statement.ctes) {
    if (!TryReplaceRedundantCTE(cte, select_statement)) {
      new_ctes.push_back(cte);
    }
  }

  select_statement.ctes = new_ctes;

  if (select_statement.ctes.empty()) {
    select_statement.ctes_are_recursive = false;
  }
}

bool CTEOptimizer::TryReplaceRedundantCTE(const std::shared_ptr<Source>& cte, SelectStatement& select_stmt) {
  auto cte_select = std::dynamic_pointer_cast<SelectStatement>(cte->sourceable);
  // CTE must be a SELECT statement
  if (!cte_select) return false;

  // Try the simple case first: wildcard CTE with single table source
  if (TryReplaceSimpleWildcardCTE(cte, cte_select)) {
    return true;
  }

  // General case: move CTE as subquery into FROM clause
  return TryReplaceGeneralCTE(cte, cte_select, select_stmt);
}

bool CTEOptimizer::TryReplaceSimpleWildcardCTE(const std::shared_ptr<Source>& cte,
                                               const std::shared_ptr<SelectStatement>& cte_select) {
  // CTE must have a single wildcard column
  if (cte_select->columns.size() != 1 || !std::dynamic_pointer_cast<Wildcard>(cte_select->columns[0])) return false;

  // CTE must have a single source
  if (!cte_select->from.has_value() || cte_select->from.value()->sources.size() != 1) return false;

  auto original_source = cte_select->from.value()->sources[0];
  auto table = std::dynamic_pointer_cast<Table>(original_source->sourceable);

  auto new_source = std::make_shared<Source>(original_source->sourceable, cte->Alias());
  // Create a map of CTE column aliases to their new names
  // Use the table's actual attribute names if available, otherwise fall back to A1, A2, etc.
  std::unordered_map<std::string, std::shared_ptr<Column>> column_map;
  for (size_t i = 0; i < cte->def_columns.size(); ++i) {
    if (table) {
      std::string column_name = table->GetAttributeName(i);
      column_map[cte->def_columns[i]] = std::make_shared<Column>(column_name, new_source);
    } else {
      std::string column_name = fmt::format("A{}", i + 1);
      column_map[cte->def_columns[i]] = std::make_shared<Column>(column_name, new_source);
    }
  }

  // Create a replacer that handles both source name and column name replacements
  SourceAndColumnReplacer replacer(cte->Alias(), new_source, column_map, false);
  base_expr_->Accept(replacer);

  return true;
}

bool CTEOptimizer::TryReplaceGeneralCTE(const std::shared_ptr<Source>& cte,
                                        const std::shared_ptr<SelectStatement>& cte_select,
                                        SelectStatement& select_stmt) {
  // Create a subquery source from the CTE's SELECT statement
  auto subquery_sourceable = std::static_pointer_cast<Sourceable>(cte_select);
  auto new_source = std::make_shared<Source>(subquery_sourceable, cte->Alias(), false);

  // Build column mapping
  // If def_columns is provided, use those as the column names
  // Otherwise, derive column names from the SELECT columns
  std::unordered_map<std::string, std::shared_ptr<Column>> column_map;

  if (!cte->def_columns.empty()) {
    // def_columns override the SELECT column names
    if (cte->def_columns.size() != cte_select->columns.size()) {
      // Mismatch in column count - cannot optimize
      return false;
    }
    for (size_t i = 0; i < cte->def_columns.size(); ++i) {
      std::string def_column_name = cte->def_columns[i];
      std::string select_column_name = GetColumnNameFromSelectable(cte_select->columns[i], i);
      column_map[def_column_name] = std::make_shared<Column>(select_column_name, new_source);
    }
  } else {
    // No def_columns - use SELECT column aliases or derive names
    for (size_t i = 0; i < cte_select->columns.size(); ++i) {
      std::string column_name = GetColumnNameFromSelectable(cte_select->columns[i], i);
      column_map[column_name] = std::make_shared<Column>(column_name, new_source);
    }
  }

  // Check if the CTE is referenced in FROM clause
  bool cte_in_from = false;
  if (select_stmt.from.has_value()) {
    for (auto& source : select_stmt.from.value()->sources) {
      if (source->Alias() == cte->Alias() && source->is_cte) {
        cte_in_from = true;
        break;
      }
    }
  }

  // If CTE is not in FROM, we need to add the new source to FROM
  // (The replacer will replace it if it is in FROM)
  if (!cte_in_from) {
    if (!select_stmt.from.has_value()) {
      select_stmt.from = std::make_shared<FromStatement>(new_source);
    } else {
      select_stmt.from.value()->sources.push_back(new_source);
    }
  }

  // Create a replacer that handles both source name and column name replacements
  SourceAndColumnReplacer replacer(cte->Alias(), new_source, column_map, false);
  base_expr_->Accept(replacer);

  return true;
}

std::string CTEOptimizer::GetColumnNameFromSelectable(const std::shared_ptr<Selectable>& selectable, size_t index) {
  // If the selectable has an alias, use it
  if (selectable->HasAlias()) {
    return selectable->Alias();
  }

  // If it's a TermSelectable with a Column term, use the column name
  auto term_selectable = std::dynamic_pointer_cast<TermSelectable>(selectable);
  if (term_selectable) {
    auto column = std::dynamic_pointer_cast<Column>(term_selectable->term);
    if (column) {
      return column->name;
    }
    // For other terms (constants, operations, etc.), use the term's string representation
    return term_selectable->term->ToString();
  }

  // For wildcards or other cases, derive a name
  return "A" + std::to_string(index + 1);
}

}  // namespace sql::ast
}  // namespace rel2sql

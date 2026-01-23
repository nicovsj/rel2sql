#include "cte_redundancy_optimizer.h"

#include "replacers.h"

namespace rel2sql {
namespace sql::ast {

void CTERedundancyOptimizer::Visit(Select& select) {
  // Visit children first
  ExpressionVisitor::Visit(select);

  // Do not attempt to optimize recursive CTEs.
  // Recursive CTEs rely on the SQL engine's recursive WITH support.
  if (select.ctes_are_recursive) {
    return;
  }

  std::vector<std::shared_ptr<Source>> new_ctes;

  // Try to eliminate CTEs that are redundant in terms of other CTEs
  for (auto& cte : select.ctes) {
    auto cte_select = std::dynamic_pointer_cast<Select>(cte->sourceable);
    if (cte_select && TryReplaceRedundantCTEInTermsOfOtherCTE(cte, cte_select, select)) {
      // CTE was eliminated, don't add it to new_ctes
      continue;
    }
    new_ctes.push_back(cte);
  }

  select.ctes = new_ctes;

  if (select.ctes.empty()) {
    select.ctes_are_recursive = false;
  }
}

bool CTERedundancyOptimizer::TryReplaceRedundantCTEInTermsOfOtherCTE(const std::shared_ptr<Source>& cte,
                                                                     const std::shared_ptr<Select>& cte_select,
                                                                     const Select& parent_select) {
  // Check if this CTE is a simple SELECT * FROM <other_cte> pattern
  // CTE must have a single wildcard column
  if (cte_select->columns.size() != 1 || !std::dynamic_pointer_cast<Wildcard>(cte_select->columns[0])) {
    return false;
  }

  // CTE must have a single source in FROM clause
  if (!cte_select->from.has_value() || cte_select->from.value()->sources.size() != 1) {
    return false;
  }

  // CTE must have no WHERE clause or GROUP BY
  if (cte_select->from.value()->where.has_value() || cte_select->group_by.has_value() || cte_select->is_distinct) {
    return false;
  }

  auto referenced_source = cte_select->from.value()->sources[0];

  // The referenced source must be another CTE (check by alias in parent's CTE list)
  std::shared_ptr<Source> referenced_cte = nullptr;
  for (const auto& parent_cte : parent_select.ctes) {
    if (parent_cte->Alias() == referenced_source->Alias() && parent_cte != cte && parent_cte->IsCTE()) {
      referenced_cte = parent_cte;
      break;
    }
  }

  if (!referenced_cte) {
    return false;
  }

  // Get the column names from the referenced CTE
  // If the referenced CTE has def_columns, use those
  // Otherwise, we need to get column names from its SELECT statement
  auto referenced_cte_select = std::dynamic_pointer_cast<Select>(referenced_cte->sourceable);
  if (!referenced_cte_select) {
    return false;
  }

  // Build column mapping from redundant CTE's def_columns to referenced CTE's columns
  std::unordered_map<std::string, std::shared_ptr<Term>> term_map;

  // Determine the number of columns
  size_t num_columns;
  if (!cte->def_columns.empty()) {
    num_columns = cte->def_columns.size();
  } else {
    // If no def_columns, we can't determine the mapping
    return false;
  }

  // Get column names from referenced CTE
  std::vector<std::string> referenced_column_names;
  if (!referenced_cte->def_columns.empty()) {
    // Use def_columns from referenced CTE
    referenced_column_names = referenced_cte->def_columns;
  } else {
    // Derive column names from SELECT statement
    for (size_t i = 0; i < referenced_cte_select->columns.size(); ++i) {
      referenced_column_names.push_back(GetColumnNameFromSelectable(referenced_cte_select->columns[i], i));
    }
  }

  // Check if column counts match
  if (num_columns != referenced_column_names.size()) {
    return false;
  }

  // Create column mapping: redundant_cte.def_column[i] -> referenced_cte.column[i]
  for (size_t i = 0; i < num_columns; ++i) {
    std::string redundant_column_name = cte->def_columns[i];
    std::string referenced_column_name = referenced_column_names[i];

    // Create a column reference to the referenced CTE
    term_map[redundant_column_name] = std::make_shared<Column>(referenced_column_name, referenced_cte);
  }

  // Replace all references to the redundant CTE with references to the referenced CTE
  SourceAndColumnReplacer replacer(cte->Alias(), referenced_cte, term_map, false);
  base_expr_->Accept(replacer);

  return true;
}

std::string CTERedundancyOptimizer::GetColumnNameFromSelectable(const std::shared_ptr<Selectable>& selectable,
                                                                size_t index) {
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

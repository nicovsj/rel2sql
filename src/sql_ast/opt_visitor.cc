#include "opt_visitor.h"

#include "replacers.h"

namespace sql::ast {

void OptimizerVisitor::Visit(Expression& expr) { expr.Accept(*this); }

bool OptimizerVisitor::TryReplaceConstantInWhere(const std::shared_ptr<Source>& source, FromStatement& from_statement) {
  /**
   * Tries to replace a constant in the WHERE condition of a FROM statement.
   *
   * This function checks if the given source is a SelectStatement with a single column that is a constant.
   * If so, it replaces the constant in the WHERE condition of the given FROM statement.
   *
   * @param source The source to check and potentially replace the constant from.
   * @param from_statement The FROM statement whose WHERE condition may be modified.
   * @return true if a constant was successfully replaced in the WHERE condition, false otherwise.
   */
  if (auto select = std::dynamic_pointer_cast<SelectStatement>(source->sourceable)) {
    if (select->columns.size() == 1) {
      if (auto term_selectable = std::dynamic_pointer_cast<TermSelectable>(select->columns[0])) {
        if (auto constant = std::dynamic_pointer_cast<Constant>(term_selectable->term)) {
          // Replace the constant in the WHERE condition
          if (from_statement.where) {
            ConstantReplacer replacer(source->Alias(), term_selectable->Alias(), constant);
            from_statement.where.value()->Accept(replacer);
            return true;
          }
        }
      }
    }
  }
  return false;
}

void OptimizerVisitor::Visit(FromStatement& from_statement) {
  std::vector<std::shared_ptr<Source>> new_sources;
  for (auto& source : from_statement.sources) {
    // If the optimization succeeds, the source needs to be removed from the FROM statement
    if (!TryReplaceConstantInWhere(source, from_statement)) {
      new_sources.push_back(source);
    }
  }
  from_statement.sources = new_sources;

  for (auto& source : from_statement.sources) {
    Visit(*source);
  }
}

void OptimizerVisitor::Visit(SelectStatement& select_statement) {
  std::vector<std::shared_ptr<Source>> new_ctes;
  for (auto& cte : select_statement.ctes) {
    if (!TryReplaceRedundantCTE(cte, select_statement)) {
      new_ctes.push_back(cte);
    }
  }
  select_statement.ctes = new_ctes;

  if (select_statement.from.has_value()) {
    Visit(*select_statement.from.value());
  }

  TryFlattenSubquery(select_statement);
}

bool OptimizerVisitor::TryReplaceRedundantCTE(const std::shared_ptr<Source>& cte, SelectStatement& select_stmt) {
  if (auto cte_select = std::dynamic_pointer_cast<SelectStatement>(cte->sourceable)) {
    if (cte_select->columns.size() == 1 && std::dynamic_pointer_cast<Wildcard>(cte_select->columns[0])) {
      if (cte_select->from.has_value() && cte_select->from.value()->sources.size() == 1) {
        if (auto table = std::dynamic_pointer_cast<Table>(cte_select->from.value()->sources[0]->sourceable)) {
          // Create a map of CTE column aliases to their new names (A1, A2, etc.)
          std::unordered_map<std::string, std::string> column_map;
          for (size_t i = 0; i < cte->def_columns.size(); ++i) {
            column_map[cte->def_columns[i]] = fmt::format("A{}", i + 1);
          }

          // Create a replacer that handles both source name and column name replacements
          SourceAndColumnReplacer replacer(cte->Alias(), table->name, column_map);
          select_stmt.Accept(replacer);
          return true;
        }
      }
    }
  }
  return false;
}

bool OptimizerVisitor::TryFlattenSubquery(SelectStatement& select_statement) {
  if (!select_statement.from.has_value()) {
    return false;
  }

  auto& from_statement = *select_statement.from.value();
  bool flattened = false;

  for (auto& source : from_statement.sources) {
    auto select_subquery = std::dynamic_pointer_cast<SelectStatement>(source->sourceable);

    if (!select_subquery || !select_subquery->from.has_value() || select_subquery->from.value()->where.has_value() ||
        select_subquery->from.value()->sources.size() != 1) {
      continue;
    }

    auto inner_source = select_subquery->from.value()->sources[0];
    std::string old_alias = source->Alias();
    std::string new_alias = inner_source->Alias();

    // Create a map of old column names to new column names
    std::unordered_map<std::string, std::string> column_map;
    for (size_t i = 0; i < select_subquery->columns.size(); ++i) {
      auto& column = select_subquery->columns[i];
      if (auto term_selectable = std::dynamic_pointer_cast<TermSelectable>(column)) {
        if (auto term = std::dynamic_pointer_cast<Column>(term_selectable->term)) {
          column_map[term_selectable->Alias()] = term->name;
        }
      }
    }

    // Update references in the outer query
    SourceAndColumnReplacer replacer(old_alias, new_alias, column_map);

    select_statement.Accept(replacer);

    // Replace the subquery with its inner source
    *source = *inner_source;

    flattened = true;
  }

  return flattened;
}

}  // namespace sql::ast

#include "opt_visitor.h"

#include "replacers.h"

namespace sql::ast {

void OptimizerVisitor::Visit(Expression& expr) {
  bool is_base_expr = !base_expr_;
  if (is_base_expr) {
    base_expr_ = std::shared_ptr<Expression>(&expr, [](Expression*) {});
  }

  expr.Accept(*this);

  if (is_base_expr) {
    base_expr_.reset();
  }
}

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
        auto original_source = cte_select->from.value()->sources[0];

        if (auto table = std::dynamic_pointer_cast<Table>(original_source->sourceable)) {
          auto new_source = std::make_shared<Source>(table, cte->Alias());
          // Create a map of CTE column aliases to their new names (A1, A2, etc.)
          std::unordered_map<std::string, std::shared_ptr<Column>> column_map;
          for (size_t i = 0; i < cte->def_columns.size(); ++i) {
            column_map[cte->def_columns[i]] = std::make_shared<Column>(fmt::format("A{}", i + 1), new_source);
          }

          // Create a replacer that handles both source name and column name replacements
          SourceAndColumnReplacer replacer(cte->Alias(), new_source, column_map, false);
          base_expr_->Accept(replacer);
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

  std::vector<std::shared_ptr<Source>> new_sources;

  for (auto& source : from_statement.sources) {
    auto select_subquery = std::dynamic_pointer_cast<SelectStatement>(source->sourceable);

    if (!select_subquery || !select_subquery->from.has_value() || select_subquery->group_by.has_value()) {
      new_sources.push_back(source);
      continue;
    }

    std::unordered_map<std::string, std::shared_ptr<Column>> column_map;

    std::string old_alias = source->Alias();

    for (auto& inner_source : select_subquery->from.value()->sources) {
      for (size_t i = 0; i < select_subquery->columns.size(); ++i) {
        auto& column = select_subquery->columns[i];
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

  return flattened;
}

}  // namespace sql::ast

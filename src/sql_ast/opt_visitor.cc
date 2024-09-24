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
}

bool OptimizerVisitor::TryReplaceRedundantCTE(const std::shared_ptr<Source>& cte, SelectStatement& select_stmt) {
  if (auto cte_select = std::dynamic_pointer_cast<SelectStatement>(cte->sourceable)) {
    if (cte_select->columns.size() == 1 && std::dynamic_pointer_cast<Wildcard>(cte_select->columns[0])) {
      if (cte_select->from.has_value() && cte_select->from.value()->sources.size() == 1) {
        if (auto table = std::dynamic_pointer_cast<Table>(cte_select->from.value()->sources[0]->sourceable)) {
          SourceNameReplacer replacer(cte->Alias(), table->name);
          select_stmt.Accept(replacer);
          return true;
        }
      }
    }
  }
  return false;
}

}  // namespace sql::ast

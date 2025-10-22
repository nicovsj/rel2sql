#include "cte_optimizer.h"

#include "replacers.h"

namespace sql::ast {

void CTEOptimizer::Visit(SelectStatement& select_statement) {
  std::vector<std::shared_ptr<Source>> new_ctes;
  for (auto& cte : select_statement.ctes) {
    if (!TryReplaceRedundantCTE(cte, select_statement)) {
      new_ctes.push_back(cte);
    }
  }
  select_statement.ctes = new_ctes;
}

bool CTEOptimizer::TryReplaceRedundantCTE(const std::shared_ptr<Source>& cte, SelectStatement& select_stmt) {
  if (auto cte_select = std::dynamic_pointer_cast<SelectStatement>(cte->sourceable)) {
    if (cte_select->columns.size() == 1 && std::dynamic_pointer_cast<Wildcard>(cte_select->columns[0])) {
      if (cte_select->from.has_value() && cte_select->from.value()->sources.size() == 1) {
        auto original_source = cte_select->from.value()->sources[0];

        if (auto table = std::dynamic_pointer_cast<Table>(original_source->sourceable)) {
          auto new_source = std::make_shared<Source>(table, cte->Alias());
          // Create a map of CTE column aliases to their new names
          // Use the table's actual attribute names if available, otherwise fall back to A1, A2, etc.
          std::unordered_map<std::string, std::shared_ptr<Column>> column_map;
          for (size_t i = 0; i < cte->def_columns.size(); ++i) {
            std::string column_name = table->GetAttributeName(i);
            column_map[cte->def_columns[i]] = std::make_shared<Column>(column_name, new_source);
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

}  // namespace sql::ast

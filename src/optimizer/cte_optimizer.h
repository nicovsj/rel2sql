#ifndef CTE_OPTIMIZER_H
#define CTE_OPTIMIZER_H

#include "base_optimizer.h"

namespace rel2sql {
namespace sql::ast {

class CTEOptimizer : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  void Visit(SelectStatement& select_statement) override;

 private:
  bool TryReplaceRedundantCTE(const std::shared_ptr<Source>& cte, SelectStatement& select_statement);
  bool TryReplaceSimpleWildcardCTE(const std::shared_ptr<Source>& cte,
                                    const std::shared_ptr<SelectStatement>& cte_select,
                                    SelectStatement& select_stmt);
  bool TryReplaceGeneralCTE(const std::shared_ptr<Source>& cte,
                            const std::shared_ptr<SelectStatement>& cte_select,
                            SelectStatement& select_stmt);
  std::string GetColumnNameFromSelectable(const std::shared_ptr<Selectable>& selectable, size_t index);
};  // class CTEOptimizer

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // CTE_OPTIMIZER_H

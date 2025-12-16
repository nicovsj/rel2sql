#ifndef CTE_INLINER_H
#define CTE_INLINER_H

#include "base_optimizer.h"

namespace rel2sql {
namespace sql::ast {

class CTEInliner : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  void Visit(SelectStatement& select_statement) override;

 private:
  bool TryReplaceRedundantCTE(const std::shared_ptr<Source>& cte);

  bool TryReplaceSimpleWildcardCTE(const std::shared_ptr<Source>& cte,
                                   const std::shared_ptr<SelectStatement>& cte_select);

  bool TryReplaceGeneralCTE(const std::shared_ptr<Source>& cte, const std::shared_ptr<SelectStatement>& cte_select);

  std::string GetColumnNameFromSelectable(const std::shared_ptr<Selectable>& selectable, size_t index);
};  // class CTEInliner

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // CTE_INLINER_H

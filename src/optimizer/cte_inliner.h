#ifndef CTE_INLINER_H
#define CTE_INLINER_H

#include "base_optimizer.h"

namespace rel2sql {
namespace sql::ast {

class CTEInliner : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  void Visit(Select& select) override;

 private:
  bool TryReplaceRedundantCTE(const std::shared_ptr<Source>& cte, const Select& owning_select);

  bool TryReplaceSimpleWildcardCTE(const std::shared_ptr<Source>& cte, const std::shared_ptr<Select>& cte_select);

  bool TryReplaceGeneralCTE(const std::shared_ptr<Source>& cte, const std::shared_ptr<Select>& cte_select);

  std::size_t CountCTEReferencesInFromClauses(const Select& root, const std::string& cte_name);

  std::string GetColumnNameFromSelectable(const std::shared_ptr<Selectable>& selectable, size_t index);
};  // class CTEInliner

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // CTE_INLINER_H

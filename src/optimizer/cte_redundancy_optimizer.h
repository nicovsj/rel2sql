#ifndef CTE_REDUNDANCY_OPTIMIZER_H
#define CTE_REDUNDANCY_OPTIMIZER_H

#include "base_optimizer.h"

namespace rel2sql {
namespace sql::ast {

class CTERedundancyOptimizer : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  void Visit(Select& select) override;

 private:
  bool TryReplaceRedundantCTEInTermsOfOtherCTE(const std::shared_ptr<Source>& cte,
                                               const std::shared_ptr<Select>& cte_select, const Select& parent_select);

  std::string GetColumnNameFromSelectable(const std::shared_ptr<Selectable>& selectable, size_t index);
};  // class CTERedundancyOptimizer

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // CTE_REDUNDANCY_OPTIMIZER_H

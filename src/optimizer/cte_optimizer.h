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
};  // class CTEOptimizer

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // CTE_OPTIMIZER_H

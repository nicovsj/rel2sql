#ifndef FLATTENER_OPTIMIZER_H
#define FLATTENER_OPTIMIZER_H

#include "base_optimizer.h"

namespace sql::ast {

class FlattenerOptimizer : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  void Visit(SelectStatement& select_statement) override;

 private:
  bool TryFlattenSubquery(SelectStatement& select_statement);
};  // class FlattenerOptimizer

}  // namespace sql::ast

#endif  // FLATTENER_OPTIMIZER_H

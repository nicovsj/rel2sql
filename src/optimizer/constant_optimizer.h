#ifndef CONSTANT_OPTIMIZER_H
#define CONSTANT_OPTIMIZER_H

#include "base_optimizer.h"

namespace rel2sql {

namespace sql::ast {

class ConstantOptimizer : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  void Visit(From& from) override;

};  // class ConstantOptimizer

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // CONSTANT_OPTIMIZER_H

#ifndef OPTIMIZER_VISITOR_H
#define OPTIMIZER_VISITOR_H

#include "base_optimizer.h"
#include "constant_optimizer.h"
#include "cte_inliner.h"
#include "cte_redundancy_optimizer.h"
#include "flattener_optimizer.h"
#include "self_join_optimizer.h"
#include "sql_ast/expr_visitor.h"
#include "sql_ast/sql_ast.h"

namespace rel2sql {
namespace sql::ast {

class Optimizer : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  void Visit(Select& select) override {
    // Cast Select to Expression
    auto& expression = static_cast<Expression&>(select);

    constant_optimizer_.Visit(expression);

    flattener_optimizer_.Visit(expression);

    cte_redundancy_optimizer_.Visit(expression);

    self_join_optimizer_.Visit(expression);

    cte_inliner_.Visit(expression);

    flattener_optimizer_.Visit(expression);

    self_join_optimizer_.Visit(expression);
  }

 private:
  CTERedundancyOptimizer cte_redundancy_optimizer_;
  CTEInliner cte_inliner_;
  ConstantOptimizer constant_optimizer_;
  FlattenerOptimizer flattener_optimizer_;
  SelfJoinOptimizer self_join_optimizer_;
};  // class Optimizer

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // OPTIMIZER_VISITOR_H

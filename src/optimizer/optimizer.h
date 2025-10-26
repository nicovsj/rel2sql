#ifndef OPTIMIZER_VISITOR_H
#define OPTIMIZER_VISITOR_H

#include "base_optimizer.h"
#include "constant_optimizer.h"
#include "cte_optimizer.h"
#include "flattener_optimizer.h"
#include "self_join_optimizer.h"
#include "structs/expr_visitor.h"
#include "structs/sql_ast.h"

namespace rel2sql {
namespace sql::ast {

class Optimizer : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  void Visit(SelectStatement& select_statement) override {
    // Cast SelectStatement to Expression
    auto& expression = static_cast<Expression&>(select_statement);

    cte_optimizer_.Visit(expression);

    if (select_statement.from.has_value()) {
      constant_optimizer_.Visit(*select_statement.from.value());
    }

    flattener_optimizer_.Visit(expression);
    // self_join_optimizer_.Visit(expression);
  }

 private:
  CTEOptimizer cte_optimizer_;
  ConstantOptimizer constant_optimizer_;
  FlattenerOptimizer flattener_optimizer_;
  SelfJoinOptimizer self_join_optimizer_;
};  // class Optimizer

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // OPTIMIZER_VISITOR_H

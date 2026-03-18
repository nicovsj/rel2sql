#ifndef OPTIMIZER_VISITOR_H
#define OPTIMIZER_VISITOR_H

#include <memory>

#include "base_optimizer.h"
#include "canonical_form_visitor.h"
#include "constant_optimizer.h"
#include "cte_inliner.h"
#include "cte_redundancy_optimizer.h"
#include "expression_simplifier_optimizer.h"
#include "flattener_optimizer.h"
#include "self_join_optimizer.h"
#include "sql_ast/expr_visitor.h"
#include "sql_ast/sql_ast.h"

namespace rel2sql {
namespace sql::ast {

class Optimizer : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  // Optimizes the expression. May return a different expression when e.g. a
  // Select wrapping a Union is flattened to the Union directly.
  std::shared_ptr<Expression> Optimize(std::shared_ptr<Expression> expr) {
    if (!expr) return expr;
    auto select = std::dynamic_pointer_cast<Select>(expr);
    if (select) {
      auto replacement = FlattenerOptimizer::TryFlattenUnionSubquery(select);
      if (replacement) {
        return Optimize(replacement);
      }
    }
    Visit(*expr);
    return expr;
  }

  void Visit(Select& select) override {
    // Cast Select to Expression
    auto& expression = static_cast<Expression&>(select);

    constant_optimizer_.Visit(expression);

    flattener_optimizer_.Visit(expression);

    cte_redundancy_optimizer_.Visit(expression);

    expression_simplifier_optimizer_.Visit(expression);
    canonical_form_visitor_.Visit(expression);

    self_join_optimizer_.Visit(expression);

    cte_inliner_.Visit(expression);

    flattener_optimizer_.Visit(expression);

    expression_simplifier_optimizer_.Visit(expression);
    canonical_form_visitor_.Visit(expression);

    self_join_optimizer_.Visit(expression);
  }

 private:
  CTERedundancyOptimizer cte_redundancy_optimizer_;
  CTEInliner cte_inliner_;
  CanonicalFormVisitor canonical_form_visitor_;
  ConstantOptimizer constant_optimizer_;
  ExpressionSimplifierOptimizer expression_simplifier_optimizer_;
  FlattenerOptimizer flattener_optimizer_;
  SelfJoinOptimizer self_join_optimizer_;
};  // class Optimizer

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // OPTIMIZER_VISITOR_H

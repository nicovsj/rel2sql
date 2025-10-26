#ifndef BASE_OPTIMIZER_H
#define BASE_OPTIMIZER_H

#include "structs/expr_visitor.h"
#include "structs/sql_ast.h"

namespace rel2sql {
namespace sql::ast {

class BaseOptimizer : public ExpressionVisitor {
 public:
  BaseOptimizer() = default;
  virtual ~BaseOptimizer() = default;

  // Stores the first Expression visited in base_expr_.
  virtual void Visit(Expression& expr) override {
    bool is_base_expr = !base_expr_;
    if (is_base_expr) {
      base_expr_ = std::shared_ptr<Expression>(&expr, [](Expression*) {});
    }

    expr.Accept(*this);

    if (is_base_expr) {
      base_expr_.reset();
    }
  }

 protected:
  std::shared_ptr<Expression> base_expr_;
};

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // BASE_OPTIMIZER_H

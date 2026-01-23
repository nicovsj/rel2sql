#ifndef VALIDATING_OPTIMIZER_H
#define VALIDATING_OPTIMIZER_H

#include <format>
#include <stdexcept>
#include <string>

#include "base_optimizer.h"
#include "constant_optimizer.h"
#include "cte_inliner.h"
#include "cte_redundancy_optimizer.h"
#include "flattener_optimizer.h"
#include "self_join_optimizer.h"
#include "sql_ast/expr_visitor.h"
#include "sql_ast/sql_ast.h"
#include "validator/validator.h"

namespace rel2sql {
namespace sql::ast {

/**
 * A debug/testing optimizer that validates the SQL expression after each optimization step.
 * This helps identify which optimizer is causing validation failures.
 *
 * It runs the same optimization sequence as the production Optimizer but validates
 * after each step, throwing an exception or reporting which step failed.
 */
class ValidatingOptimizer : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  /**
   * Validates the expression and throws an exception with details if validation fails.
   * @param expression The expression to validate
   * @param step_name The name of the optimization step that just completed
   */
  void ValidateAndReport(Expression& expression, const std::string& step_name) {
    Validator validator;
    validator.Visit(expression);

    if (!validator.IsValid()) {
      auto errors = validator.GetErrors();
      std::string error_msg =
          std::format("Validation failed after optimization step '{}' with {} error(s):\n", step_name, errors.size());
      for (size_t i = 0; i < errors.size(); ++i) {
        error_msg += std::format("  {}. {}", i + 1, errors[i].message);
        if (i < errors.size() - 1) {
          error_msg += "\n";
        }
      }
      throw std::runtime_error(error_msg);
    }
  }

  void Visit(Select& select) override {
    // Cast Select to Expression
    auto& expression = static_cast<Expression&>(select);

    // Validate initial state
    ValidateAndReport(expression, "initial state");

    constant_optimizer_.Visit(expression);
    ValidateAndReport(expression, "constant_optimizer");

    flattener_optimizer_.Visit(expression);
    ValidateAndReport(expression, "flattener_optimizer (first pass)");

    cte_redundancy_optimizer_.Visit(expression);
    ValidateAndReport(expression, "cte_redundancy_optimizer");

    self_join_optimizer_.Visit(expression);
    ValidateAndReport(expression, "self_join_optimizer (first pass)");

    cte_inliner_.Visit(expression);
    ValidateAndReport(expression, "cte_inliner");

    flattener_optimizer_.Visit(expression);
    ValidateAndReport(expression, "flattener_optimizer (second pass)");

    self_join_optimizer_.Visit(expression);
    ValidateAndReport(expression, "self_join_optimizer (second pass)");
  }

 private:
  CTERedundancyOptimizer cte_redundancy_optimizer_;
  CTEInliner cte_inliner_;
  ConstantOptimizer constant_optimizer_;
  FlattenerOptimizer flattener_optimizer_;
  SelfJoinOptimizer self_join_optimizer_;
};  // class ValidatingOptimizer

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // VALIDATING_OPTIMIZER_H

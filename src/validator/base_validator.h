#ifndef BASE_VALIDATOR_H
#define BASE_VALIDATOR_H

#include <string>
#include <vector>

#include "sql_ast/expr_visitor.h"
#include "sql_ast/sql_ast.h"

namespace rel2sql {
namespace sql::ast {

/**
 * Structure to hold validation error information
 */
struct ValidationError {
  std::string message;
  // Additional context can be added here as needed
};

class BaseValidator : public ExpressionVisitor {
 public:
  BaseValidator() = default;
  virtual ~BaseValidator() = default;

  // Returns true if validation passed, false otherwise
  virtual bool IsValid() const = 0;

  // Returns a list of validation errors
  virtual std::vector<ValidationError> GetErrors() const = 0;

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

#endif  // BASE_VALIDATOR_H

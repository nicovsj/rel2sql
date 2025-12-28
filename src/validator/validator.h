#ifndef VALIDATOR_H
#define VALIDATOR_H

#include "base_validator.h"
#include "table_reference_validator.h"

namespace rel2sql {
namespace sql::ast {

class Validator : public BaseValidator {
 public:
  using BaseValidator::Visit;

  void Visit(SelectStatement& select_statement) override {
    // Cast SelectStatement to Expression
    auto& expression = static_cast<Expression&>(select_statement);

    table_reference_validator_.Visit(expression);
  }

  // Returns true if all validations passed, false otherwise
  bool IsValid() const override {
    return table_reference_validator_.IsValid();
  }

  // Returns all validation errors from all validators
  std::vector<ValidationError> GetErrors() const override {
    return table_reference_validator_.GetErrors();
  }

 private:
  TableReferenceValidator table_reference_validator_;
};  // class Validator

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // VALIDATOR_H

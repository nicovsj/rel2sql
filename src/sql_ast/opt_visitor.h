#ifndef OPTIMIZER_VISITOR_H
#define OPTIMIZER_VISITOR_H

#include "sql_ast/expr_visitor.h"
#include "sql_ast/sql_ast.h"

namespace sql::ast {

class OptimizerVisitor : public ExpressionVisitor {
 public:
  OptimizerVisitor() = default;
  virtual ~OptimizerVisitor() = default;

  void Visit(Expression& expr) override;

  void Visit(FromStatement& from_statement) override;

 private:
  bool TryReplaceConstantInWhere(const std::shared_ptr<Source>& source, FromStatement& from_statement);
};

}  // namespace sql::ast

#endif  // OPTIMIZER_VISITOR_H

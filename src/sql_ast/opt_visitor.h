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

  void Visit(SelectStatement& select_statement) override;
  void Visit(FromStatement& from_statement) override;

 private:
  bool TryReplaceConstantInWhere(const std::shared_ptr<Source>& source, FromStatement& from_statement);
  bool TryReplaceRedundantCTE(const std::shared_ptr<Source>& cte, SelectStatement& select_statement);
  bool TryFlattenSubquery(SelectStatement& select_statement);

  std::shared_ptr<Expression> base_expr_;
};

}  // namespace sql::ast

#endif  // OPTIMIZER_VISITOR_H

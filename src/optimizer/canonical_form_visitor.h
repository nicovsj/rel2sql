#ifndef CANONICAL_FORM_VISITOR_H
#define CANONICAL_FORM_VISITOR_H

#include "sql_ast/expr_visitor.h"
#include "sql_ast/sql_ast.h"

namespace rel2sql {
namespace sql::ast {

/**
 * Populates lhs_canonical and rhs_canonical on equality ComparisonConditions.
 * Read-only analysis; no AST modification. Must run before SelfJoinOptimizer.
 */
class CanonicalFormVisitor : public ExpressionVisitor {
 public:
  using ExpressionVisitor::Visit;

  void Visit(Select& select) override;
  void Visit(ComparisonCondition& comparison_condition) override;
};

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // CANONICAL_FORM_VISITOR_H

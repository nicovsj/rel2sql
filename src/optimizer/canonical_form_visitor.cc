#include "canonical_form_visitor.h"

#include "canonical_form.h"

namespace rel2sql {
namespace sql::ast {

void CanonicalFormVisitor::Visit(Select& select) {
  // Visit CTEs first so conditions inside them get canonical forms
  for (auto& cte : select.ctes) {
    Visit(*cte);
  }
  ExpressionVisitor::Visit(select);
}

void CanonicalFormVisitor::Visit(ComparisonCondition& comparison_condition) {
  if (comparison_condition.op == CompOp::EQ) {
    comparison_condition.lhs_canonical = ComputeCanonicalForm(comparison_condition.lhs);
    comparison_condition.rhs_canonical = ComputeCanonicalForm(comparison_condition.rhs);
  }
  ExpressionVisitor::Visit(comparison_condition);
}

}  // namespace sql::ast
}  // namespace rel2sql

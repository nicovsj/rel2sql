#ifndef EXPRESSION_SIMPLIFIER_OPTIMIZER_H
#define EXPRESSION_SIMPLIFIER_OPTIMIZER_H

#include "base_optimizer.h"

namespace rel2sql {
namespace sql::ast {

/**
 * Simplifies arithmetic expressions to enable downstream optimizations
 * (e.g. self-join detection). Performs constant folding for + and - so that
 * expressions like (-1) + col + 1 simplify to col.
 */
class ExpressionSimplifierOptimizer : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  void Visit(Select& select) override;
  void Visit(Operation& operation) override;
  void Visit(ComparisonCondition& comparison_condition) override;
  void Visit(LogicalCondition& logical_condition) override;
  void Visit(From& from) override;
  void Visit(TermSelectable& term_selectable) override;
  void Visit(ParenthesisTerm& parenthesis_term) override;
  void Visit(Function& function) override;
  void Visit(CaseWhen& case_when) override;
};

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // EXPRESSION_SIMPLIFIER_OPTIMIZER_H

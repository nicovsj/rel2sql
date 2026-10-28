#ifndef REWRITER_NEGATED_COMPARISON_REWRITER_H
#define REWRITER_NEGATED_COMPARISON_REWRITER_H

#include "rel_ast/rel_ast_visitor.h"

namespace rel2sql {

/**
 * Rewrites the negation of a comparison into the opposite comparison:
 *
 *   not (t1 = t2)  =>  t1 != t2
 *   not (t1 < t2)  =>  t1 >= t2, and so on.
 *
 * Both formulas need the variables of t1 and t2 to be bound by their context, and they hold for the same values.
 * Translated as a negation, the comparison would be an anti-join against the bound of its variables; translated as
 * a comparison, it is a filter.
 */
class NegatedComparisonRewriter : public BaseRelVisitor {
 public:
  using BaseRelVisitor::Visit;

  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelNegation>& node) override;
};

}  // namespace rel2sql

#endif  // REWRITER_NEGATED_COMPARISON_REWRITER_H

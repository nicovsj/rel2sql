#ifndef REWRITER_EXPRESSION_AS_TERM_REWRITER_H
#define REWRITER_EXPRESSION_AS_TERM_REWRITER_H

#include "rel_ast/rel_ast_visitor.h"

namespace rel2sql {

/**
 * Rewrites expressions appearing as terms (but not as parameters to relations)
 * into relational form by introducing a fresh variable:
 *
 * 1. [x,y]: x + y + 1  →  [x,y]: { (z) : z = x + y + 1 }
 * 2. (..., x+y+1, ...) →  (..., (z): z=x+y+1, ...)
 * 3. {...; x+y+1; ...} →  {...; (z): z=x+y+1; ...}
 * 4. x+y+1 where F →   (z): z = x+y+1 and F
 *
 * Applies in RelBindingsExpr body, RelProductExpr elements, and RelUnion
 * elements. Does NOT apply to expressions used as relation parameters.
 */
class TermRewriter : public BaseRelVisitor {
 public:
  using BaseRelVisitor::Visit;

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelExprAbstraction>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelCondition>& node) override;
  std::shared_ptr<RelUnion> Visit(const std::shared_ptr<RelUnion>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelProduct>& node) override;

 private:
  std::string FreshVarName();
  std::shared_ptr<RelExpr> WrapTermExpr(std::shared_ptr<RelTerm> term, bool wrap_in_abs);
  std::shared_ptr<RelExpr> WrapConditionExpr(std::shared_ptr<RelCondition> expr);
  std::shared_ptr<RelExpr> WrapExpr(std::shared_ptr<RelExpr> expr, bool wrap_in_abs);

  int fresh_var_counter_ = 0;
};

}  // namespace rel2sql

#endif  // REWRITER_EXPRESSION_AS_TERM_REWRITER_H

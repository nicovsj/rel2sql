#ifndef REWRITER_EXPRESSION_AS_TERM_REWRITER_H
#define REWRITER_EXPRESSION_AS_TERM_REWRITER_H

#include "rewriter/base_rewriter.h"

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
 * Applies in RelBindingsExpr body, RelProductExpr elements, and RelAbstraction
 * elements. Does NOT apply to expressions used as relation parameters.
 */
class ExpressionAsTermRewriter : public BaseRelRewriter {
 public:
  void Visit(RelBindingsExpr& node) override;
  void Visit(RelConditionExpr& node) override;
  void Visit(RelAbstraction& node) override;
  void Visit(RelProductExpr& node) override;

 private:
  std::string FreshVarName();
  std::shared_ptr<RelExpr> WrapTermExpr(std::shared_ptr<RelTermExpr> expr, bool wrap_in_abs);
  std::shared_ptr<RelExpr> WrapConditionExpr(std::shared_ptr<RelConditionExpr> expr);
  std::shared_ptr<RelExpr> WrapExpr(std::shared_ptr<RelExpr> expr, bool wrap_in_abs);

  int fresh_var_counter_ = 0;
};

}  // namespace rel2sql

#endif  // REWRITER_EXPRESSION_AS_TERM_REWRITER_H

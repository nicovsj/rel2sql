#ifndef REWRITER_EXPRESSION_AS_TERM_REWRITER_H
#define REWRITER_EXPRESSION_AS_TERM_REWRITER_H

#include "rel_ast/rel_ast_visitor.h"

namespace rel2sql {

class RelContextBuilder;

/**
 * Rewrites expressions appearing as terms (but not as parameters to relations)
 * into relational form by introducing a fresh variable:
 *
 * 1. [x,y]: x + y + 1  →  [x,y]: { (z) : z = x + y + 1 }
 * 2. (..., x+y+1, ...) →  (..., (z): z=x+y+1, ...)
 * 3. {...; x+y+1; ...} →  {...; (z): z=x+y+1; ...}
 * 4. x+y+1 where F →   (z): z = x+y+1 and F
 * 5. A(...,x+y+1,...)  →   exists((z) | A(...,z,...) and z = x+y+1)
 * 6. A[a1,..., ai-1, x+y+1, ai+1,..., ak]  →
 *      (zk+1, ..., z|A|) : exists((z) | A(a1,..., ai-1, z, ai+1,..., ak, zk+1, ..., z|A|) and z = x+y+1)
 *
 * Applies in RelBindingsExpr body, RelProductExpr elements, RelUnion
 * elements, and relation application parameters
 */
class TermRewriter : public BaseRelVisitor {
 public:
  using BaseRelVisitor::Visit;

  TermRewriter() : container_(nullptr) {}
  explicit TermRewriter(const RelContextBuilder* container) : container_(container) {}

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelExprAbstraction>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelCondition>& node) override;
  std::shared_ptr<RelUnion> Visit(const std::shared_ptr<RelUnion>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelProduct>& node) override;
  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelFullApplication>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelPartialApplication>& node) override;

 private:
  std::string FreshVarName();
  std::shared_ptr<RelExpr> WrapTermExpr(std::shared_ptr<RelTerm> term, bool wrap_in_abs);
  std::shared_ptr<RelExpr> WrapConditionExpr(std::shared_ptr<RelCondition> expr);
  std::shared_ptr<RelExpr> WrapExpr(std::shared_ptr<RelExpr> expr, bool wrap_in_abs);
  std::shared_ptr<RelApplParam> MakeVarParam(const std::string& var);
  int GetRelationArity(const std::string& id) const;

  const RelContextBuilder* container_;
  int fresh_var_counter_ = 0;
};

}  // namespace rel2sql

#endif  // REWRITER_EXPRESSION_AS_TERM_REWRITER_H

#ifndef REWRITER_BINDING_DOMAIN_REWRITER_H
#define REWRITER_BINDING_DOMAIN_REWRITER_H

#include "rel_ast/rel_ast_visitor.h"

namespace rel2sql {

/**
 * Rewriter that expands binding domains into explicit constraints:
 *
 * 1. (..., x in A, ...): F  →  (..., x, ...): F and A(x)
 *    Bindings formula: drop "in A" from the binding and conjoin the formula with A(x).
 *
 * 2. [..., x in A, ...]: E  →  [..., x, ...]: E where A(x)
 *    Bindings expression: drop "in A" from the binding and add A(x) as conditions
 *    in the condition expression.
 */
class BindingRewriter : public BaseRelVisitor {
 public:
  using BaseRelVisitor::Visit;

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelFormulaAbstraction>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelExprAbstraction>& node) override;
};

}  // namespace rel2sql

#endif  // REWRITER_BINDING_DOMAIN_REWRITER_H

#ifndef REWRITER_BINDING_DOMAIN_REWRITER_H
#define REWRITER_BINDING_DOMAIN_REWRITER_H

#include "rel_ast/rel_ast_visitor.h"

namespace rel2sql {

/**
 * Rewriter that expands binding domains into explicit constraints:
 *
 * 1. (..., x in A, ..., y in B, ...): F  →  (..., x, ..., y, ...): F and A(x) and B(y)
 *    Bindings formula: drop "in A" and "in B" from the binding and conjoin the formula with A(x) and B(y).
 *
 * 2. [..., x in A, ..., y in B, ...): E  →  [..., x, ..., y, ...]: E where A(x) and B(y)
 *    Bindings expression: drop "in A" and "in B" from the binding and add A(x) and B(y) as conditions
 *    in the condition expression.
 */
class BindingDomainRewriter : public BaseRelVisitor {
 public:
  using BaseRelVisitor::Visit;

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelBindingsFormula>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelBindingsExpr>& node) override;
};

}  // namespace rel2sql

#endif  // REWRITER_BINDING_DOMAIN_REWRITER_H

#ifndef PREPROCESSING_TERM_POLYNOMIAL_VISITOR_REL_H
#define PREPROCESSING_TERM_POLYNOMIAL_VISITOR_REL_H

#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_ast_visitor.h"

namespace rel2sql {

/**
 * Visitor that computes an affine model a * x + b for numerical term
 * expressions involving at most one variable. Non-linear, multi-variable,
 * or genuinely rational terms are marked as invalid by setting
 * RelNode::term_linear_invalid.
 */
class TermPolynomialVisitor : public BaseRelVisitor {
 public:
  using BaseRelVisitor::Visit;
  TermPolynomialVisitor() = default;

  std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelNumTerm>& node) override;
  std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelIDTerm>& node) override;
  std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelOpTerm>& node) override;
  std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelParenthesisTerm>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelTermExpr>& node) override;
};

}  // namespace rel2sql

#endif  // PREPROCESSING_TERM_POLYNOMIAL_VISITOR_REL_H

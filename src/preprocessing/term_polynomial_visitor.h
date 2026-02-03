#ifndef TERM_POLYNOMIAL_VISITOR_H
#define TERM_POLYNOMIAL_VISITOR_H

#include <antlr4-runtime.h>

#include "preprocessing/base_visitor.h"
#include "rel_ast/extended_ast.h"

namespace rel2sql {

/**
 * Visitor that computes an affine model a * x + b for numerical term
 * expressions involving at most one variable. Non-linear, multi-variable,
 * or genuinely rational terms are marked as invalid by setting
 * RelASTNode::term_linear_invalid.
 */
class TermPolynomialVisitor : public BaseVisitor {
 public:
  explicit TermPolynomialVisitor(std::shared_ptr<RelAST> ast);

  // Term branches
  std::any visitNumTerm(psr::NumTermContext* ctx) override;
  std::any visitIDTerm(psr::IDTermContext* ctx) override;
  std::any visitOpTerm(psr::OpTermContext* ctx) override;
  std::any visitParenthesisTerm(psr::ParenthesisTermContext* ctx) override;

  // Expression wrapper
  std::any visitTermExpr(psr::TermExprContext* ctx) override;
};

}  // namespace rel2sql

#endif  // TERM_POLYNOMIAL_VISITOR_H

#ifndef REWRITER_BASE_REWRITER_H
#define REWRITER_BASE_REWRITER_H

#include <memory>

#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_ast_visitor.h"

namespace rel2sql {

/**
 * Visitor for the Rel AST that supports rewriting: after visiting a child,
 * a subclass can set a replacement node, and the base class replaces the
 * pointer so the tree is transformed in place.
 *
 * Use by overriding Visit() for the node types you want to rewrite. To replace
 * a node, call SetExprReplacement / SetFormulaReplacement / SetTermReplacement
 * (or SetAbstractionReplacement) before returning. The parent will then substitute
 * that node for the child it just visited.
 *
 * Default Visit() implementations recurse into all children and apply any
 * replacement set by the child's visit. Subclasses can call the base Visit()
 * first, then optionally set a replacement for the current node.
 */
class BaseRelRewriter : public RelASTVisitor {
 public:
  ~BaseRelRewriter() override = default;

  // Replacement slots: set in overridden Visit(), consumed by parent
  void SetExprReplacement(std::shared_ptr<RelExpr> replacement);
  std::shared_ptr<RelExpr> TakeExprReplacement();

  void SetFormulaReplacement(std::shared_ptr<RelFormula> replacement);
  std::shared_ptr<RelFormula> TakeFormulaReplacement();

  void SetTermReplacement(std::shared_ptr<RelTerm> replacement);
  std::shared_ptr<RelTerm> TakeTermReplacement();

  void SetAbstractionReplacement(std::shared_ptr<RelAbstraction> replacement);
  std::shared_ptr<RelAbstraction> TakeAbstractionReplacement();

  // Helpers: visit child and replace pointer if a replacement was set
  void RewriteExpr(std::shared_ptr<RelExpr>& expr);
  void RewriteFormula(std::shared_ptr<RelFormula>& formula);
  void RewriteTerm(std::shared_ptr<RelTerm>& term);
  void RewriteAbstraction(std::shared_ptr<RelAbstraction>& abs);

  // Program / def / abstraction
  void Visit(RelProgram& node) override;
  void Visit(RelDef& node) override;
  void Visit(RelAbstraction& node) override;

  // Expressions
  void Visit(RelLitExpr& node) override;
  void Visit(RelTermExpr& node) override;
  void Visit(RelProductExpr& node) override;
  void Visit(RelConditionExpr& node) override;
  void Visit(RelAbstractionExpr& node) override;
  void Visit(RelFormulaExpr& node) override;
  void Visit(RelBindingsExpr& node) override;
  void Visit(RelBindingsFormula& node) override;
  void Visit(RelPartialAppl& node) override;

  // Formulas
  void Visit(RelFormulaBool& node) override;
  void Visit(RelFullAppl& node) override;
  void Visit(RelQuantification& node) override;
  void Visit(RelParen& node) override;
  void Visit(RelComparison& node) override;
  void Visit(RelUnOp& node) override;
  void Visit(RelBinOp& node) override;

  // Terms
  void Visit(RelIDTerm& node) override;
  void Visit(RelNumTerm& node) override;
  void Visit(RelOpTerm& node) override;
  void Visit(RelParenthesisTerm& node) override;

  // Literal (no children to rewrite)
  void Visit(RelLiteral& node) override;

 private:
  std::shared_ptr<RelExpr> expr_replacement_;
  std::shared_ptr<RelFormula> formula_replacement_;
  std::shared_ptr<RelTerm> term_replacement_;
  std::shared_ptr<RelAbstraction> abstraction_replacement_;
};

}  // namespace rel2sql

#endif  // REWRITER_BASE_REWRITER_H

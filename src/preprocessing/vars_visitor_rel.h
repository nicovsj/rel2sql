#ifndef PREPROCESSING_VARS_VISITOR_REL_H
#define PREPROCESSING_VARS_VISITOR_REL_H

#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_ast_container.h"
#include "rel_ast/rel_ast_visitor.h"

namespace rel2sql {

class VariablesVisitorRel : public RelASTVisitor {
 public:
  explicit VariablesVisitorRel(RelASTContainer* container) : container_(container) {}

  void Visit(RelProgram& node) override;
  void Visit(RelDef& node) override;
  void Visit(RelAbstraction& node) override;
  void Visit(RelIDTerm& node) override;
  void Visit(RelNumTerm& node) override;
  void Visit(RelOpTerm& node) override;
  void Visit(RelParenthesisTerm& node) override;
  void Visit(RelLitExpr& node) override;
  void Visit(RelTermExpr& node) override;
  void Visit(RelProductExpr& node) override;
  void Visit(RelConditionExpr& node) override;
  void Visit(RelAbstractionExpr& node) override;
  void Visit(RelFormulaExpr& node) override;
  void Visit(RelBindingsExpr& node) override;
  void Visit(RelBindingsFormula& node) override;
  void Visit(RelPartialAppl& node) override;
  void Visit(RelFullAppl& node) override;
  void Visit(RelBinOp& node) override;
  void Visit(RelUnOp& node) override;
  void Visit(RelQuantification& node) override;
  void Visit(RelParen& node) override;
  void Visit(RelComparison& node) override;

 private:
  RelASTContainer* container_;
};

}  // namespace rel2sql

#endif  // PREPROCESSING_VARS_VISITOR_REL_H

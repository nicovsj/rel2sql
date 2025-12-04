#ifndef TREE_STRUCTURE_VISITOR_H
#define TREE_STRUCTURE_VISITOR_H

#include "base_visitor.h"

namespace rel2sql {

class TreeStructureVisitor : public BaseVisitor {
  /*
   * Visitor that builds the tree structure of ExtendedNodes by populating
   * the children vectors. This visitor should run first, before any other
   * visitors, to establish the parent-child relationships.
   */
 public:
  using psr = rel_parser::RelParser;

  TreeStructureVisitor() : BaseVisitor() {}

  explicit TreeStructureVisitor(std::shared_ptr<RelAST> ast) : BaseVisitor(ast) {}

  // Visit specific node types to populate children vectors
  std::any visitProgram(psr::ProgramContext* ctx) override;
  std::any visitRelDef(psr::RelDefContext* ctx) override;
  std::any visitRelAbs(psr::RelAbsContext* ctx) override;
  std::any visitBindingInner(psr::BindingInnerContext* ctx) override;
  std::any visitBinding(psr::BindingContext* ctx) override;
  std::any visitApplBase(psr::ApplBaseContext* ctx) override;
  std::any visitApplParams(psr::ApplParamsContext* ctx) override;
  std::any visitApplParam(psr::ApplParamContext* ctx) override;
  std::any visitProductInner(psr::ProductInnerContext* ctx) override;

  // Override all expression, formula, and term types
  std::any visitLitExpr(psr::LitExprContext* ctx) override;
  std::any visitIDExpr(psr::IDExprContext* ctx) override;
  std::any visitProductExpr(psr::ProductExprContext* ctx) override;
  std::any visitConditionExpr(psr::ConditionExprContext* ctx) override;
  std::any visitRelAbsExpr(psr::RelAbsExprContext* ctx) override;
  std::any visitFormulaExpr(psr::FormulaExprContext* ctx) override;
  std::any visitBindingsExpr(psr::BindingsExprContext* ctx) override;
  std::any visitBindingsFormula(psr::BindingsFormulaContext* ctx) override;
  std::any visitPartialAppl(psr::PartialApplContext* ctx) override;
  std::any visitFullAppl(psr::FullApplContext* ctx) override;
  std::any visitBinOp(psr::BinOpContext* ctx) override;
  std::any visitUnOp(psr::UnOpContext* ctx) override;
  std::any visitQuantification(psr::QuantificationContext* ctx) override;
  std::any visitParen(psr::ParenContext* ctx) override;
  std::any visitComparison(psr::ComparisonContext* ctx) override;
  std::any visitIDTerm(psr::IDTermContext* ctx) override;
  std::any visitNumTerm(psr::NumTermContext* ctx) override;
  std::any visitOpTerm(psr::OpTermContext* ctx) override;

 private:
  // Helper method to populate children for a context
  void PopulateChildren(antlr4::ParserRuleContext* ctx);
};

}  // namespace rel2sql

#endif  // TREE_STRUCTURE_VISITOR_H

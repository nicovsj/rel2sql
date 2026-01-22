#ifndef VARS_VISITOR_H
#define VARS_VISITOR_H

#include <antlr4-runtime.h>

#include "preprocessing/base_visitor.h"
#include "rel_ast/extended_ast.h"

namespace rel2sql {

class VariablesVisitor : public BaseVisitor {
  /*
   * Visitor that computes the free variables and variables in scope
   * for each node in the AST.
   */
 public:
  VariablesVisitor(std::shared_ptr<RelAST> ast);

  std::any visitProgram(psr::ProgramContext* ctx) override;

  std::any visitRelDef(psr::RelDefContext* ctx) override;

  std::any visitRelAbs(psr::RelAbsContext* ctx) override;

  std::any visitIDTerm(psr::IDTermContext* ctx) override;

  std::any visitOpTerm(psr::OpTermContext* ctx) override;

  // Expression branches

  std::any visitTermExpr(psr::TermExprContext* ctx) override;

  std::any visitProductExpr(psr::ProductExprContext* ctx) override;

  std::any visitConditionExpr(psr::ConditionExprContext* ctx) override;

  std::any visitRelAbsExpr(psr::RelAbsExprContext* ctx) override;

  std::any visitFormulaExpr(psr::FormulaExprContext* ctx) override;

  std::any visitBindingsExpr(psr::BindingsExprContext* ctx) override;

  std::any visitBindingsFormula(psr::BindingsFormulaContext* ctx) override;

  std::any visitPartialAppl(psr::PartialApplContext* ctx) override;

  //  Formula branches

  std::any visitFullAppl(psr::FullApplContext* ctx) override;

  std::any visitBinOp(psr::BinOpContext* ctx) override;

  std::any visitUnOp(psr::UnOpContext* ctx) override;

  std::any visitQuantification(psr::QuantificationContext* ctx) override;

  std::any visitParen(psr::ParenContext* ctx) override;

  std::any visitComparison(psr::ComparisonContext* ctx) override;

  //  Binding branches

  std::any visitBindingInner(psr::BindingInnerContext* ctx) override;

  std::any visitBinding(psr::BindingContext* ctx) override;

  std::any visitApplBase(psr::ApplBaseContext* ctx) override;

  std::any visitApplParams(psr::ApplParamsContext* ctx) override;

  std::any visitApplParam(psr::ApplParamContext* ctx) override;
};

}  // namespace rel2sql

#endif  // VARS_VISITOR_H

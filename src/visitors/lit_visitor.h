#ifndef LIT_VISITOR_H
#define LIT_VISITOR_H

#include <antlr4-runtime.h>

#include "parser/extended_ast.h"
#include "visitors/base_visitor.h"

class LiteralVisitor : public BaseVisitor {
  /*
   * Utility visitor that computes the literal values of the Rel program and
   * stores them in the LitExprContext nodes.
   */
 public:
  LiteralVisitor(std::shared_ptr<ExtendedASTData> ast_data);

  std::any visitProgram(psr::ProgramContext *ctx) override;

  std::any visitRelDef(psr::RelDefContext *ctx) override;

  std::any visitRelAbs(psr::RelAbsContext *ctx) override;

  std::any visitNumTerm(psr::NumTermContext *ctx) override;

  std::any visitOpTerm(psr::OpTermContext *ctx) override;

  // Expression branches

  std::any visitLitExpr(psr::LitExprContext *ctx) override;

  std::any visitIDExpr(psr::IDExprContext *ctx) override;

  std::any visitProductExpr(psr::ProductExprContext *ctx) override;

  std::any visitConditionExpr(psr::ConditionExprContext *ctx) override;

  std::any visitRelAbsExpr(psr::RelAbsExprContext *ctx) override;

  std::any visitFormulaExpr(psr::FormulaExprContext *ctx) override;

  std::any visitBindingsExpr(psr::BindingsExprContext *ctx) override;

  std::any visitBindingsFormula(psr::BindingsFormulaContext *ctx) override;

  std::any visitPartialAppl(psr::PartialApplContext *ctx) override;

  std::any visitProductInner(psr::ProductInnerContext *ctx) override;

  //  Formula branches

  std::any visitFullAppl(psr::FullApplContext *ctx) override;

  std::any visitBinOp(psr::BinOpContext *ctx) override;

  std::any visitUnOp(psr::UnOpContext *ctx) override;

  std::any visitQuantification(psr::QuantificationContext *ctx) override;

  std::any visitParen(psr::ParenContext *ctx) override;

  std::any visitComparison(psr::ComparisonContext *ctx) override;

  //  Binding branches

  std::any visitBindingInner(psr::BindingInnerContext *ctx) override;

  std::any visitBinding(psr::BindingContext *ctx) override;

  std::any visitApplBase(psr::ApplBaseContext *ctx) override;

  std::any visitApplParams(psr::ApplParamsContext *ctx) override;

  std::any visitApplParam(psr::ApplParamContext *ctx) override;

  // # Literal branches

  std::any visitInt(psr::IntContext *ctx) override;

  std::any visitNegInt(psr::NegIntContext *ctx) override;

  // std::any visitMetaInt(psr::MetaIntContext *ctx) override;

  std::any visitFloat(psr::FloatContext *ctx) override;

  std::any visitNegFloat(psr::NegFloatContext *ctx) override;

  // std::any visitRelName(psr::RelNameContext *ctx) override;

  // std::any visitRelNameStr(psr::RelNameStrContext *ctx) override;

  // std::any visitRelNameMstr(psr::RelNameMstrContext *ctx) override;

  std::any visitChar(psr::CharContext *ctx) override;

  std::any visitStr(psr::StrContext *ctx) override;

  // std::any visitMstr(psr::MstrContext *ctx) override;

  // std::any visitRawstr(psr::RawstrContext *ctx) override;

  // std::any visitDate(psr::DateContext *ctx) override;

  // std::any visitDatetime(psr::DatetimeContext *ctx) override;

  std::any visitBool(psr::BoolContext *ctx) override;

  // std::any visitInterpol(psr::InterpolContext *ctx) override;

  // # Numerical constant branches

  std::any visitNumInt(psr::NumIntContext *ctx) override;

  std::any visitNumNegInt(psr::NumNegIntContext *ctx) override;

  std::any visitNumFloat(psr::NumFloatContext *ctx) override;

  std::any visitNumNegFloat(psr::NumNegFloatContext *ctx) override;
};

#endif  // LIT_VISITOR_H

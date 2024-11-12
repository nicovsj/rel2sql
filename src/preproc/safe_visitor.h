#ifndef SAFE_VISITOR_H
#define SAFE_VISITOR_H

#include <antlr4-runtime.h>

#include "preproc/base_visitor.h"
#include "structs/extended_ast.h"

class SafeVisitor : public BaseVisitor {
  /*
   * Visitor that computes the safeness analysis of each formula and expr in the Rel program.
   */
 public:
  SafeVisitor(std::shared_ptr<ExtendedASTData> extended_ast);

  std::any visitProgram(psr::ProgramContext *ctx) override;

  std::any visitRelDef(psr::RelDefContext *ctx) override;

  std::any visitRelAbs(psr::RelAbsContext *ctx) override;

  // Expression branches

  std::any visitIDExpr(psr::IDExprContext *ctx) override;

  std::any visitProductExpr(psr::ProductExprContext *ctx) override;

  std::any visitConditionExpr(psr::ConditionExprContext *ctx) override;

  std::any visitRelAbsExpr(psr::RelAbsExprContext *ctx) override;

  std::any visitFormulaExpr(psr::FormulaExprContext *ctx) override;

  std::any visitBindingsExpr(psr::BindingsExprContext *ctx) override;

  std::any visitBindingsFormula(psr::BindingsFormulaContext *ctx) override;

  std::any visitPartialAppl(psr::PartialApplContext *ctx) override;

  //  Formula branches

  std::any visitFullAppl(psr::FullApplContext *ctx) override;

  std::any visitBinOp(psr::BinOpContext *ctx) override;

  std::any visitUnOp(psr::UnOpContext *ctx) override;

  std::any visitQuantification(psr::QuantificationContext *ctx) override;

  std::any visitParen(psr::ParenContext *ctx) override;

  //  Binding branches

  // std::any visitBindingInner(psr::BindingInnerContext *ctx) override;

  // std::any visitBinding(psr::BindingContext *ctx) override;

  std::any visitApplBase(psr::ApplBaseContext *ctx) override;

  // std::any visitApplParams(psr::ApplParamsContext *ctx) override;

  std::any visitApplParam(psr::ApplParamContext *ctx) override;

 private:
  std::any VisitConjunction(psr::BinOpContext *ctx);

  std::any VisitDisjunction(psr::BinOpContext *ctx);

  std::unordered_set<TupleBinding> SpecialIntersectionOfTupleBindings(
      const std::vector<std::unordered_set<TupleBinding>> &sets) const;
};

#endif  // SAFE_VISITOR_H

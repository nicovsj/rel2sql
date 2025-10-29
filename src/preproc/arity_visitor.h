#ifndef ARITY_VISITOR_H
#define ARITY_VISITOR_H

#include <antlr4-runtime.h>

#include "preproc/base_visitor.h"
#include "structs/extended_ast.h"

namespace rel2sql {

class ArityVisitor : public BaseVisitor {
  /*
   * Visitor that computes the arity of each ID in the Rel program.
   */
 public:
  ArityVisitor(std::shared_ptr<ExtendedASTData> data);

  std::any visitProgram(psr::ProgramContext* ctx) override;

  std::any visitRelDef(psr::RelDefContext* ctx) override;

  std::any visitRelAbs(psr::RelAbsContext* ctx) override;

  // Expression branches

  std::any visitLitExpr(psr::LitExprContext* ctx) override;

  std::any visitIDExpr(psr::IDExprContext* ctx) override;

  std::any visitProductExpr(psr::ProductExprContext* ctx) override;

  std::any visitConditionExpr(psr::ConditionExprContext* ctx) override;

  std::any visitRelAbsExpr(psr::RelAbsExprContext* ctx) override;

  std::any visitFormulaExpr(psr::FormulaExprContext* ctx) override;

  std::any visitBindingsExpr(psr::BindingsExprContext* ctx) override;

  std::any visitBindingsFormula(psr::BindingsFormulaContext* ctx) override;

  std::any visitPartialAppl(psr::PartialApplContext* ctx) override;

  // Formula branches

  std::any visitFullAppl(psr::FullApplContext* ctx) override;

  std::any visitBinOp(psr::BinOpContext* ctx) override;

  std::any visitUnOp(psr::UnOpContext* ctx) override;

  std::any visitQuantification(psr::QuantificationContext* ctx) override;

  std::any visitParen(psr::ParenContext* ctx) override;

  std::any visitComparison(psr::ComparisonContext* ctx) override;

  //  Binding branches

  std::any visitApplBase(psr::ApplBaseContext* ctx) override;

  std::any visitApplParams(psr::ApplParamsContext* ctx) override;
};

}  // namespace rel2sql

#endif  // ARITY_VISITOR_H

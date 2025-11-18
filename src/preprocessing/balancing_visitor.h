#ifndef BALANCING_VISITOR_H
#define BALANCING_VISITOR_H

#include <antlr4-runtime.h>

#include "preprocessing/base_visitor.h"
#include "rel_ast/extended_ast.h"

namespace rel2sql {

class BalancingVisitor : public BaseVisitor {
  /*
   * Utility visitor that computes the literal values of the Rel program and
   * stores them in the LitExprContext nodes.
   */
 public:
  explicit BalancingVisitor(std::shared_ptr<RelAST> ast);

  std::any visitProgram(psr::ProgramContext* ctx) override;

  std::any visitRelDef(psr::RelDefContext* ctx) override;

  std::any visitRelAbs(psr::RelAbsContext* ctx) override;

  // Expression branches

  std::any visitProductExpr(psr::ProductExprContext* ctx) override;

  std::any visitConditionExpr(psr::ConditionExprContext* ctx) override;

  std::any visitRelAbsExpr(psr::RelAbsExprContext* ctx) override;

  std::any visitFormulaExpr(psr::FormulaExprContext* ctx) override;

  std::any visitBindingsExpr(psr::BindingsExprContext* ctx) override;

  std::any visitBindingsFormula(psr::BindingsFormulaContext* ctx) override;

  std::any visitPartialAppl(psr::PartialApplContext* ctx) override;

  std::any visitProductInner(psr::ProductInnerContext* ctx) override;

  //  Formula branches

  std::any visitFullAppl(psr::FullApplContext* ctx) override;

  std::any visitBinOp(psr::BinOpContext* ctx) override;

  std::any visitUnOp(psr::UnOpContext* ctx) override;

  std::any visitQuantification(psr::QuantificationContext* ctx) override;

  std::any visitParen(psr::ParenContext* ctx) override;

  std::any visitApplBase(psr::ApplBaseContext* ctx) override;

  std::any visitApplParams(psr::ApplParamsContext* ctx) override;

  std::any visitApplParam(psr::ApplParamContext* ctx) override;

 private:
  void CollectComparatorFormulas(psr::FormulaContext* formula_ctx,
                                 std::vector<antlr4::ParserRuleContext*>& comparator_formulas,
                                 std::vector<antlr4::ParserRuleContext*>& other_formulas);
};

}  // namespace rel2sql

#endif  // BALANCING_VISITOR_H

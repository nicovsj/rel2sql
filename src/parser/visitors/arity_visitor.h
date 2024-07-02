#ifndef ARITY_VISITOR_H
#define ARITY_VISITOR_H

#include <antlr4-runtime.h>

#include "parser/extended_ast.h"
#include "parser/visitors/base_visitor.h"

class ArityVisitor : public BaseVisitor {
 public:
  ArityVisitor(std::shared_ptr<ExtendedASTData> data);

  std::any visitProgram(psr::ProgramContext *ctx) override;

  std::any visitRelDef(psr::RelDefContext *ctx) override;

  std::any visitRelAbs(psr::RelAbsContext *ctx) override;

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

  //  Binding branches

  std::any visitApplBase(psr::ApplBaseContext *ctx) override;

  std::any visitApplParams(psr::ApplParamsContext *ctx) override;
};

#endif  // ARITY_VISITOR_H

#ifndef SQL_VISITOR_H
#define SQL_VISITOR_H

#include <antlr4-runtime.h>
#include <gtest/gtest.h>

#include "parser/extended_ast.h"
#include "parser/visitors/base_visitor.h"
#include "sql.h"

class SQLVisitor : public BaseVisitor {
 public:
  using psr = rel_parser::PrunedCoreRelParser;

  struct NumberedContext {
    antlr4::ParserRuleContext *ctx;
    int index;
  };

  SQLVisitor(std::shared_ptr<ExtendedASTData> extended_ast);

  virtual ~SQLVisitor();

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

  //  Formula branches

  std::any visitFullAppl(psr::FullApplContext *ctx) override;

  std::any visitBinOp(psr::BinOpContext *ctx) override;

  std::any visitUnOp(psr::UnOpContext *ctx) override;

  std::any visitQuantification(psr::QuantificationContext *ctx) override;

  std::any visitParen(psr::ParenContext *ctx) override;

  //  Binding branches

  std::any visitBindingInner(psr::BindingInnerContext *ctx) override;

  std::any visitBinding(psr::BindingContext *ctx) override;

  std::any visitApplBase(psr::ApplBaseContext *ctx) override;

  std::any visitApplParams(psr::ApplParamsContext *ctx) override;

  std::any visitApplParam(psr::ApplParamContext *ctx) override;

 private:
  std::any VisitConjunction(psr::BinOpContext *ctx);

  std::any VisitDisjunction(psr::BinOpContext *ctx);

  std::any VisitExistential(psr::QuantificationContext *ctx);

  std::any VisitUniversal(psr::QuantificationContext *ctx);

  // Utility functions

  std::string GenerateTableAlias();

  std::shared_ptr<sql::ast::Condition> EqualitySpecialCondition(std::vector<antlr4::ParserRuleContext *> ctxs);

  std::vector<std::shared_ptr<sql::ast::Selectable>> SpecialVarList(std::vector<antlr4::ParserRuleContext *> ctxs);

  std::vector<std::shared_ptr<sql::ast::Condition>> FullApplicationVariableConditions(
      psr::FullApplContext *formula_ctx, std::vector<NumberedContext> var_param_ctxs,
      std::vector<NumberedContext> other_param_ctxs) const;

  std::vector<std::shared_ptr<sql::ast::Selectable>> SpecialAppliedVarList(
      psr::FullApplContext *formula_ctx, std::vector<NumberedContext> input_ctxs,
      std::vector<NumberedContext> variable_param_ctxs) const;

  std::shared_ptr<sql::ast::Expression> GetExpressionFromID(antlr4::ParserRuleContext *ctx, std::string id) const;

  int table_alias_counter_ = 0;

  std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>> table_index_;

  // Testing bindings

  FRIEND_TEST(SQLVisitorTest, EqualitySpecialCondition);
  FRIEND_TEST(SQLVisitorTest, SpecialVarList);
};

#endif  // SQL_VISITOR_H

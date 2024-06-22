#ifndef SQL_VISITOR_H
#define SQL_VISITOR_H

#include <antlr4-runtime.h>
#include <gtest/gtest.h>

#include "parser/extended_ast.h"
#include "parser/generated/PrunedCoreRelParserBaseVisitor.h"
#include "sql.h"

class SQLVisitor : public rel_parser::PrunedCoreRelParserBaseVisitor {
 public:
  virtual ~SQLVisitor();

  std::any visitProgram(rel_parser::PrunedCoreRelParser::ProgramContext *ctx) override;

  std::any visitRelDef(rel_parser::PrunedCoreRelParser::RelDefContext *ctx) override;

  std::any visitRelAbs(rel_parser::PrunedCoreRelParser::RelAbsContext *ctx) override;

  // Expression branches

  std::any visitLitExpr(rel_parser::PrunedCoreRelParser::LitExprContext *ctx) override;

  std::any visitIDExpr(rel_parser::PrunedCoreRelParser::IDExprContext *ctx) override;

  std::any visitProductExpr(rel_parser::PrunedCoreRelParser::ProductExprContext *ctx) override;

  std::any visitConditionExpr(rel_parser::PrunedCoreRelParser::ConditionExprContext *ctx) override;

  std::any visitRelAbsExpr(rel_parser::PrunedCoreRelParser::RelAbsExprContext *ctx) override;

  std::any visitFormulaExpr(rel_parser::PrunedCoreRelParser::FormulaExprContext *ctx) override;

  std::any visitBindingsExpr(rel_parser::PrunedCoreRelParser::BindingsExprContext *ctx) override;

  std::any visitBindingsFormula(rel_parser::PrunedCoreRelParser::BindingsFormulaContext *ctx) override;

  std::any visitPartialAppl(rel_parser::PrunedCoreRelParser::PartialApplContext *ctx) override;

  //  Formula branches

  std::any visitFullAppl(rel_parser::PrunedCoreRelParser::FullApplContext *ctx) override;

  std::any visitBinOp(rel_parser::PrunedCoreRelParser::BinOpContext *ctx) override;

  std::any visitUnOp(rel_parser::PrunedCoreRelParser::UnOpContext *ctx) override;

  std::any visitQuantification(rel_parser::PrunedCoreRelParser::QuantificationContext *ctx) override;

  std::any visitParen(rel_parser::PrunedCoreRelParser::ParenContext *ctx) override;

  //  Binding branches

  std::any visitBindingInner(rel_parser::PrunedCoreRelParser::BindingInnerContext *ctx) override;

  std::any visitBinding(rel_parser::PrunedCoreRelParser::BindingContext *ctx) override;

  std::any visitApplBase(rel_parser::PrunedCoreRelParser::ApplBaseContext *ctx) override;

  std::any visitApplParams(rel_parser::PrunedCoreRelParser::ApplParamsContext *ctx) override;

  std::any visitApplParam(rel_parser::PrunedCoreRelParser::ApplParamContext *ctx) override;

 private:
  std::any VisitConjunction(rel_parser::PrunedCoreRelParser::BinOpContext *ctx);

  std::any VisitDisjunction(rel_parser::PrunedCoreRelParser::BinOpContext *ctx);

  std::any VisitExistential(rel_parser::PrunedCoreRelParser::QuantificationContext *ctx);

  std::any VisitUniversal(rel_parser::PrunedCoreRelParser::QuantificationContext *ctx);

  // Utility functions

  std::string GenerateTableAlias();

  std::shared_ptr<sql::ast::Condition> EqualitySpecialCondition(
      std::unordered_map<antlr4::ParserRuleContext *, std::shared_ptr<sql::ast::Source>> input_map);

  std::vector<std::shared_ptr<sql::ast::Selectable>> SpecialVarList(
      std::unordered_map<antlr4::ParserRuleContext *, std::shared_ptr<sql::ast::Source>> input_map);

  std::vector<std::shared_ptr<sql::ast::Condition>> FullApplicationVariableConditions(
      rel_parser::PrunedCoreRelParser::FullApplContext *f_ctx,
      std::vector<std::pair<rel_parser::PrunedCoreRelParser::ApplParamContext *, int>> e_ctx) const;

  std::shared_ptr<sql::ast::Condition> FullApplicationCondition(
      rel_parser::PrunedCoreRelParser::FullApplContext *f_ctx, rel_parser::PrunedCoreRelParser::ApplParamContext *e_ctx,
      int index) const;

  int table_alias_counter_ = 0;

  ExtendedAST &extended_ast_;

  std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>> table_index_;

  // Testing bindings

  FRIEND_TEST(SQLVisitorTest, EqualitySpecialCondition);
  FRIEND_TEST(SQLVisitorTest, SpecialVarList);
};

#endif  // SQL_VISITOR_H

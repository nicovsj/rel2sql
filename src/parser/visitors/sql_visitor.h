#ifndef SQL_VISITOR_H
#define SQL_VISITOR_H

#include <antlr4-runtime.h>
#include <gtest/gtest.h>

#include "parser/extended_ast.h"
#include "parser/visitors/base_visitor.h"
#include "sql.h"

class SQLVisitor : public BaseVisitor {
  /*
   * Visitor that constructs the SQL AST from the Rel program.
   */
 public:
  using psr = rel_parser::PrunedCoreRelParser;

  struct IndexedContext {
    antlr4::ParserRuleContext *ctx;
    int index;

    bool operator<(const IndexedContext &other) const { return index < other.index; }
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

  std::string GenerateTableAlias(std::string prefix = "T");

  std::shared_ptr<sql::ast::Condition> EqualityShorthand(std::vector<antlr4::ParserRuleContext *> ctxs);

  std::vector<std::shared_ptr<sql::ast::Selectable>> VarListShorthand(std::vector<antlr4::ParserRuleContext *> ctxs);

  std::vector<std::shared_ptr<sql::ast::Condition>> FullApplicationVariableConditions(
      psr::ApplBaseContext *base_appl_ctx, const std::vector<IndexedContext> &var_param_ctxs,
      const std::unordered_map<std::string, IndexedContext> &params_by_free_vars) const;

  std::vector<std::shared_ptr<sql::ast::Selectable>> SpecialAppliedVarList(
      psr::ApplBaseContext *base_ctx, std::vector<IndexedContext> input_ctxs,
      std::vector<IndexedContext> variable_param_ctxs,
      std::unordered_map<std::string, IndexedContext> free_vars_in_non_variable_params) const;

  std::shared_ptr<sql::ast::Expression> GetExpressionFromID(antlr4::ParserRuleContext *ctx, std::string id) const;

  std::unordered_map<std::string, IndexedContext> GetFirstNonVarParamByFreeVariables(
      const std::vector<IndexedContext> &other_param_ctxs);

  std::pair<std::vector<IndexedContext>, std::vector<IndexedContext>> GetVariableAndNonVariableParams(
      psr::ApplBaseContext *base, const std::vector<psr::ApplParamContext *> &params);

  int table_alias_counter_ = 0;

  std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>> table_index_;

  // Testing bindings

  FRIEND_TEST(SQLVisitorTest, EqualitySpecialCondition);
  FRIEND_TEST(SQLVisitorTest, SpecialVarList);
};

#endif  // SQL_VISITOR_H

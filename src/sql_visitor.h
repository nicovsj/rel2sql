#ifndef SQL_VISITOR_H
#define SQL_VISITOR_H

#include <antlr4-runtime.h>
#include <gtest/gtest.h>

#include "preproc/base_visitor.h"
#include "structs/extended_ast.h"
#include "structs/sql_ast.h"

namespace rel2sql {

class SQLVisitor : public BaseVisitor {
  /*
   * Visitor that constructs the SQL AST from the Rel program.
   */
 public:
  using psr = rel_parser::PrunedCoreRelParser;

  struct IndexedContext {
    antlr4::ParserRuleContext* ctx;
    int index;

    bool operator<(const IndexedContext& other) const { return index < other.index; }
  };

  SQLVisitor(std::shared_ptr<ExtendedASTData> extended_ast);

  virtual ~SQLVisitor();

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

  //  Formula branches

  std::any visitFullAppl(psr::FullApplContext* ctx) override;

  std::any visitBinOp(psr::BinOpContext* ctx) override;

  std::any visitUnOp(psr::UnOpContext* ctx) override;

  std::any visitQuantification(psr::QuantificationContext* ctx) override;

  std::any visitParen(psr::ParenContext* ctx) override;

  std::any visitComparison(psr::ComparisonContext* ctx) override;

  //  Binding branches

  // std::any visitBindingInner(psr::BindingInnerContext *ctx) override;

  // std::any visitBinding(psr::BindingContext *ctx) override;

  std::any visitApplBase(psr::ApplBaseContext* ctx) override;

  // std::any visitApplParams(psr::ApplParamsContext *ctx) override;

  std::any visitApplParam(psr::ApplParamContext* ctx) override;

  // Term branches

  std::any visitIDTerm(psr::IDTermContext* ctx) override;

  std::any visitNumTerm(psr::NumTermContext* ctx) override;

  std::any visitOpTerm(psr::OpTermContext* ctx) override;

 private:
  std::any VisitConjunction(psr::BinOpContext* ctx);

  std::any VisitGeneralizedConjunction(const std::vector<antlr4::ParserRuleContext*>& subformulas);

  std::any VisitDisjunction(psr::BinOpContext* ctx);

  std::any VisitExistential(psr::QuantificationContext* ctx);

  std::any VisitUniversal(psr::QuantificationContext* ctx);

  std::any VisitAggregate(sql::ast::AggregateFunction function, psr::ExprContext* expr_ctx);

  std::any SpecialVisitRelAbs(psr::RelAbsContext* ctx);

  std::any SpecialVisitProductExpr(psr::ProductExprContext* ctx);

  std::any VisitConjunctionWithTerms(psr::BinOpContext* ctx);

  // Utility functions

  std::string GenerateTableAlias(std::string prefix = "T");

  std::shared_ptr<sql::ast::Source> CreateTableSource(const std::string& table_name);

  std::string GetEDBColumnName(const std::string& table_name, int index) const;

  std::string GetColumnNameFromSource(const std::shared_ptr<sql::ast::Source>& source, int index) const;

  std::shared_ptr<sql::ast::Condition> EqualityShorthand(std::vector<antlr4::ParserRuleContext*> ctxs);

  std::vector<std::shared_ptr<sql::ast::Selectable>> VarListShorthand(std::vector<antlr4::ParserRuleContext*> ctxs);

  std::vector<std::shared_ptr<sql::ast::Condition>> ApplicationVariableConditions(
      psr::ApplBaseContext* base_appl_ctx, const std::vector<IndexedContext>& var_param_ctxs,
      const std::vector<IndexedContext>& non_var_param_ctxs,
      const std::unordered_map<std::string, IndexedContext>& params_by_free_vars) const;

  std::vector<std::shared_ptr<sql::ast::Selectable>> SpecialAppliedVarList(
      psr::ApplBaseContext* base_ctx, std::vector<IndexedContext> input_ctxs,
      std::vector<IndexedContext> variable_param_ctxs,
      std::unordered_map<std::string, IndexedContext> free_vars_in_non_variable_params) const;

  std::shared_ptr<sql::ast::Expression> GetExpressionFromID(antlr4::ParserRuleContext* ctx, std::string id, bool is_top_level = false);

  std::unordered_map<std::string, IndexedContext> GetFirstNonVarParamByFreeVariables(
      const std::vector<IndexedContext>& other_param_ctxs);

  std::pair<std::vector<IndexedContext>, std::vector<IndexedContext>> GetVariableAndNonVariableParams(
      psr::ApplBaseContext* base, const std::vector<psr::ApplParamContext*>& params);

  std::unordered_set<BindingsBound> SafeFunction(psr::BindingInnerContext* binding_ctx,
                                                antlr4::ParserRuleContext* expr_ctx);

  std::unordered_map<BindingsBound, std::shared_ptr<sql::ast::Source>> ComputeBindingsCTEs(
      std::unordered_set<BindingsBound>& safe_result);

  std::vector<std::shared_ptr<sql::ast::Selectable>> ComputeBindingsOutput(
      const std::unordered_map<BindingsBound, std::shared_ptr<sql::ast::Source>>& safe_result);

  std::shared_ptr<sql::ast::Condition> BindingsEqualityShorthand(
      antlr4::ParserRuleContext* expr,
      const std::unordered_map<BindingsBound, std::shared_ptr<sql::ast::Source>>& safe_result);

  void SpecialAddSourceToFreeVariablesInTerm(
      const std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>>& free_var_sources,
      std::shared_ptr<sql::ast::Term>& comparison);

  std::shared_ptr<sql::ast::Sourceable> VisitRelAbsLogic(psr::RelAbsContext* ctx);

  std::unordered_map<std::string, int> table_alias_prefix_counter_;

  std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>> table_index_;

  std::shared_ptr<sql::ast::Sourceable> TryGetTopLevelIDSelect(psr::RelAbsContext* ctx);

  // Testing bindings

  FRIEND_TEST(SQLVisitorTest, EqualitySpecialCondition);
  FRIEND_TEST(SQLVisitorTest, SpecialVarList);
};

}  // namespace rel2sql

#endif  // SQL_VISITOR_H

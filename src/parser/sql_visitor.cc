#include "sql_visitor.h"

#include "sql.h"

using namespace rel_parser;

SQLVisitor::SQLVisitor(ExtendedAST &ast) : extended_ast_(ast) {}

SQLVisitor::~SQLVisitor() = default;

std::any SQLVisitor::visitProgram(PrunedCoreRelParser::ProgramContext *ctx) {
  /*
   * Generates an SQL query from the program.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitRelDef(PrunedCoreRelParser::RelDefContext *ctx) {
  /*
   * Generates an SQL query from the relation definition.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitRelAbs(PrunedCoreRelParser::RelAbsContext *ctx) {
  /*
   * Generates an SQL query from the relation abstraction.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitLitExpr(PrunedCoreRelParser::LitExprContext *ctx) {
  /*
   * Generates an SQL query from the literal expression.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitIDExpr(PrunedCoreRelParser::IDExprContext *ctx) {
  /*
   * Generates an SQL query from the identifier expression.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitProductExpr(PrunedCoreRelParser::ProductExprContext *ctx) {
  /*
   * Generates an SQL query from the product expression.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitConditionExpr(PrunedCoreRelParser::ConditionExprContext *ctx) {
  /*
   * Generates an SQL query from the condition expression.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitRelAbsExpr(PrunedCoreRelParser::RelAbsExprContext *ctx) {
  /*
   * Generates an SQL query from the relation abstraction expression.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitFormulaExpr(PrunedCoreRelParser::FormulaExprContext *ctx) {
  /*
   * Generates an SQL query from the formula expression.
   */
  return visit(ctx->formula());
}

std::any SQLVisitor::visitBindingsExpr(PrunedCoreRelParser::BindingsExprContext *ctx) {
  /*
   * Generates an SQL query from the bindings expression.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitBindingsFormula(PrunedCoreRelParser::BindingsFormulaContext *ctx) {
  /*
   * Generates an SQL query from the bindings formula.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitPartialAppl(PrunedCoreRelParser::PartialApplContext *ctx) {
  /*
   * Generates an SQL query from the partial application.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitFullAppl(rel_parser::PrunedCoreRelParser::FullApplContext *ctx) {
  auto ra_sql = std::any_cast<std::shared_ptr<sql::ast::SelectStatement>>(visit(ctx->applBase()));

  std::vector<PrunedCoreRelParser::ApplParamContext *> var_params, wildcard_params, other_params;

  for (auto appl : ctx->applParams()->applParam()) {
    auto appl_sql = std::any_cast<std::shared_ptr<sql::ast::SelectStatement>>(visit(appl));
    if (appl->T_UNDERSCORE())
      wildcard_params.push_back(appl);
    else
      // TODO: I think that here we need to visit the expression. However, this complicates a bit the decision
      // of the expression beeing a variable or not.
      throw std::runtime_error("Not implemented yet");
  }

  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitBinOp(PrunedCoreRelParser::BinOpContext *ctx) {
  if (ctx->K_and()) {
    return visitConjunction(ctx);
  } else if (ctx->K_or()) {
    return visitDisjunction(ctx);
  } else {
    throw std::runtime_error("Unknown binary operation");
  }
}

std::any SQLVisitor::visitUnOp(PrunedCoreRelParser::UnOpContext *ctx) {
  /*
   * Generates an SQL query from the unary operation.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitQuantification(rel_parser::PrunedCoreRelParser::QuantificationContext *ctx) {
  if (ctx->K_exists()) {
    return visitExistential(ctx);
  } else if (ctx->K_forall()) {
    return visitUniversal(ctx);
  } else {
    throw std::runtime_error("Unknown quantification");
  }
}

std::any SQLVisitor::visitParen(PrunedCoreRelParser::ParenContext *ctx) {
  /*
   * Generates an SQL query from the parenthesized expression.
   */
  return visit(ctx->formula());
}

std::any SQLVisitor::visitBindingInner(PrunedCoreRelParser::BindingInnerContext *ctx) {
  /*
   * Generates an SQL query from the inner binding.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitBinding(PrunedCoreRelParser::BindingContext *ctx) {
  /*
   * Generates an SQL query from the binding.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitApplBase(PrunedCoreRelParser::ApplBaseContext *ctx) {
  /*
   * Generates an SQL query from the application base.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitApplParams(PrunedCoreRelParser::ApplParamsContext *ctx) {
  /*
   * Generates an SQL query from the application parameters.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitApplParam(PrunedCoreRelParser::ApplParamContext *ctx) {
  /*
   * Generates an SQL query from the application parameter.
   */
  throw std::runtime_error("Not implemented yet");
}

std::string SQLVisitor::GenerateTableAlias() {
  /*
   * Generates a table alias.
   */
  return "T" + std::to_string(table_alias_counter_++);
}

std::any SQLVisitor::visitConjunction(PrunedCoreRelParser::BinOpContext *ctx) {
  /*
   * Generates an SQL query from the conjunction of the two formulas.
   */
  auto lhs_sql = std::any_cast<std::shared_ptr<sql::ast::SelectStatement>>(visit(ctx->lhs));
  auto rhs_sql = std::any_cast<std::shared_ptr<sql::ast::SelectStatement>>(visit(ctx->rhs));

  auto lhs_subquery = std::make_shared<sql::ast::Subquery>(lhs_sql, GenerateTableAlias());
  auto rhs_subquery = std::make_shared<sql::ast::Subquery>(rhs_sql, GenerateTableAlias());

  std::unordered_map<antlr4::ParserRuleContext *, std::shared_ptr<sql::ast::Source>> input_map = {
      {ctx->lhs, lhs_subquery}, {ctx->rhs, rhs_subquery}};

  auto condition = EqualitySpecialCondition(input_map);

  auto select_columns = SpecialVarList(input_map);

  return std::make_shared<sql::ast::SelectStatement>(
      select_columns, std::vector<std::shared_ptr<sql::ast::Source>>{lhs_subquery, rhs_subquery}, condition);
}

std::any SQLVisitor::visitDisjunction(PrunedCoreRelParser::BinOpContext *ctx) {
  /*
   * Generates an SQL query from the disjunction of the two formulas.
   */
  auto lhs_sql = std::any_cast<std::shared_ptr<sql::ast::SelectStatement>>(visit(ctx->lhs));
  auto rhs_sql = std::any_cast<std::shared_ptr<sql::ast::SelectStatement>>(visit(ctx->rhs));

  auto lhs_subquery = std::make_shared<sql::ast::Subquery>(lhs_sql, GenerateTableAlias());
  auto rhs_subquery = std::make_shared<sql::ast::Subquery>(rhs_sql, GenerateTableAlias());

  auto lhs_cols = SpecialVarList(
      std::unordered_map<antlr4::ParserRuleContext *, std::shared_ptr<sql::ast::Source>>{{ctx->lhs, lhs_subquery}});
  auto rhs_cols = SpecialVarList(
      std::unordered_map<antlr4::ParserRuleContext *, std::shared_ptr<sql::ast::Source>>{{ctx->rhs, rhs_subquery}});

  auto lhs_select = std::make_shared<sql::ast::SelectStatement>(
      lhs_cols, std::vector<std::shared_ptr<sql::ast::Source>>{lhs_subquery});
  auto rhs_select = std::make_shared<sql::ast::SelectStatement>(
      rhs_cols, std::vector<std::shared_ptr<sql::ast::Source>>{rhs_subquery});

  return std::make_shared<sql::ast::Union>(lhs_select, rhs_select);
}

std::any SQLVisitor::visitExistential(PrunedCoreRelParser::QuantificationContext *ctx) {
  /*
   * Generates an SQL query from the existence quantification.
   */

  std::vector<PrunedCoreRelParser::BindingContext *> bounded_bindings;

  std::vector<PrunedCoreRelParser::BindingContext *> bindings = ctx->bindingInner()->binding();

  auto free_vars = extended_ast_.Get(ctx).free_variables;

  for (auto binding : bindings) {
    if (binding->id_domain) {
      bounded_bindings.push_back(binding);
      auto id_domain = binding->id_domain->getText();
      if (table_index_.find(id_domain) == table_index_.end()) {
        table_index_[id_domain] = std::make_shared<sql::ast::Table>(id_domain);
      }
    }
  }

  auto sql = std::any_cast<std::shared_ptr<sql::ast::SelectStatement>>(visit(ctx->formula()));

  auto subquery = std::make_shared<sql::ast::Subquery>(sql, GenerateTableAlias());

  std::vector<std::shared_ptr<sql::ast::Selectable>> select_columns;

  for (auto var : free_vars) {
    select_columns.push_back(std::make_shared<sql::ast::Column>(var, subquery));
  }

  std::vector<std::shared_ptr<sql::ast::Source>> sources = {subquery};

  for (auto binding : bounded_bindings) {
    auto id_domain = binding->id_domain->getText();
    sources.push_back(table_index_[id_domain]);
  }

  std::vector<std::shared_ptr<sql::ast::Condition>> conditions;

  for (auto binding : bounded_bindings) {
    auto id_domain = binding->id_domain->getText();
    auto id = binding->id->getText();
    auto bound_column = std::make_shared<sql::ast::Column>("A1", table_index_[id_domain]);
    auto free_var_column = std::make_shared<sql::ast::Column>(id, subquery);
    conditions.push_back(
        std::make_shared<sql::ast::ColumnComparisonCondition>(free_var_column, sql::ast::CompOp::EQ, bound_column));
  }

  auto condition = std::make_shared<sql::ast::LogicalCondition>(conditions, sql::ast::LogicalOp::AND);

  return std::make_shared<sql::ast::SelectStatement>(select_columns, sources, condition);
}

std::any SQLVisitor::visitUniversal(PrunedCoreRelParser::QuantificationContext *ctx) {
  /*
   * Generates an SQL query from the universal quantification.
   */

  auto free_vars = extended_ast_.Get(ctx).free_variables;

  std::vector<std::shared_ptr<sql::ast::Source>> bound_domain_sources;

  for (auto binding : ctx->bindingInner()->binding()) {
    if (!binding->id_domain) {
      throw std::runtime_error("Universal quantification with no domain");
    }
    auto id_domain = binding->id_domain->getText();
    if (table_index_.find(id_domain) == table_index_.end()) {
      table_index_[id_domain] = std::make_shared<sql::ast::Table>(id_domain);
    }
    bound_domain_sources.push_back(table_index_[id_domain]);
  }

  auto sql = std::any_cast<std::shared_ptr<sql::ast::SelectStatement>>(visit(ctx->formula()));

  auto subquery = std::make_shared<sql::ast::Subquery>(sql, GenerateTableAlias());

  std::vector<std::shared_ptr<sql::ast::Selectable>> select_columns;

  for (auto var : free_vars) {
    select_columns.push_back(std::make_shared<sql::ast::Column>(var, subquery));
  }

  std::vector<std::shared_ptr<sql::ast::Source>> sources = {subquery};

  auto wildcard = std::make_shared<sql::ast::Wildcard>();

  auto inter_inner_select = std::make_shared<sql::ast::SelectStatement>(
      std::vector<std::shared_ptr<sql::ast::Selectable>>{wildcard}, sources);

  std::vector<std::shared_ptr<sql::ast::Column>> inclusion_tuple;

  for (auto col : select_columns) {
    inclusion_tuple.push_back(std::static_pointer_cast<sql::ast::Column>(col));
  }

  for (auto binding : ctx->bindingInner()->binding()) {
    auto id_domain = binding->id_domain->getText();
    auto bound_column = std::make_shared<sql::ast::Column>("A1", table_index_[id_domain]);
    inclusion_tuple.push_back(bound_column);
  }

  auto inclusion = std::make_shared<sql::ast::Inclusion>(inclusion_tuple, inter_inner_select);

  auto wildcard2 = std::make_shared<sql::ast::Wildcard>();

  auto inner_select = std::make_shared<sql::ast::SelectStatement>(
      std::vector<std::shared_ptr<sql::ast::Selectable>>{wildcard2}, bound_domain_sources, inclusion);

  auto exists = std::make_shared<sql::ast::Exists>(inner_select);

  auto not_exists = std::make_shared<sql::ast::LogicalCondition>(
      std::vector<std::shared_ptr<sql::ast::Condition>>{exists}, sql::ast::LogicalOp::NOT);

  return std::make_shared<sql::ast::SelectStatement>(select_columns, sources, not_exists);
}

std::vector<std::shared_ptr<sql::ast::Condition>> SQLVisitor::FullApplicationVariableConditions(
    rel_parser::PrunedCoreRelParser::FullApplContext *formula_ctx,
    std::vector<std::pair<rel_parser::PrunedCoreRelParser::ApplParamContext *, int>> param_ctxs) const {
  std::vector<std::shared_ptr<sql::ast::Condition>> conditions;

  std::unordered_map<std::string, std::vector<std::pair<rel_parser::PrunedCoreRelParser::ApplParamContext *, int>>>
      params_by_variable;

  std::set<std::string> formula_fre_variables = extended_ast_.Get(formula_ctx).free_variables;

  for (auto [param, index] : param_ctxs) {
    auto variable = *extended_ast_.Get(param).variables.begin();
    params_by_variable[variable].push_back({param, index});
  }

  for (auto [variable, params] : params_by_variable) {
    if (formula_fre_variables.find(variable) != formula_fre_variables.end()) {
      // Then the variable is free in the formula

    } else {
      // Then the variable is not free in the formula
    }
  }

  return conditions;
}

std::shared_ptr<sql::ast::Condition> SQLVisitor::FullApplicationCondition(
    PrunedCoreRelParser::FullApplContext *f_ctx, PrunedCoreRelParser::ApplParamContext *param_ctx, int index) const {
  /*
   * Generates an SQL query from the full application condition.
   */

  std::set<std::string> parameter_variables = extended_ast_.Get(param_ctx).variables;
  std::set<std::string> formula_free_variables = extended_ast_.Get(f_ctx).free_variables;

  if (parameter_variables.size() != 1) {
    throw std::runtime_error("Full application variable parameter with more than one variable");
  }

  std::string param_variable = *parameter_variables.begin();

  if (formula_free_variables.find(param_variable) != formula_free_variables.end()) {
    // Then the variable is free in the formula

  } else {
    // Then the variable is not free in the formula
  }
}

std::shared_ptr<sql::ast::Condition> SQLVisitor::EqualitySpecialCondition(
    std::unordered_map<antlr4::ParserRuleContext *, std::shared_ptr<sql::ast::Source>> input_map) {
  std::unordered_map<std::string, std::vector<antlr4::ParserRuleContext *>> repetition_map;
  for (auto const &[ctx, _] : input_map) {
    for (auto const &var : extended_ast_.Get(ctx).variables) {
      repetition_map[var].push_back(ctx);
    }
  }

  std::vector<std::shared_ptr<sql::ast::Condition>> conditions;

  for (auto const &[var, ctxs] : repetition_map) {
    if (ctxs.size() < 2) continue;

    for (size_t i = 0; i < ctxs.size(); i++) {
      for (size_t j = i + 1; j < ctxs.size(); j++) {
        auto lhs = std::make_shared<sql::ast::Column>(var, input_map[ctxs[i]]);
        auto rhs = std::make_shared<sql::ast::Column>(var, input_map[ctxs[j]]);
        conditions.push_back(std::make_shared<sql::ast::ColumnComparisonCondition>(lhs, sql::ast::CompOp::EQ, rhs));
      }
    }
  }

  return std::make_shared<sql::ast::LogicalCondition>(conditions, sql::ast::LogicalOp::AND);
}

std::vector<std::shared_ptr<sql::ast::Selectable>> SQLVisitor::SpecialVarList(
    std::unordered_map<antlr4::ParserRuleContext *, std::shared_ptr<sql::ast::Source>> input_map) {
  std::set<std::string> seen_vars;

  std::vector<std::shared_ptr<sql::ast::Selectable>> columns;

  for (auto const &[ctx, data] : input_map) {
    for (auto const &var : extended_ast_.Get(ctx).variables) {
      if (seen_vars.find(var) != seen_vars.end()) continue;

      columns.push_back(std::static_pointer_cast<sql::ast::Selectable>(std::make_shared<sql::ast::Column>(var, data)));
      seen_vars.insert(var);
    }
  }

  return columns;
}

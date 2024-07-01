#include "sql_visitor.h"

#include "sql.h"

SQLVisitor::SQLVisitor(std::shared_ptr<ExtendedASTData> ast) : BaseVisitor(ast) {}

SQLVisitor::~SQLVisitor() = default;

std::any SQLVisitor::visitProgram(psr::ProgramContext *ctx) {
  /*
   * Generates an SQL query from the program.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitRelDef(psr::RelDefContext *ctx) {
  /*
   * Generates an SQL query from the relation definition.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitRelAbs(psr::RelAbsContext *ctx) {
  /*
   * Generates an SQL query from the relation abstraction.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitLitExpr(psr::LitExprContext *ctx) {
  /*
   * Generates an SQL query from the literal expression.
   */

  auto extended_data = GetNode(ctx);

  if (!extended_data.constant.has_value()) {
    throw std::runtime_error("Literal expression without constant value");
  }

  auto constant = std::make_shared<sql::ast::Constant>(extended_data.constant.value());

  auto selectable = std::make_shared<sql::ast::TermSelectable>(constant, "A1");

  auto select_statement =
      std::make_shared<sql::ast::SelectStatement>(std::vector<std::shared_ptr<sql::ast::Selectable>>{selectable});

  return std::static_pointer_cast<sql::ast::Expression>(select_statement);
}

std::any SQLVisitor::visitIDExpr(psr::IDExprContext *ctx) {
  /*
   * Generates an SQL query from the identifier expression.
   */
  std::string id = ctx->T_ID()->getText();

  return GetExpressionFromID(ctx, id);
}

std::any SQLVisitor::visitProductExpr(psr::ProductExprContext *ctx) {
  /*
   * Generates an SQL query from the product expression.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitConditionExpr(psr::ConditionExprContext *ctx) {
  /*
   * Generates an SQL query from the condition expression.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitRelAbsExpr(psr::RelAbsExprContext *ctx) {
  /*
   * Generates an SQL query from the relation abstraction expression.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitFormulaExpr(psr::FormulaExprContext *ctx) {
  /*
   * Generates an SQL query from the formula expression.
   */
  return visit(ctx->formula());
}

std::any SQLVisitor::visitBindingsExpr(psr::BindingsExprContext *ctx) {
  /*
   * Generates an SQL query from the bindings expression.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitBindingsFormula(psr::BindingsFormulaContext *ctx) {
  /*
   * Generates an SQL query from the bindings formula.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitPartialAppl(psr::PartialApplContext *ctx) {
  /*
   * Generates an SQL query from the partial application.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitFullAppl(psr::FullApplContext *ctx) {
  auto text = ctx->getText();

  auto ra_sql = std::any_cast<std::shared_ptr<sql::ast::Sourceable>>(visit(ctx->applBase()));

  auto ra_source = std::make_shared<sql::ast::Source>(ra_sql, GenerateTableAlias());

  GetNode(ctx->applBase()).sql_expression = ra_source;

  auto sources = std::vector<std::shared_ptr<sql::ast::Source>>{ra_source};

  std::vector<IndexedContext> var_params, other_params = {{ctx->applBase(), 0}};

  for (int i = 0; i < ctx->applParams()->applParam().size(); i++) {
    auto appl = ctx->applParams()->applParam(i);
    if (!appl->T_UNDERSCORE()) {
      auto param_sql = std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(appl));

      if (auto variable_sql = std::dynamic_pointer_cast<sql::ast::Column>(param_sql)) {
        // Then the parameter is a variable
        GetNode(appl).sql_expression = variable_sql;
        var_params.push_back({appl, i});
      } else if (auto subquery_sql = std::dynamic_pointer_cast<sql::ast::SelectStatement>(param_sql)) {
        // Then the parameter is not a variable
        auto param_subquery = std::make_shared<sql::ast::Source>(subquery_sql, GenerateTableAlias());
        GetNode(appl).sql_expression = param_subquery;

        sources.push_back(param_subquery);

        other_params.push_back({appl, i});
      }
    }
  }

  auto min_non_variable_param_numbered_ctx_by_free_variable = GetMinimumContextByFreeVariables(other_params);

  auto conditions = FullApplicationVariableConditions(ctx->applBase(), var_params,
                                                      min_non_variable_param_numbered_ctx_by_free_variable);

  std::vector<antlr4::ParserRuleContext *> source_ctxs;
  for (auto [param, _] : other_params) {
    source_ctxs.push_back(param);
  }

  std::vector<std::shared_ptr<sql::ast::Source>> from_sources;

  for (auto &ctx : source_ctxs) {
    auto source_subquery = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(ctx).sql_expression);
    from_sources.push_back(source_subquery);
  }

  auto eq_condition = EqualitySpecialCondition(source_ctxs);

  conditions.push_back(eq_condition);

  auto condition = std::make_shared<sql::ast::LogicalCondition>(conditions, sql::ast::LogicalOp::AND);

  std::vector<IndexedContext> numbered_source_ctxs = {{ctx->applBase(), 0}};

  numbered_source_ctxs.insert(numbered_source_ctxs.end(), other_params.begin(), other_params.end());

  auto select_cols = SpecialAppliedVarList(ctx, numbered_source_ctxs, var_params,
                                           min_non_variable_param_numbered_ctx_by_free_variable);

  auto from_statement = std::make_shared<sql::ast::FromStatement>(from_sources, condition);

  auto query = std::make_shared<sql::ast::SelectStatement>(select_cols, from_statement);

  return std::static_pointer_cast<sql::ast::Expression>(query);
}

std::any SQLVisitor::visitBinOp(psr::BinOpContext *ctx) {
  if (ctx->K_and()) {
    return VisitConjunction(ctx);
  } else if (ctx->K_or()) {
    return VisitDisjunction(ctx);
  } else {
    throw std::runtime_error("Unknown binary operation");
  }
}

std::any SQLVisitor::visitUnOp(psr::UnOpContext *ctx) {
  /*
   * Generates an SQL query from the unary operation.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitQuantification(psr::QuantificationContext *ctx) {
  if (ctx->K_exists()) {
    return VisitExistential(ctx);
  } else if (ctx->K_forall()) {
    return VisitUniversal(ctx);
  } else {
    throw std::runtime_error("Unknown quantification");
  }
}

std::any SQLVisitor::visitParen(psr::ParenContext *ctx) {
  /*
   * Generates an SQL query from the parenthesized expression.
   */
  return visit(ctx->formula());
}

std::any SQLVisitor::visitBindingInner(psr::BindingInnerContext *ctx) {
  /*
   * Generates an SQL query from the inner binding.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitBinding(psr::BindingContext *ctx) {
  /*
   * Generates an SQL query from the binding.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitApplBase(psr::ApplBaseContext *ctx) {
  /*
   * Generates an SQL query from the application base.
   */
  if (ctx->T_ID()) {
    return std::dynamic_pointer_cast<sql::ast::Sourceable>(GetExpressionFromID(ctx, ctx->T_ID()->getText()));
  } else if (ctx->relAbs()) {
    throw std::runtime_error("Not implemented yet");
  }

  throw std::runtime_error("Unknown application base");
}

std::any SQLVisitor::visitApplParams(psr::ApplParamsContext *ctx) {
  /*
   * Generates an SQL query from the application parameters.
   */
  throw std::runtime_error("Not implemented yet");
}

std::any SQLVisitor::visitApplParam(psr::ApplParamContext *ctx) {
  /*
   * Generates an SQL query from the application parameter.
   */
  if (ctx->expr()) {
    auto ret = std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(ctx->expr()));
    return ret;
  }

  throw std::runtime_error("Unknown application parameter");
}

std::string SQLVisitor::GenerateTableAlias() {
  /*
   * Generates a table alias.
   */
  return "T" + std::to_string(table_alias_counter_++);
}

std::any SQLVisitor::VisitConjunction(psr::BinOpContext *ctx) {
  /*
   * Generates an SQL query from the conjunction of the two formulas.
   */
  auto lhs_sql = std::static_pointer_cast<sql::ast::Sourceable>(
      std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(ctx->lhs)));
  auto rhs_sql = std::static_pointer_cast<sql::ast::Sourceable>(
      std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(ctx->rhs)));

  auto lhs_subquery = std::make_shared<sql::ast::Source>(lhs_sql, GenerateTableAlias());
  auto rhs_subquery = std::make_shared<sql::ast::Source>(rhs_sql, GenerateTableAlias());

  GetNode(ctx->lhs).sql_expression = lhs_subquery;
  GetNode(ctx->rhs).sql_expression = rhs_subquery;

  std::vector<antlr4::ParserRuleContext *> input_ctxs = {ctx->lhs, ctx->rhs};

  auto condition = EqualitySpecialCondition(input_ctxs);

  auto select_columns = SpecialVarList(input_ctxs);

  auto from = std::make_shared<sql::ast::FromStatement>(
      std::vector<std::shared_ptr<sql::ast::Source>>{lhs_subquery, rhs_subquery}, condition);

  auto query = std::make_shared<sql::ast::SelectStatement>(select_columns, from);

  return std::static_pointer_cast<sql::ast::Expression>(query);
}

std::any SQLVisitor::VisitDisjunction(psr::BinOpContext *ctx) {
  /*
   * Generates an SQL query from the disjunction of the two formulas.
   */
  auto lhs_sql = std::static_pointer_cast<sql::ast::Sourceable>(
      std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(ctx->lhs)));
  auto rhs_sql = std::static_pointer_cast<sql::ast::Sourceable>(
      std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(ctx->rhs)));

  auto lhs_subquery = std::make_shared<sql::ast::Source>(lhs_sql, GenerateTableAlias());
  auto rhs_subquery = std::make_shared<sql::ast::Source>(rhs_sql, GenerateTableAlias());

  GetNode(ctx->lhs).sql_expression = lhs_subquery;
  GetNode(ctx->rhs).sql_expression = rhs_subquery;

  auto lhs_cols = SpecialVarList(std::vector<antlr4::ParserRuleContext *>{ctx->lhs});
  auto rhs_cols = SpecialVarList(std::vector<antlr4::ParserRuleContext *>{ctx->rhs});

  auto lhs_from =
      std::make_shared<sql::ast::FromStatement>(std::vector<std::shared_ptr<sql::ast::Source>>{lhs_subquery});

  auto rhs_from =
      std::make_shared<sql::ast::FromStatement>(std::vector<std::shared_ptr<sql::ast::Source>>{rhs_subquery});

  auto lhs_select = std::make_shared<sql::ast::SelectStatement>(lhs_cols, lhs_from);

  auto rhs_select = std::make_shared<sql::ast::SelectStatement>(rhs_cols, rhs_from);

  auto query = std::make_shared<sql::ast::Union>(lhs_select, rhs_select);

  return std::static_pointer_cast<sql::ast::Expression>(query);
}

std::any SQLVisitor::VisitExistential(psr::QuantificationContext *ctx) {
  /*
   * Generates an SQL query from the existence quantification.
   */

  std::vector<psr::BindingContext *> bounded_bindings;

  std::vector<psr::BindingContext *> bindings = ctx->bindingInner()->binding();

  auto free_vars = GetNode(ctx).free_variables;

  for (auto binding : bindings) {
    if (binding->id_domain) {
      bounded_bindings.push_back(binding);
      auto id_domain = binding->id_domain->getText();
      if (table_index_.find(id_domain) == table_index_.end()) {
        auto table = std::make_shared<sql::ast::Table>(id_domain);
        table_index_[id_domain] = std::make_shared<sql::ast::Source>(table);
      }
    }
  }

  auto sql = std::any_cast<std::shared_ptr<sql::ast::SelectStatement>>(visit(ctx->formula()));

  auto subquery = std::make_shared<sql::ast::Source>(sql, GenerateTableAlias());

  std::vector<std::shared_ptr<sql::ast::Selectable>> select_columns;

  for (auto var : free_vars) {
    auto column = std::make_shared<sql::ast::Column>(var, subquery);
    select_columns.push_back(std::make_shared<sql::ast::TermSelectable>(column));
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
        std::make_shared<sql::ast::ComparisonCondition>(free_var_column, sql::ast::CompOp::EQ, bound_column));
  }

  auto condition = std::make_shared<sql::ast::LogicalCondition>(conditions, sql::ast::LogicalOp::AND);

  auto from = std::make_shared<sql::ast::FromStatement>(sources, condition);

  return std::make_shared<sql::ast::SelectStatement>(select_columns, from);
}

std::any SQLVisitor::VisitUniversal(psr::QuantificationContext *ctx) {
  /*
   * Generates an SQL query from the universal quantification.
   */

  auto free_vars = GetNode(ctx).free_variables;

  std::vector<std::shared_ptr<sql::ast::Source>> bound_domain_sources;

  for (auto binding : ctx->bindingInner()->binding()) {
    if (!binding->id_domain) {
      throw std::runtime_error("Universal quantification with no domain");
    }
    auto id_domain = binding->id_domain->getText();
    if (table_index_.find(id_domain) == table_index_.end()) {
      auto table = std::make_shared<sql::ast::Table>(id_domain);
      table_index_[id_domain] = std::make_shared<sql::ast::Source>(table);
    }
    bound_domain_sources.push_back(table_index_[id_domain]);
  }

  auto sql = std::any_cast<std::shared_ptr<sql::ast::SelectStatement>>(visit(ctx->formula()));

  auto subquery = std::make_shared<sql::ast::Source>(sql, GenerateTableAlias());

  std::vector<std::shared_ptr<sql::ast::Term>> columns;
  std::vector<std::shared_ptr<sql::ast::Selectable>> select_columns;

  for (auto var : free_vars) {
    auto column = std::make_shared<sql::ast::Column>(var, subquery);
    columns.push_back(column);
    select_columns.push_back(std::make_shared<sql::ast::TermSelectable>(column));
  }

  std::vector<std::shared_ptr<sql::ast::Source>> sources = {subquery};

  auto wildcard = std::make_shared<sql::ast::Wildcard>();

  auto inter_inner_from = std::make_shared<sql::ast::FromStatement>(sources);

  auto inter_inner_select = std::make_shared<sql::ast::SelectStatement>(
      std::vector<std::shared_ptr<sql::ast::Selectable>>{wildcard}, inter_inner_from);

  std::vector<std::shared_ptr<sql::ast::Column>> inclusion_tuple;

  for (auto col : columns) {
    inclusion_tuple.push_back(std::static_pointer_cast<sql::ast::Column>(col));
  }

  for (auto binding : ctx->bindingInner()->binding()) {
    auto id_domain = binding->id_domain->getText();
    auto bound_column = std::make_shared<sql::ast::Column>("A1", table_index_[id_domain]);
    inclusion_tuple.push_back(bound_column);
  }

  auto inclusion = std::make_shared<sql::ast::Inclusion>(inclusion_tuple, inter_inner_select);

  auto wildcard2 = std::make_shared<sql::ast::Wildcard>();

  auto inner_from = std::make_shared<sql::ast::FromStatement>(bound_domain_sources, inclusion);

  auto inner_select = std::make_shared<sql::ast::SelectStatement>(
      std::vector<std::shared_ptr<sql::ast::Selectable>>{wildcard2}, inner_from);

  auto exists = std::make_shared<sql::ast::Exists>(inner_select);

  auto not_exists = std::make_shared<sql::ast::LogicalCondition>(
      std::vector<std::shared_ptr<sql::ast::Condition>>{exists}, sql::ast::LogicalOp::NOT);

  auto outer_from = std::make_shared<sql::ast::FromStatement>(sources, not_exists);

  return std::make_shared<sql::ast::SelectStatement>(select_columns, outer_from);
}

std::vector<std::shared_ptr<sql::ast::Condition>> SQLVisitor::FullApplicationVariableConditions(
    psr::ApplBaseContext *base_appl_ctx, std::vector<IndexedContext> variable_param_ctxs,
    std::unordered_map<std::string, IndexedContext> mini_non_variable_param_numbered_ctx_by_free_variable) const {
  std::vector<std::shared_ptr<sql::ast::Condition>> conditions;

  // Map the singled-out variables that are parameters to the set of parameter contexts
  // where they are appearing. The set is ordered by the index of the parameter context.
  std::unordered_map<std::string, std::set<IndexedContext>> params_by_variable;

  for (auto numbered_ctx : variable_param_ctxs) {
    auto variable = *(GetNode(numbered_ctx.ctx).variables.begin());
    params_by_variable[variable].insert(numbered_ctx);
  }

  std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>> seen_vars;

  auto ra_subquery = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(base_appl_ctx).sql_expression);

  for (auto [explicit_variable, ctxs_that_are_the_variable] : params_by_variable) {
    auto found = mini_non_variable_param_numbered_ctx_by_free_variable.find(explicit_variable);
    if (found != mini_non_variable_param_numbered_ctx_by_free_variable.end()) {
      // Then the variable is also in one of the sub queries, and we need to equate the respective columns
      auto found_numbered_ctx = found->second;

      auto min_sql = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(found_numbered_ctx.ctx).sql_expression);

      auto lhs = std::make_shared<sql::ast::Column>(explicit_variable, min_sql);

      // The ctxs_that_are_the_variable set is ordered by the index of the parameter context
      // so we can just take the first one to do the equality
      auto min_parameter_variable_index = ctxs_that_are_the_variable.begin()->index;
      auto rhs = std::make_shared<sql::ast::Column>(fmt::format("A{}", min_parameter_variable_index), ra_subquery);

      conditions.push_back(std::make_shared<sql::ast::ComparisonCondition>(lhs, sql::ast::CompOp::EQ, rhs));
    }
    if (ctxs_that_are_the_variable.size() > 1) {
      // Then the variable is repeated as a parameter multiple times, and we need to equate those columns
      for (auto it1 = ctxs_that_are_the_variable.begin(); it1 != ctxs_that_are_the_variable.end(); it1++) {
        for (auto it2 = std::next(it1); it2 != ctxs_that_are_the_variable.end(); it2++) {
          auto lhs = std::make_shared<sql::ast::Column>(fmt::format("A{}", it1->index), ra_subquery);
          auto rhs = std::make_shared<sql::ast::Column>(fmt::format("A{}", it2->index), ra_subquery);
          conditions.push_back(std::make_shared<sql::ast::ComparisonCondition>(lhs, sql::ast::CompOp::EQ, rhs));
        }
      }
    }
  }

  return conditions;
}

std::shared_ptr<sql::ast::Condition> SQLVisitor::EqualitySpecialCondition(
    std::vector<antlr4::ParserRuleContext *> input_ctxs) {
  /*
   * Generates a condition that equates all the repeated variables in the input map. This is the EQ function
   * in the paper.
   */

  std::unordered_map<std::string, std::vector<antlr4::ParserRuleContext *>> repetition_map;

  for (auto const &ctx : input_ctxs) {
    for (auto const &var : GetNode(ctx).variables) {
      repetition_map[var].push_back(ctx);
    }
  }

  std::vector<std::shared_ptr<sql::ast::Condition>> conditions;

  for (auto const &[var, ctxs] : repetition_map) {
    if (ctxs.size() < 2) continue;

    for (size_t i = 0; i < ctxs.size(); i++) {
      for (size_t j = i + 1; j < ctxs.size(); j++) {
        auto lhs_source = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(ctxs[i]).sql_expression);
        auto rhs_source = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(ctxs[j]).sql_expression);

        auto lhs = std::make_shared<sql::ast::Column>(var, lhs_source);
        auto rhs = std::make_shared<sql::ast::Column>(var, rhs_source);

        conditions.push_back(std::make_shared<sql::ast::ComparisonCondition>(lhs, sql::ast::CompOp::EQ, rhs));
      }
    }
  }

  return std::make_shared<sql::ast::LogicalCondition>(conditions, sql::ast::LogicalOp::AND);
}

std::vector<std::shared_ptr<sql::ast::Selectable>> SQLVisitor::SpecialVarList(
    std::vector<antlr4::ParserRuleContext *> input_ctxs) {
  std::unordered_set<std::string> seen_vars;

  std::vector<std::shared_ptr<sql::ast::Selectable>> columns;

  for (auto const &ctx : input_ctxs) {
    for (auto const &var : GetNode(ctx).variables) {
      if (seen_vars.find(var) != seen_vars.end()) continue;

      auto source = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(ctx).sql_expression);

      auto column = std::make_shared<sql::ast::Column>(var, source);
      auto selectable = std::make_shared<sql::ast::TermSelectable>(column);

      columns.push_back(selectable);
      seen_vars.insert(var);
    }
  }

  return columns;
}

std::vector<std::shared_ptr<sql::ast::Selectable>> SQLVisitor::SpecialAppliedVarList(
    psr::FullApplContext *formula_ctx, std::vector<IndexedContext> input_ctxs,
    std::vector<IndexedContext> variable_param_ctxs,
    std::unordered_map<std::string, IndexedContext> free_vars_in_non_variable_params) const {
  std::unordered_set<std::string> seen_vars;

  std::vector<std::shared_ptr<sql::ast::Selectable>> columns;

  std::unordered_map<int, std::string> bound_variable_ctxs_by_index;

  // Remove variable parameters that are free in the formula
  variable_param_ctxs.erase(
      std::remove_if(variable_param_ctxs.begin(), variable_param_ctxs.end(),
                     [this, &free_vars_in_non_variable_params](auto const &ctx) {
                       return free_vars_in_non_variable_params.find(*GetNode(ctx.ctx).variables.begin()) !=
                              free_vars_in_non_variable_params.end();
                     }),
      variable_param_ctxs.end());

  // Map the non-free (bound) variable parameter indexes to the variable names
  for (auto const &[ctx, index] : variable_param_ctxs) {
    auto variables = GetNode(ctx).variables;

    if (variables.size() != 1) {
      throw std::runtime_error("Variable parameter with more than one variable");
    }
    auto variable = *variables.begin();

    bound_variable_ctxs_by_index[index] = variable;
  }

  // Zip both input_ctxs and final_ctxs
  std::vector<IndexedContext> final_ctxs;

  final_ctxs.insert(final_ctxs.end(), variable_param_ctxs.begin(), variable_param_ctxs.end());
  final_ctxs.insert(final_ctxs.end(), input_ctxs.begin(), input_ctxs.end());

  std::sort(final_ctxs.begin(), final_ctxs.end(), [](auto const &a, auto const &b) { return a.index < b.index; });

  auto ra_subquery = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(formula_ctx->applBase()).sql_expression);

  for (auto const &[ctx, index] : final_ctxs) {
    for (auto const &var : GetNode(ctx).variables) {
      if (seen_vars.find(var) != seen_vars.end()) continue;

      std::shared_ptr<sql::ast::Selectable> selectable;

      if (auto found = bound_variable_ctxs_by_index.find(index); found != bound_variable_ctxs_by_index.end()) {
        auto bound_var = found->second;

        auto column = std::make_shared<sql::ast::Column>(fmt::format("A{}", index), ra_subquery);
        selectable = std::make_shared<sql::ast::TermSelectable>(column, bound_var);
      } else {
        auto subquery = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(ctx).sql_expression);

        auto column = std::make_shared<sql::ast::Column>(var, subquery);
        selectable = std::make_shared<sql::ast::TermSelectable>(column);
      }

      columns.push_back(selectable);
      seen_vars.insert(var);
    }
  }

  return columns;
}

std::shared_ptr<sql::ast::Expression> SQLVisitor::GetExpressionFromID(antlr4::ParserRuleContext *ctx,
                                                                      std::string id) const {
  auto node = GetNode(ctx);

  // Check if it is a variable
  if (node.variables.find(id) != node.variables.end()) {
    return std::make_shared<sql::ast::Column>(id);
  }

  return std::make_shared<sql::ast::Table>(id);
}

std::unordered_map<std::string, SQLVisitor::IndexedContext> SQLVisitor::GetMinimumContextByFreeVariables(
    const std::vector<IndexedContext> &other_param_ctxs) {
  std::unordered_map<std::string, IndexedContext> minimum_context_by_free_variable;

  for (auto indexed_ctx : other_param_ctxs) {
    for (auto var : GetNode(indexed_ctx.ctx).free_variables) {
      if (minimum_context_by_free_variable.find(var) == minimum_context_by_free_variable.end()) {
        minimum_context_by_free_variable[var] = indexed_ctx;
      } else if (indexed_ctx.index < minimum_context_by_free_variable[var].index) {
        minimum_context_by_free_variable[var] = indexed_ctx;
      }
    }
  }

  return minimum_context_by_free_variable;
}

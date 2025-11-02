#include "sql_visitor.h"

#include "exceptions.h"
#include "structs/sql_ast.h"

namespace rel2sql {

SQLVisitor::SQLVisitor(std::shared_ptr<ExtendedASTData> ast) : BaseVisitor(ast) {}

SQLVisitor::~SQLVisitor() = default;

std::shared_ptr<sql::ast::Sourceable> SQLVisitor::TryGetTopLevelIDSelect(psr::RelAbsContext* ctx) {
  if (!ctx) return nullptr;
  if (ctx->expr().size() != 1) return nullptr;

  auto single_expr_ctx = ctx->expr()[0];
  auto id_expr_ctx = dynamic_cast<psr::IDExprContext*>(single_expr_ctx);
  if (!id_expr_ctx) return nullptr;

  auto expr = GetExpressionFromID(id_expr_ctx, id_expr_ctx->T_ID()->getText(), true);
  return std::dynamic_pointer_cast<sql::ast::Sourceable>(expr);
}

std::any SQLVisitor::visitProgram(psr::ProgramContext* ctx) {
  /*
   * Generates an SQL query from the program.
   */

  std::vector<std::shared_ptr<sql::ast::Expression>> exprs;

  for (auto& child_ctx : ctx->relDef()) {
    if (GetNode(child_ctx).disabled) {
      continue;
    }
    auto expr = std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(child_ctx));
    exprs.push_back(expr);
  }

  auto query = std::make_shared<sql::ast::MultipleStatements>(exprs);

  return std::static_pointer_cast<sql::ast::Expression>(query);
}

std::any SQLVisitor::visitRelDef(psr::RelDefContext* ctx) {
  /*
   * Generates an SQL query from the relation definition.
   */

  auto body_ctx = ctx->relAbs();

  auto special_child = TryGetTopLevelIDSelect(body_ctx);
  std::shared_ptr<sql::ast::Sourceable> child_sql;
  if (special_child) {
    child_sql = special_child;
  } else {
    child_sql = std::dynamic_pointer_cast<sql::ast::Sourceable>(
        std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(ctx->relAbs())));
  }

  std::string def_id = ctx->T_ID()->getText();

  if (def_id != "output") {
    auto view = std::make_shared<sql::ast::View>(child_sql, def_id);
    return std::static_pointer_cast<sql::ast::Expression>(view);
  }

  return std::static_pointer_cast<sql::ast::Expression>(child_sql);
}

std::any SQLVisitor::visitRelAbs(psr::RelAbsContext* ctx) {
  /*
   * Generates an SQL query from the relation abstraction.
   */

  if (GetNode(ctx).has_only_literal_values) {
    return SpecialVisitRelAbs(ctx);
  }

  auto query = VisitRelAbsLogic(ctx);

  // If there is only one definition, we can return the query directly
  if (GetNode(ctx).multiple_defs.empty()) {
    return std::static_pointer_cast<sql::ast::Expression>(query);
  }

  // We do have multiple definitions for this relation abstraction
  std::vector<std::shared_ptr<sql::ast::Sourceable>> queries;

  queries.push_back(query);

  for (auto& additional_ctx : GetNode(ctx).multiple_defs) {
    auto def_ctx = dynamic_cast<psr::RelDefContext*>(additional_ctx);
    if (!def_ctx) {
      throw std::runtime_error("Invalid multiple definition in relation abstraction");
    }
    queries.push_back(VisitRelAbsLogic(def_ctx->relAbs()));
  }

  auto union_query = std::make_shared<sql::ast::Union>(queries);

  return std::static_pointer_cast<sql::ast::Expression>(union_query);
}

std::shared_ptr<sql::ast::Sourceable> SQLVisitor::VisitRelAbsLogic(psr::RelAbsContext* ctx) {
  std::vector<std::shared_ptr<sql::ast::Source>> from_sources;
  std::vector<std::vector<sql::ast::constant_t>> values;

  auto expr_ctxs = ctx->expr();

  if (expr_ctxs.size() < 1) {
    throw std::runtime_error("Relation abstraction with no member");
  }

  auto first_ctx = expr_ctxs[0];

  auto first_sql = std::dynamic_pointer_cast<sql::ast::Sourceable>(
      std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(first_ctx)));
  GetNode(first_ctx).sql_expression = first_sql;

  if (expr_ctxs.size() == 1) {  // Single member relation abstraction
    return first_sql;
  }

  from_sources.push_back(std::make_shared<sql::ast::Source>(first_sql, GenerateTableAlias()));
  values.push_back({1});

  int arity = GetNode(first_ctx).arity;

  for (int i = 1; i < expr_ctxs.size(); i++) {
    auto child_ctx = expr_ctxs[i];

    if (GetNode(child_ctx).arity != arity) {
      throw std::runtime_error("Inconsistent arity in relation abstraction");
    }

    auto child_sql = std::dynamic_pointer_cast<sql::ast::Sourceable>(
        std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(child_ctx)));
    GetNode(child_ctx).sql_expression = child_sql;

    from_sources.push_back(std::make_shared<sql::ast::Source>(child_sql, GenerateTableAlias()));
    values.push_back({i + 1});
  }

  std::vector<antlr4::ParserRuleContext*> source_ctxs{expr_ctxs.begin(), expr_ctxs.end()};

  auto condition = EqualityShorthand(source_ctxs);

  auto selects = VarListShorthand(source_ctxs);

  // Define the VALUES expression used in the FROM clause
  auto values_expr = std::make_shared<sql::ast::Values>(values);
  auto values_alias =
      std::make_shared<sql::ast::AliasStatement>(GenerateTableAlias("Ind"), std::vector<std::string>{"I"});
  auto values_source = std::make_shared<sql::ast::Source>(values_expr, values_alias);
  from_sources.push_back(values_source);
  auto values_col = std::make_shared<sql::ast::Column>("I", values_source);

  // Define every CASE WHEN in the SELECT clause
  for (int i = 0; i < arity; i++) {
    std::vector<std::pair<std::shared_ptr<sql::ast::Condition>, std::shared_ptr<sql::ast::Term>>> cases;
    for (int j = 0; j < ctx->expr().size(); j++) {
      auto column = std::make_shared<sql::ast::Column>(fmt::format("A{}", i + 1), from_sources[j]);
      auto comparison = std::make_shared<sql::ast::ComparisonCondition>(values_col, sql::ast::CompOp::EQ, j + 1);
      cases.push_back({comparison, column});
    }
    auto case_when = std::make_shared<sql::ast::CaseWhen>(cases);
    auto term_selectable = std::make_shared<sql::ast::TermSelectable>(case_when, fmt::format("A{}", i + 1));
    selects.push_back(term_selectable);
  }

  auto from_statement = std::make_shared<sql::ast::FromStatement>(from_sources, condition);

  return std::make_shared<sql::ast::SelectStatement>(selects, from_statement);
}

std::any SQLVisitor::visitLitExpr(psr::LitExprContext* ctx) {
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

std::any SQLVisitor::visitIDExpr(psr::IDExprContext* ctx) {
  /*
   * Generates an SQL query from the identifier expression.
   */
  std::string id = ctx->T_ID()->getText();

  return GetExpressionFromID(ctx, id);
}

std::any SQLVisitor::visitProductExpr(psr::ProductExprContext* ctx) {
  /*
   * Generates an SQL query from the product expression.
   */

  if (GetNode(ctx).has_only_literal_values) {
    return SpecialVisitProductExpr(ctx);
  }

  std::vector<std::shared_ptr<sql::ast::Source>> from_sources;

  auto expr_ctxs = ctx->productInner()->expr();

  for (auto& child_ctx : expr_ctxs) {
    auto child_sql = std::static_pointer_cast<sql::ast::Sourceable>(
        std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(child_ctx)));
    auto child_subquery = std::make_shared<sql::ast::Source>(child_sql, GenerateTableAlias());
    GetNode(child_ctx).sql_expression = child_subquery;
    from_sources.push_back(child_subquery);
  }

  std::vector<antlr4::ParserRuleContext*> source_ctxs{expr_ctxs.begin(), expr_ctxs.end()};

  auto condition = EqualityShorthand(source_ctxs);

  auto select_columns = VarListShorthand(source_ctxs);

  for (int i = 0; i < expr_ctxs.size(); i++) {
    auto child_ctx = expr_ctxs[i];
    auto child_source = std::static_pointer_cast<sql::ast::Source>(GetNode(child_ctx).sql_expression);
    int child_arity = GetNode(child_ctx).arity;
    for (int j = 1; j <= child_arity; j++) {
      auto column = std::make_shared<sql::ast::Column>(fmt::format("A{}", j), child_source);
      select_columns.push_back(std::make_shared<sql::ast::TermSelectable>(column, fmt::format("A{}", i + 1)));
    }
  }

  auto from_statement = std::make_shared<sql::ast::FromStatement>(from_sources, condition);

  auto query = std::make_shared<sql::ast::SelectStatement>(select_columns, from_statement);

  return std::static_pointer_cast<sql::ast::Expression>(query);
}

std::any SQLVisitor::visitConditionExpr(psr::ConditionExprContext* ctx) {
  /*
   * Generates an SQL query from the condition expression.
   */
  auto lhs_sql = std::static_pointer_cast<sql::ast::Sourceable>(
      std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(ctx->lhs)));
  auto rhs_sql = std::static_pointer_cast<sql::ast::Sourceable>(
      std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(ctx->rhs)));

  auto lhs_subquery = std::make_shared<sql::ast::Source>(lhs_sql, GenerateTableAlias());
  auto rhs_subquery = std::make_shared<sql::ast::Source>(rhs_sql, GenerateTableAlias());

  GetNode(ctx->lhs).sql_expression = lhs_subquery;
  GetNode(ctx->rhs).sql_expression = rhs_subquery;

  auto condition = EqualityShorthand({ctx->lhs, ctx->rhs});

  auto select_columns = VarListShorthand({ctx->lhs, ctx->rhs});

  for (int i = 1; i <= GetNode(ctx->lhs).arity; i++) {
    auto column = std::make_shared<sql::ast::Column>(fmt::format("A{}", i), lhs_subquery);
    select_columns.push_back(std::make_shared<sql::ast::TermSelectable>(column));
  }

  auto from_statement = std::make_shared<sql::ast::FromStatement>(
      std::vector<std::shared_ptr<sql::ast::Source>>{lhs_subquery, rhs_subquery}, condition);

  auto query = std::make_shared<sql::ast::SelectStatement>(select_columns, from_statement);

  return std::static_pointer_cast<sql::ast::Expression>(query);
}

std::any SQLVisitor::visitRelAbsExpr(psr::RelAbsExprContext* ctx) {
  /*
   * Generates an SQL query from the relation abstraction expression.
   */
  auto child_sql = std::dynamic_pointer_cast<sql::ast::Sourceable>(
      std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(ctx->relAbs())));
  GetNode(ctx->relAbs()).sql_expression = child_sql;

  return std::dynamic_pointer_cast<sql::ast::Expression>(child_sql);
}

std::any SQLVisitor::visitFormulaExpr(psr::FormulaExprContext* ctx) {
  /*
   * Generates an SQL query from the formula expression.
   */
  return visit(ctx->formula());
}

std::any SQLVisitor::visitBindingsExpr(psr::BindingsExprContext* ctx) {
  /*
   * Generates an SQL query from the bindings expression.
   */

  auto expr_sql = std::dynamic_pointer_cast<sql::ast::Sourceable>(
      std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(ctx->expr())));

  auto expr_source = std::make_shared<sql::ast::Source>(expr_sql, GenerateTableAlias());

  GetNode(ctx->expr()).sql_expression = expr_source;

  auto free_variables = GetNode(ctx).free_variables;

  auto safe_result = SafeFunction(ctx->bindingInner(), ctx->expr());

  auto cte_map = ComputeBindingsCTEs(safe_result);

  auto select_columns = VarListShorthand({ctx});

  auto binding_output = ComputeBindingsOutput(cte_map);

  select_columns.insert(select_columns.end(), binding_output.begin(), binding_output.end());

  int expr_arity = GetNode(ctx->expr()).arity;

  for (int i = 1; i <= expr_arity; i++) {
    auto column = std::make_shared<sql::ast::Column>(fmt::format("A{}", i), expr_source);
    select_columns.push_back(
        std::make_shared<sql::ast::TermSelectable>(column, fmt::format("A{}", binding_output.size() + i)));
  }

  std::vector<std::shared_ptr<sql::ast::Source>> from_sources = {expr_source};

  std::vector<std::shared_ptr<sql::ast::Source>> ctes;

  for (auto& [_, cte] : cte_map) {
    ctes.push_back(cte);
  }

  from_sources.insert(from_sources.end(), ctes.begin(), ctes.end());

  auto condition = BindingsEqualityShorthand(ctx->expr(), cte_map);

  auto from = std::make_shared<sql::ast::FromStatement>(from_sources, condition);

  auto query = std::make_shared<sql::ast::SelectStatement>(select_columns, from, ctes);

  return std::static_pointer_cast<sql::ast::Expression>(query);
}

std::any SQLVisitor::visitBindingsFormula(psr::BindingsFormulaContext* ctx) {
  /*
   * Generates an SQL query from the bindings formula.
   */
  auto formula_sql = std::dynamic_pointer_cast<sql::ast::Sourceable>(
      std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(ctx->formula())));

  auto formula_source = std::make_shared<sql::ast::Source>(formula_sql, GenerateTableAlias());

  GetNode(ctx->formula()).sql_expression = formula_source;

  auto safe_result = SafeFunction(ctx->bindingInner(), ctx->formula());

  auto cte_map = ComputeBindingsCTEs(safe_result);

  auto select_columns = VarListShorthand({ctx});

  auto binding_output = ComputeBindingsOutput(cte_map);

  select_columns.insert(select_columns.end(), binding_output.begin(), binding_output.end());

  std::vector<std::shared_ptr<sql::ast::Source>> from_sources = {formula_source};

  std::vector<std::shared_ptr<sql::ast::Source>> ctes;

  for (auto& [_, cte] : cte_map) {
    ctes.push_back(cte);
  }

  from_sources.insert(from_sources.end(), ctes.begin(), ctes.end());

  auto condition = BindingsEqualityShorthand(ctx->formula(), cte_map);

  auto from = std::make_shared<sql::ast::FromStatement>(from_sources, condition);

  auto query = std::make_shared<sql::ast::SelectStatement>(select_columns, from, ctes);

  return std::static_pointer_cast<sql::ast::Expression>(query);
}

std::any SQLVisitor::visitPartialAppl(psr::PartialApplContext* ctx) {
  /*
   * Generates an SQL query from the partial application.
   */

  if (ctx->applBase()->T_ID() && AGGREGATE_MAP.find(ctx->applBase()->T_ID()->getText()) != AGGREGATE_MAP.end()) {
    auto text = ctx->getText();
    auto id = ctx->applBase()->T_ID()->getText();
    if (ctx->applParams()->applParam().size() != 1) {
      throw std::runtime_error("Aggregate function with wrong number of parameters");
    }
    auto expr_ctx = ctx->applParams()->applParam()[0]->expr();
    return VisitAggregate(AGGREGATE_MAP.at(id), expr_ctx);
  }

  auto ra_sql = std::any_cast<std::shared_ptr<sql::ast::Sourceable>>(visit(ctx->applBase()));

  auto ra_source = std::make_shared<sql::ast::Source>(ra_sql, GenerateTableAlias());

  GetNode(ctx->applBase()).sql_expression = ra_source;

  auto [var_params, non_var_params] = GetVariableAndNonVariableParams(ctx->applBase(), ctx->applParams()->applParam());

  auto non_var_param_by_free_vars = GetFirstNonVarParamByFreeVariables(non_var_params);

  auto conditions =
      ApplicationVariableConditions(ctx->applBase(), var_params, non_var_params, non_var_param_by_free_vars);

  std::vector<antlr4::ParserRuleContext*> source_ctxs;
  for (auto [param, _] : non_var_params) {
    source_ctxs.push_back(param);
  }

  std::vector<std::shared_ptr<sql::ast::Source>> from_sources;

  for (auto& ctx : source_ctxs) {
    auto source_subquery = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(ctx).sql_expression);
    from_sources.push_back(source_subquery);
  }

  conditions.push_back(EqualityShorthand(source_ctxs));

  auto condition = std::make_shared<sql::ast::LogicalCondition>(conditions, sql::ast::LogicalOp::AND);

  auto select_cols = SpecialAppliedVarList(ctx->applBase(), non_var_params, var_params, non_var_param_by_free_vars);

  int m = ctx->applParams()->applParam().size();

  for (int i = 1; i <= GetNode(ctx->applBase()).arity - m; i++) {
    std::string column_name = GetColumnNameFromSource(ra_source, m + i - 1);
    auto column = std::make_shared<sql::ast::Column>(column_name, ra_source);
    select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(column, fmt::format("A{}", i)));
  }

  auto from_statement = std::make_shared<sql::ast::FromStatement>(from_sources, condition);

  auto query = std::make_shared<sql::ast::SelectStatement>(select_cols, from_statement);

  return std::static_pointer_cast<sql::ast::Expression>(query);
}

std::any SQLVisitor::visitFullAppl(psr::FullApplContext* ctx) {
  /*
   * Generates an SQL query from the full application.
   */
  auto ra_sql = std::any_cast<std::shared_ptr<sql::ast::Sourceable>>(visit(ctx->applBase()));

  auto ra_source = std::make_shared<sql::ast::Source>(ra_sql, GenerateTableAlias());

  GetNode(ctx->applBase()).sql_expression = ra_source;

  auto [var_params, non_var_params] = GetVariableAndNonVariableParams(ctx->applBase(), ctx->applParams()->applParam());

  auto non_var_param_by_free_vars = GetFirstNonVarParamByFreeVariables(non_var_params);

  auto conditions =
      ApplicationVariableConditions(ctx->applBase(), var_params, non_var_params, non_var_param_by_free_vars);

  std::vector<antlr4::ParserRuleContext*> source_ctxs;
  for (auto [param, _] : non_var_params) {
    source_ctxs.push_back(param);
  }

  std::vector<std::shared_ptr<sql::ast::Source>> from_sources;

  for (auto& ctx : source_ctxs) {
    auto source_subquery = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(ctx).sql_expression);
    from_sources.push_back(source_subquery);
  }

  conditions.push_back(EqualityShorthand(source_ctxs));

  auto condition = std::make_shared<sql::ast::LogicalCondition>(conditions, sql::ast::LogicalOp::AND);

  auto select_cols = SpecialAppliedVarList(ctx->applBase(), non_var_params, var_params, non_var_param_by_free_vars);
  auto from_statement = std::make_shared<sql::ast::FromStatement>(from_sources, condition);

  auto query = std::make_shared<sql::ast::SelectStatement>(select_cols, from_statement);

  return std::static_pointer_cast<sql::ast::Expression>(query);
}

std::any SQLVisitor::visitBinOp(psr::BinOpContext* ctx) {
  if (ctx->K_and()) {
    return VisitConjunction(ctx);
  } else if (ctx->K_or()) {
    return VisitDisjunction(ctx);
  } else {
    throw SemanticException("Unknown binary operation", ErrorCode::UNKNOWN_BINARY_OPERATOR);
  }
}

std::any SQLVisitor::visitUnOp(psr::UnOpContext* ctx) {
  /*
   * Generates an SQL query from the unary operation.
   */
  throw NotImplementedException("Unary operation translation not implemented yet");
}

std::any SQLVisitor::visitQuantification(psr::QuantificationContext* ctx) {
  if (ctx->K_exists()) {
    return VisitExistential(ctx);
  } else if (ctx->K_forall()) {
    return VisitUniversal(ctx);
  } else {
    throw SemanticException("Unknown quantification", ErrorCode::UNKNOWN_QUANTIFICATION);
  }
}

std::any SQLVisitor::visitParen(psr::ParenContext* ctx) {
  /*
   * Generates an SQL query from the parenthesized expression.
   */
  return visit(ctx->formula());
}

std::any SQLVisitor::visitComparison(psr::ComparisonContext* ctx) {
  /*
   * Generates an SQL query from the comparison.
   */

  std::string comparator = ctx->comparator()->getText();

  std::shared_ptr<sql::ast::Term> lhs_term = std::any_cast<std::shared_ptr<sql::ast::Term>>(visit(ctx->lhs));
  std::shared_ptr<sql::ast::Term> rhs_term = std::any_cast<std::shared_ptr<sql::ast::Term>>(visit(ctx->rhs));

  sql::ast::CompOp op;

  if (comparator == "<") {
    op = sql::ast::CompOp::LT;
  } else if (comparator == "<=") {
    op = sql::ast::CompOp::LTE;
  } else if (comparator == ">") {
    op = sql::ast::CompOp::GT;
  } else if (comparator == ">=") {
    op = sql::ast::CompOp::GTE;
  } else if (comparator == "=") {
    op = sql::ast::CompOp::EQ;
  } else if (comparator == "<>") {
    op = sql::ast::CompOp::NEQ;
  } else {
    throw std::runtime_error("Unknown comparator");
  }

  auto condition = std::make_shared<sql::ast::ComparisonCondition>(lhs_term, op, rhs_term);
  return std::static_pointer_cast<sql::ast::Expression>(condition);
}

std::any SQLVisitor::visitApplBase(psr::ApplBaseContext* ctx) {
  /*
   * Generates an SQL query from the application base.
   */
  if (ctx->T_ID()) {
    return std::dynamic_pointer_cast<sql::ast::Sourceable>(GetExpressionFromID(ctx, ctx->T_ID()->getText()));
  } else if (ctx->relAbs()) {
    return visit(ctx->relAbs());
  }

  throw std::runtime_error("Unknown application base");
}

std::any SQLVisitor::visitApplParam(psr::ApplParamContext* ctx) {
  /*
   * Generates an SQL query from the application parameter.
   */
  if (ctx->expr()) {
    auto ret = std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(ctx->expr()));
    return ret;
  }

  throw std::runtime_error("Unknown application parameter");
}

std::any SQLVisitor::visitIDTerm(psr::IDTermContext* ctx) {
  /*
   * Generates an SQL query from the identifier term.
   */
  return std::static_pointer_cast<sql::ast::Term>(std::make_shared<sql::ast::Column>(ctx->T_ID()->getText()));
}

std::any SQLVisitor::visitNumTerm(psr::NumTermContext* ctx) {
  /*
   * Generates an SQL query from the literal term.
   */

  auto node = GetNode(ctx);
  return std::static_pointer_cast<sql::ast::Term>(std::make_shared<sql::ast::Constant>(GetNode(ctx).constant.value()));
}

std::any SQLVisitor::visitOpTerm(psr::OpTermContext* ctx) {
  /*
   * Generates an SQL query from the application term.
   */

  auto lhs_term = std::any_cast<std::shared_ptr<sql::ast::Term>>(visit(ctx->lhs));
  auto rhs_term = std::any_cast<std::shared_ptr<sql::ast::Term>>(visit(ctx->rhs));

  return std::static_pointer_cast<sql::ast::Term>(
      std::make_shared<sql::ast::Operation>(lhs_term, rhs_term, ctx->operator_()->getText()));
}

std::any SQLVisitor::VisitConjunction(psr::BinOpContext* ctx) {
  /*
   * Generates an SQL query from the conjunction of the two formulas.
   */

  // There is a special case for conjunctions with terms, which are already partitioned
  // by the balancing visitor.
  if (GetNode(ctx).IsConjunctionWithTerms()) {
    return VisitConjunctionWithTerms(ctx);
  }

  // Call the generalized conjunction function with the left and right subformulas
  return VisitGeneralizedConjunction({ctx->lhs, ctx->rhs});
}

std::any SQLVisitor::VisitGeneralizedConjunction(const std::vector<antlr4::ParserRuleContext*>& subformulas) {
  /*
   * Generates an SQL query from the conjunction of N subformulas.
   */

  std::vector<std::shared_ptr<sql::ast::Source>> subqueries;
  std::vector<antlr4::ParserRuleContext*> input_ctxs;

  for (auto subformula : subformulas) {
    auto subformula_sql = std::static_pointer_cast<sql::ast::Sourceable>(
        std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(subformula)));

    auto subquery = std::make_shared<sql::ast::Source>(subformula_sql, GenerateTableAlias());

    GetNode(subformula).sql_expression = subquery;
    subqueries.push_back(subquery);
    input_ctxs.push_back(subformula);
  }

  auto condition = EqualityShorthand(input_ctxs);
  auto select_columns = VarListShorthand(input_ctxs);

  auto from = std::make_shared<sql::ast::FromStatement>(subqueries, condition);
  auto query = std::make_shared<sql::ast::SelectStatement>(select_columns, from);

  return std::static_pointer_cast<sql::ast::Expression>(query);
}

std::any SQLVisitor::VisitConjunctionWithTerms(psr::BinOpContext* ctx) {
  // We know that this subtree is a conjunction of formulas of the form:
  //
  // C1 and C2 and ... and Cn and F1 and F2 and ... and Fm,
  //
  // where each Ci is a formula that has to be translated, and each Fi is a comparison of terms.
  //
  // Conveniently, we already separated the children into comparator and non-comparator formulas
  // with the BalancingVisitor.

  std::vector<antlr4::ParserRuleContext*> other_formulas = GetNode(ctx).other_formulas;
  std::vector<antlr4::ParserRuleContext*> comparator_formulas = GetNode(ctx).comparator_formulas;

  auto select_expression = std::static_pointer_cast<sql::ast::SelectStatement>(
      std::any_cast<std::shared_ptr<sql::ast::Expression>>(VisitGeneralizedConjunction(other_formulas)));

  auto from_statement = select_expression->from.value();

  std::vector<std::shared_ptr<sql::ast::Condition>> new_conditions;

  if (from_statement->where.has_value()) {
    new_conditions.push_back(from_statement->where.value());
  }

  std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>> free_var_sources;

  for (auto source_ctx : other_formulas) {
    for (auto free_var : GetNode(source_ctx).free_variables) {
      if (free_var_sources.find(free_var) == free_var_sources.end()) {
        free_var_sources[free_var] = std::static_pointer_cast<sql::ast::Source>(GetNode(source_ctx).sql_expression);
      }
    }
  }

  for (auto comparator : comparator_formulas) {
    auto comparator_sql = std::static_pointer_cast<sql::ast::ComparisonCondition>(
        std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(comparator)));

    SpecialAddSourceToFreeVariablesInTerm(free_var_sources, comparator_sql->lhs);
    SpecialAddSourceToFreeVariablesInTerm(free_var_sources, comparator_sql->rhs);

    new_conditions.push_back(std::dynamic_pointer_cast<sql::ast::Condition>(comparator_sql));
  }

  auto new_where = std::make_shared<sql::ast::LogicalCondition>(new_conditions, sql::ast::LogicalOp::AND);

  from_statement->where = new_where;

  return std::static_pointer_cast<sql::ast::Expression>(select_expression);
}

void SQLVisitor::SpecialAddSourceToFreeVariablesInTerm(
    const std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>>& free_var_sources,
    std::shared_ptr<sql::ast::Term>& term) {
  if (auto column = std::dynamic_pointer_cast<sql::ast::Column>(term)) {
    column->source = free_var_sources.at(column->name);
  } else if (auto operation = std::dynamic_pointer_cast<sql::ast::Operation>(term)) {
    SpecialAddSourceToFreeVariablesInTerm(free_var_sources, operation->lhs);
    SpecialAddSourceToFreeVariablesInTerm(free_var_sources, operation->rhs);
  }
}

std::any SQLVisitor::VisitDisjunction(psr::BinOpContext* ctx) {
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

  auto lhs_cols = VarListShorthand(std::vector<antlr4::ParserRuleContext*>{ctx->lhs});
  auto rhs_cols = VarListShorthand(std::vector<antlr4::ParserRuleContext*>{ctx->rhs});

  auto lhs_from =
      std::make_shared<sql::ast::FromStatement>(std::vector<std::shared_ptr<sql::ast::Source>>{lhs_subquery});

  auto rhs_from =
      std::make_shared<sql::ast::FromStatement>(std::vector<std::shared_ptr<sql::ast::Source>>{rhs_subquery});

  auto lhs_select = std::make_shared<sql::ast::SelectStatement>(lhs_cols, lhs_from);

  auto rhs_select = std::make_shared<sql::ast::SelectStatement>(rhs_cols, rhs_from);

  auto query = std::make_shared<sql::ast::Union>(lhs_select, rhs_select);

  return std::static_pointer_cast<sql::ast::Expression>(query);
}

std::any SQLVisitor::VisitExistential(psr::QuantificationContext* ctx) {
  /*
   * Generates an SQL query from the existence quantification.
   */

  std::vector<psr::BindingContext*> bounded_bindings;

  std::vector<psr::BindingContext*> bindings = ctx->bindingInner()->binding();

  auto free_vars = GetNode(ctx).free_variables;

  for (auto binding : bindings) {
    if (binding->id_domain) {
      bounded_bindings.push_back(binding);
      auto id_domain = binding->id_domain->getText();
      if (table_index_.find(id_domain) == table_index_.end()) {
        table_index_[id_domain] = CreateTableSource(id_domain);
      }
    }
  }

  auto sql = std::static_pointer_cast<sql::ast::Sourceable>(
      std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(ctx->formula())));

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
    auto bound_column = std::make_shared<sql::ast::Column>(GetEDBColumnName(id_domain, 0), table_index_[id_domain]);
    auto free_var_column = std::make_shared<sql::ast::Column>(id, subquery);
    conditions.push_back(
        std::make_shared<sql::ast::ComparisonCondition>(free_var_column, sql::ast::CompOp::EQ, bound_column));
  }

  auto condition = std::make_shared<sql::ast::LogicalCondition>(conditions, sql::ast::LogicalOp::AND);

  auto from = std::make_shared<sql::ast::FromStatement>(sources, condition);

  auto query = std::make_shared<sql::ast::SelectStatement>(select_columns, from);

  return std::static_pointer_cast<sql::ast::Expression>(query);
}

std::any SQLVisitor::VisitUniversal(psr::QuantificationContext* ctx) {
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
      table_index_[id_domain] = CreateTableSource(id_domain);
    }
    bound_domain_sources.push_back(table_index_[id_domain]);
  }

  auto sql = std::static_pointer_cast<sql::ast::Sourceable>(
      std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(ctx->formula())));

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
    auto bound_column = std::make_shared<sql::ast::Column>(GetEDBColumnName(id_domain, 0), table_index_[id_domain]);
    inclusion_tuple.push_back(bound_column);
  }

  auto inclusion = std::make_shared<sql::ast::Inclusion>(inclusion_tuple, inter_inner_select, true);

  auto wildcard2 = std::make_shared<sql::ast::Wildcard>();

  auto inner_from = std::make_shared<sql::ast::FromStatement>(bound_domain_sources, inclusion);

  auto inner_select = std::make_shared<sql::ast::SelectStatement>(
      std::vector<std::shared_ptr<sql::ast::Selectable>>{wildcard2}, inner_from);

  auto exists = std::make_shared<sql::ast::Exists>(inner_select);

  auto not_exists = std::make_shared<sql::ast::LogicalCondition>(
      std::vector<std::shared_ptr<sql::ast::Condition>>{exists}, sql::ast::LogicalOp::NOT);

  auto outer_from = std::make_shared<sql::ast::FromStatement>(sources, not_exists);

  auto query = std::make_shared<sql::ast::SelectStatement>(select_columns, outer_from);

  return std::static_pointer_cast<sql::ast::Expression>(query);
}

std::any SQLVisitor::VisitAggregate(sql::ast::AggregateFunction function, psr::ExprContext* expr_ctx) {
  /*
   * Generates an SQL query from the aggregate.
   */
  auto sql = std::static_pointer_cast<sql::ast::Sourceable>(
      std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(expr_ctx)));

  auto subquery = std::make_shared<sql::ast::Source>(sql, GenerateTableAlias());

  GetNode(expr_ctx).sql_expression = subquery;

  int arity = GetNode(expr_ctx).arity;

  // Always use proper column name from the table (EDBInfo handles both custom and standard naming)
  std::string column_name;
  if (auto table = std::dynamic_pointer_cast<sql::ast::Table>(subquery->sourceable)) {
    column_name = table->GetAttributeName(arity - 1);  // Convert to 0-based index
  } else {
    column_name = fmt::format("A{}", arity);
  }
  auto column = std::make_shared<sql::ast::Column>(column_name, subquery);

  auto aggregate_column =
      std::make_shared<sql::ast::TermSelectable>(std::make_shared<sql::ast::Function>(function, column), "A1");

  auto text = expr_ctx->getText();

  auto columns = VarListShorthand({expr_ctx});

  std::shared_ptr<sql::ast::GroupBy> group_by;

  if (!columns.empty()) {
    group_by = std::make_shared<sql::ast::GroupBy>(columns);
  }

  columns.push_back(aggregate_column);

  auto from = std::make_shared<sql::ast::FromStatement>(std::vector<std::shared_ptr<sql::ast::Source>>{subquery});

  std::shared_ptr<sql::ast::SelectStatement> query;

  if (group_by) {
    query = std::make_shared<sql::ast::SelectStatement>(columns, from, group_by);
  } else {
    query = std::make_shared<sql::ast::SelectStatement>(columns, from);
  }

  return std::static_pointer_cast<sql::ast::Expression>(query);
}

std::any SQLVisitor::SpecialVisitRelAbs(psr::RelAbsContext* ctx) {
  std::vector<std::vector<sql::ast::Constant>> values;

  auto expr_ctxs = ctx->expr();

  auto& current_node = GetNode(ctx);
  for (auto more_ctx : current_node.multiple_defs) {
    auto def_ctx = dynamic_cast<psr::RelDefContext*>(more_ctx);
    if (!def_ctx) {
      throw std::runtime_error("Invalid member in relation abstraction: " + more_ctx->getText());
    }
    auto rel_abs_ctx = def_ctx->relAbs();
    auto other_expr_ctxs = rel_abs_ctx->expr();
    expr_ctxs.insert(expr_ctxs.end(), other_expr_ctxs.begin(), other_expr_ctxs.end());
  }

  if (expr_ctxs.empty()) {
    throw std::runtime_error("Relation abstraction with no member");
  }

  int arity = GetNode(expr_ctxs[0]).arity;

  for (auto expr_ctx : expr_ctxs) {
    auto& expr_node = GetNode(expr_ctx);
    if (expr_node.arity != arity) {
      throw std::runtime_error("Inconsistent arity in relation abstraction");
    }

    if (auto product_inner = dynamic_cast<psr::ProductExprContext*>(expr_ctx)) {
      auto product_inner_ctx = product_inner->productInner();
      if (product_inner_ctx) {
        std::vector<sql::ast::Constant> row;
        for (auto term_ctx : product_inner_ctx->expr()) {
          auto constant = GetNode(term_ctx).constant;
          auto sql_constant = std::make_shared<sql::ast::Constant>(constant.value());
          row.push_back(*sql_constant);
        }
        values.push_back(row);
      } else {
        throw std::runtime_error("Invalid product expression: missing productInner");
      }
    } else if (auto lit_expr_ctx = dynamic_cast<psr::LitExprContext*>(expr_ctx)) {
      auto constant = GetNode(lit_expr_ctx).constant;
      auto sql_constant = std::make_shared<sql::ast::Constant>(constant.value());
      std::vector<sql::ast::Constant> row{*sql_constant};
      values.push_back(row);
    } else {
      throw std::runtime_error("Invalid expression in relation abstraction: expected product expression");
    }
  }

  auto values_expr = std::make_shared<sql::ast::Values>(values);

  std::vector<std::string> column_names;
  for (int i = 1; i <= arity; ++i) {
    column_names.push_back(fmt::format("A{}", i));
  }

  auto alias = std::make_shared<sql::ast::AliasStatement>(GenerateTableAlias(), column_names);
  auto source = std::make_shared<sql::ast::Source>(values_expr, alias);

  auto from = std::make_shared<sql::ast::FromStatement>(std::vector<std::shared_ptr<sql::ast::Source>>{source});

  auto wildcard = std::make_shared<sql::ast::Wildcard>();
  auto select_columns = std::vector<std::shared_ptr<sql::ast::Selectable>>{wildcard};

  auto select = std::make_shared<sql::ast::SelectStatement>(select_columns, from, true);  // true for DISTINCT

  return std::static_pointer_cast<sql::ast::Expression>(select);
}

std::any SQLVisitor::SpecialVisitProductExpr(psr::ProductExprContext* ctx) {
  /*
   * Generates an SQL query from an "only literals" product expression.
   */
  auto expr_ctxs = ctx->productInner()->expr();

  std::vector<std::shared_ptr<sql::ast::Selectable>> selects;

  // All children must be constants
  for (auto& child_ctx : expr_ctxs) {
    if (!GetNode(child_ctx).constant.has_value()) {
      throw std::runtime_error("Special product expression with non-constant member");
    }
    auto constant = std::make_shared<sql::ast::Constant>(GetNode(child_ctx).constant.value());
    selects.push_back(std::make_shared<sql::ast::TermSelectable>(constant));
  }

  auto query = std::make_shared<sql::ast::SelectStatement>(selects);

  return std::static_pointer_cast<sql::ast::Expression>(query);
}

std::string SQLVisitor::GenerateTableAlias(std::string prefix) {
  /*
   * Generates a table alias.
   */
  if (table_alias_prefix_counter_.find(prefix) == table_alias_prefix_counter_.end()) {
    table_alias_prefix_counter_[prefix] = 0;
  }

  return fmt::format("{}{}", prefix, table_alias_prefix_counter_[prefix]++);
}

std::shared_ptr<sql::ast::Source> SQLVisitor::CreateTableSource(const std::string& table_name) {
  /*
   * Creates a table source with proper column names based on EDB attribute information.
   */
  std::shared_ptr<sql::ast::Table> table;

  // Always create table with attribute names from EDBInfo (whether custom or standard A1, A2, etc.)
  auto edb_info = ast_data_->GetEDBInfo(table_name);
  if (edb_info && edb_info->arity() > 0) {
    std::vector<std::string> attribute_names;
    for (int i = 0; i < edb_info->arity(); ++i) {
      attribute_names.push_back(edb_info->get_attribute_name(i));
    }
    table = std::make_shared<sql::ast::Table>(table_name, edb_info->arity(), attribute_names);
    // Create a source with an alias that includes the attribute names
    auto alias = std::make_shared<sql::ast::AliasStatement>(GenerateTableAlias());
    return std::make_shared<sql::ast::Source>(table, alias);
  } else {
    // Fallback to default table creation if EDBInfo not found
    table = std::make_shared<sql::ast::Table>(table_name, ast_data_->arity_by_id[table_name]);
    return std::make_shared<sql::ast::Source>(table);
  }
}

std::string SQLVisitor::GetEDBColumnName(const std::string& table_name, int index) const {
  /*
   * Gets the proper column name for an EDB table at the given index.
   */
  // Always use EDBInfo for attribute names (whether custom or standard A1, A2, etc.)
  auto edb_info = ast_data_->GetEDBInfo(table_name);
  if (edb_info) {
    return edb_info->get_attribute_name(index);
  } else {
    // Fallback to default naming if EDBInfo not found (shouldn't happen with EDBMap)
    return fmt::format("A{}", index + 1);
  }
}

std::string SQLVisitor::GetColumnNameFromSource(const std::shared_ptr<sql::ast::Source>& source, int index) const {
  /*
   * Gets the proper column name from a source at the given index.
   */
  if (auto table = std::dynamic_pointer_cast<sql::ast::Table>(source->sourceable)) {
    // Always use the attribute names from EDBInfo (whether custom or standard A1, A2, etc.)
    return table->GetAttributeName(index);
  } else {
    // For subqueries and other sources, use default naming
    return fmt::format("A{}", index + 1);
  }
}

std::vector<std::shared_ptr<sql::ast::Condition>> SQLVisitor::ApplicationVariableConditions(
    psr::ApplBaseContext* base_appl_ctx, const std::vector<IndexedContext>& variable_param_ctxs,
    const std::vector<IndexedContext>& non_variable_param_ctxs,
    const std::unordered_map<std::string, IndexedContext>& params_by_free_vars) const {
  std::vector<std::shared_ptr<sql::ast::Condition>> conditions;

  // Map the singled-out variables that are parameters to the set of parameter contexts
  // where they are appearing. The set is ordered by the index of the parameter context.
  std::unordered_map<std::string, std::set<IndexedContext>> params_by_variable;

  for (auto numbered_ctx : variable_param_ctxs) {
    // The first variable should be the only one
    auto variable = *(GetNode(numbered_ctx.ctx).variables.begin());
    params_by_variable[variable].insert(numbered_ctx);
  }

  std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>> seen_vars;

  auto ra_subquery = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(base_appl_ctx).sql_expression);

  for (auto [explicit_variable, variable_params] : params_by_variable) {
    auto found = params_by_free_vars.find(explicit_variable);
    if (found != params_by_free_vars.end()) {
      // Then the variable is also in one of the sub queries, and we need to equate the respective columns
      auto found_numbered_ctx = found->second;

      auto min_sql = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(found_numbered_ctx.ctx).sql_expression);

      auto lhs = std::make_shared<sql::ast::Column>(explicit_variable, min_sql);

      // The ctxs_that_are_the_variable set is ordered by the index of the parameter context
      // so we can just take the first one to do the equality
      auto min_parameter_variable_index = variable_params.begin()->index;
      // Always use proper column name from the table (EDBInfo handles both custom and standard naming)
      std::string rhs_column_name;
      if (auto table = std::dynamic_pointer_cast<sql::ast::Table>(ra_subquery->sourceable)) {
        rhs_column_name = table->GetAttributeName(min_parameter_variable_index - 1);  // Convert to 0-based index
      } else {
        rhs_column_name = fmt::format("A{}", min_parameter_variable_index);
      }
      auto rhs = std::make_shared<sql::ast::Column>(rhs_column_name, ra_subquery);

      conditions.push_back(std::make_shared<sql::ast::ComparisonCondition>(lhs, sql::ast::CompOp::EQ, rhs));
    }
    if (variable_params.size() > 1) {
      // Then the variable is repeated as a parameter multiple times, and we need to equate those columns
      for (auto it1 = variable_params.begin(), it2 = std::next(it1); it2 != variable_params.end(); it1++, it2++) {
        // Always use proper column names from the table (EDBInfo handles both custom and standard naming)
        std::string lhs_column_name, rhs_column_name;
        if (auto table = std::dynamic_pointer_cast<sql::ast::Table>(ra_subquery->sourceable)) {
          lhs_column_name = table->GetAttributeName(it1->index - 1);  // Convert to 0-based index
          rhs_column_name = table->GetAttributeName(it2->index - 1);  // Convert to 0-based index
        } else {
          lhs_column_name = fmt::format("A{}", it1->index);
          rhs_column_name = fmt::format("A{}", it2->index);
        }
        auto lhs = std::make_shared<sql::ast::Column>(lhs_column_name, ra_subquery);
        auto rhs = std::make_shared<sql::ast::Column>(rhs_column_name, ra_subquery);
        conditions.push_back(std::make_shared<sql::ast::ComparisonCondition>(lhs, sql::ast::CompOp::EQ, rhs));
      }
    }
  }

  for (int i = 1; i < non_variable_param_ctxs.size(); i++) {
    auto ctx = non_variable_param_ctxs[i].ctx;
    auto index = non_variable_param_ctxs[i].index;
    auto sq = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(ctx).sql_expression);
    auto lhs = std::make_shared<sql::ast::Column>(fmt::format("A{}", index), ra_subquery);
    auto rhs = std::make_shared<sql::ast::Column>("A1", sq);
    conditions.push_back(std::make_shared<sql::ast::ComparisonCondition>(lhs, sql::ast::CompOp::EQ, rhs));
  }

  return conditions;
}

std::shared_ptr<sql::ast::Condition> SQLVisitor::EqualityShorthand(std::vector<antlr4::ParserRuleContext*> input_ctxs) {
  /*
   * Generates a condition that equates all the repeated variables in the input map. This is the EQ function
   * in the paper.
   */

  std::unordered_map<std::string, std::vector<antlr4::ParserRuleContext*>> repetition_map;

  for (auto const& ctx : input_ctxs) {
    for (auto const& var : GetNode(ctx).variables) {
      repetition_map[var].push_back(ctx);
    }
  }

  std::vector<std::shared_ptr<sql::ast::Condition>> conditions;

  for (auto const& [var, ctxs] : repetition_map) {
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

std::vector<std::shared_ptr<sql::ast::Selectable>> SQLVisitor::VarListShorthand(
    std::vector<antlr4::ParserRuleContext*> input_ctxs) {
  std::unordered_set<std::string> seen_vars;

  std::vector<std::shared_ptr<sql::ast::Selectable>> columns;

  for (auto const& ctx : input_ctxs) {
    for (auto const& var : GetNode(ctx).free_variables) {
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
    psr::ApplBaseContext* base_ctx, std::vector<IndexedContext> input_ctxs,
    std::vector<IndexedContext> variable_param_ctxs,
    std::unordered_map<std::string, IndexedContext> free_vars_in_non_variable_params) const {
  std::unordered_set<std::string> seen_vars;

  std::vector<std::shared_ptr<sql::ast::Selectable>> columns;

  std::unordered_map<int, std::string> bound_variable_ctxs_by_index;

  // Remove variable parameters that are already part of the free variables of non-variable parameters
  variable_param_ctxs.erase(
      std::remove_if(variable_param_ctxs.begin(), variable_param_ctxs.end(),
                     [this, &free_vars_in_non_variable_params](auto const& ctx) {
                       return free_vars_in_non_variable_params.find(*GetNode(ctx.ctx).variables.begin()) !=
                              free_vars_in_non_variable_params.end();
                     }),
      variable_param_ctxs.end());

  // Map the non-free (bound) variable parameter indexes to the variable names
  for (auto const& [ctx, index] : variable_param_ctxs) {
    auto variables = GetNode(ctx).variables;

    if (variables.size() != 1) {
      throw std::runtime_error("Variable parameter with more than one variable");
    }
    auto variable = *variables.begin();

    bound_variable_ctxs_by_index[index] = variable;
  }

  // Zip both input_ctxs and final_ctxs
  std::vector<IndexedContext> final_ctxs;
  final_ctxs.reserve(variable_param_ctxs.size() + input_ctxs.size());
  final_ctxs.insert(final_ctxs.end(), variable_param_ctxs.begin(), variable_param_ctxs.end());
  final_ctxs.insert(final_ctxs.end(), input_ctxs.begin(), input_ctxs.end());

  std::sort(final_ctxs.begin(), final_ctxs.end(), [](auto const& a, auto const& b) { return a.index < b.index; });

  auto ra_subquery = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(base_ctx).sql_expression);

  for (auto const& [ctx, index] : final_ctxs) {
    for (auto const& var : GetNode(ctx).variables) {
      if (seen_vars.find(var) != seen_vars.end()) continue;

      std::shared_ptr<sql::ast::Selectable> selectable;

      if (auto found = bound_variable_ctxs_by_index.find(index); found != bound_variable_ctxs_by_index.end()) {
        auto bound_var = found->second;

        // Use proper column name from the table instead of hardcoded A{}
        std::string column_name;
        if (auto table = std::dynamic_pointer_cast<sql::ast::Table>(ra_subquery->sourceable)) {
          // The index is 1-based, but GetAttributeName expects 0-based index
          int adjusted_index = index - 1;
          column_name = table->GetAttributeName(adjusted_index);
        } else {
          column_name = fmt::format("A{}", index);
        }
        auto column = std::make_shared<sql::ast::Column>(column_name, ra_subquery);
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

std::shared_ptr<sql::ast::Expression> SQLVisitor::GetExpressionFromID(antlr4::ParserRuleContext* ctx, std::string id,
                                                                      bool is_top_level) {
  auto node = GetNode(ctx);

  // Check if it is a variable
  if (node.variables.find(id) != node.variables.end()) {
    return std::make_shared<sql::ast::Column>(id);
  }

  // Always create table with attribute names from EDBInfo (whether custom or standard A1, A2, etc.)
  auto edb_info = ast_data_->GetEDBInfo(id);
  std::shared_ptr<sql::ast::Table> table;
  if (edb_info && edb_info->arity() > 0) {
    std::vector<std::string> attribute_names;
    for (int i = 0; i < edb_info->arity(); ++i) {
      attribute_names.push_back(edb_info->get_attribute_name(i));
    }
    table = std::make_shared<sql::ast::Table>(id, edb_info->arity(), attribute_names);
  } else {
    // Fallback to default table creation if EDBInfo not found
    table = std::make_shared<sql::ast::Table>(id, ast_data_->arity_by_id[id]);
  }

  if (is_top_level) {
    // Build SELECT <alias>.<columns> AS <columns> FROM <Table> AS <alias>
    auto source = std::make_shared<sql::ast::Source>(table, GenerateTableAlias());
    std::vector<std::shared_ptr<sql::ast::Selectable>> select_columns;
    select_columns.reserve(table->arity);
    for (int i = 0; i < table->arity; ++i) {
      auto col_name = table->GetAttributeName(i);
      auto col = std::make_shared<sql::ast::Column>(col_name, source);
      select_columns.push_back(std::make_shared<sql::ast::TermSelectable>(col, col_name));
    }
    auto from_stmt = std::make_shared<sql::ast::FromStatement>(source);
    return std::make_shared<sql::ast::SelectStatement>(select_columns, from_stmt);
  }

  return table;
}

std::unordered_map<std::string, SQLVisitor::IndexedContext> SQLVisitor::GetFirstNonVarParamByFreeVariables(
    const std::vector<IndexedContext>& non_var_params) {
  std::unordered_map<std::string, IndexedContext> minimum_context_by_free_variable;

  for (auto indexed_ctx : non_var_params) {
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

std::pair<std::vector<SQLVisitor::IndexedContext>, std::vector<SQLVisitor::IndexedContext>>
SQLVisitor::GetVariableAndNonVariableParams(psr::ApplBaseContext* base,
                                            const std::vector<psr::ApplParamContext*>& params) {
  /*
   * Splits the parameters of the full application into variable and non-variable parameters.
   */
  std::vector<IndexedContext> var_params, non_var_params = {{base, 0}};

  for (int i = 0; i < params.size(); i++) {
    auto appl = params[i];
    if (!appl->T_UNDERSCORE()) {
      auto param_sql = std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(appl));

      if (auto variable_sql = std::dynamic_pointer_cast<sql::ast::Column>(param_sql)) {
        // Then the parameter is a variable
        GetNode(appl).sql_expression = variable_sql;
        var_params.push_back({appl, i + 1});
      } else if (auto subquery_sql = std::dynamic_pointer_cast<sql::ast::SelectStatement>(param_sql)) {
        // Then the parameter is not a variable
        auto param_subquery = std::make_shared<sql::ast::Source>(subquery_sql, GenerateTableAlias());
        GetNode(appl).sql_expression = param_subquery;

        non_var_params.push_back({appl, i + 1});
      }
    }
  }

  return std::make_pair(var_params, non_var_params);
}

std::unordered_set<BindingsBound> SQLVisitor::SafeFunction(psr::BindingInnerContext* binding_ctx,
                                                           antlr4::ParserRuleContext* expr_ctx) {
  /*
   * Computes the safe function from the paper.
   */

  std::unordered_set<BindingsBound> safe_result;

  std::set<std::string> binding_vars(GetNode(binding_ctx).variables);

  for (auto binding : binding_ctx->binding()) {
    if (binding->id_domain) {
      auto found = ast_data_->arity_by_id.find(binding->id_domain->getText());

      int domain_arity = found != ast_data_->arity_by_id.end() ? found->second : 0;

      auto table_source = TableSource(binding->id_domain->getText(), domain_arity);
      auto projection_table = SourceProjection(table_source);

      auto bindings_bound = BindingsBound({binding->id->getText()}, {projection_table});
      safe_result.insert(bindings_bound);

      binding_vars.erase(binding->id->getText());
    }
  }

  if (binding_vars.empty()) {
    return safe_result;
  }

  auto safeness = GetNode(expr_ctx).safeness;

  if (!safeness.has_value()) {
    throw InternalException("No safeness value for expression");
  }

  for (auto [vars, union_domain] : safeness.value()) {
    for (auto var : vars) {
      if (binding_vars.find(var) != binding_vars.end()) {
        binding_vars.erase(var);
      }
    }
  }

  if (binding_vars.empty()) {
    safe_result.insert(safeness.value().begin(), safeness.value().end());
    return safe_result;
  }

  throw SemanticException("Not all variables are bound", ErrorCode::UNBALANCED_VARIABLE);
}

std::unordered_map<BindingsBound, std::shared_ptr<sql::ast::Source>> SQLVisitor::ComputeBindingsCTEs(
    std::unordered_set<BindingsBound>& safe_result) {
  /*
   * Computes the CTEs for the bindings. Associates each safe result with the CTE that it generates for after use.
   */

  std::unordered_map<BindingsBound, std::shared_ptr<sql::ast::Source>> cte_map;

  for (auto& elem : safe_result) {
    if (elem.domain.empty()) {
      throw std::runtime_error("Empty union domain");
    }

    // Build selects for each domain
    std::vector<std::shared_ptr<sql::ast::Sourceable>> selects;
    for (auto projection : elem.domain) {
      // Create table with attribute names from EDBInfo (whether custom or standard A1, A2, etc.)

      auto bound_source = projection.source;
      auto table_bound = std::dynamic_pointer_cast<TableSource>(bound_source);
      auto constant_bound = std::dynamic_pointer_cast<ConstantSource>(bound_source);

      std::shared_ptr<sql::ast::SelectStatement> select;

      if (!constant_bound && !table_bound) {
        throw std::runtime_error("Unknown domain source type");
      } else if (constant_bound) {
        auto sql_constant = std::make_shared<sql::ast::Constant>(constant_bound->value);
        auto selectable = std::make_shared<sql::ast::TermSelectable>(sql_constant, "A1");

        select =
            std::make_shared<sql::ast::SelectStatement>(std::vector<std::shared_ptr<sql::ast::Selectable>>{selectable});

      } else {
        auto edb_info = ast_data_->GetEDBInfo(table_bound->table_name);
        std::shared_ptr<sql::ast::Table> table;
        if (edb_info && edb_info->arity() > 0) {
          std::vector<std::string> attribute_names;
          for (int i = 0; i < edb_info->arity(); ++i) {
            attribute_names.push_back(edb_info->get_attribute_name(i));
          }
          table = std::make_shared<sql::ast::Table>(table_bound->table_name, edb_info->arity(), attribute_names);
        } else {
          table = std::make_shared<sql::ast::Table>(table_bound->table_name,
                                                    ast_data_->arity_by_id[table_bound->table_name]);
        }

        auto source = std::make_shared<sql::ast::Source>(table);
        auto from = std::make_shared<sql::ast::FromStatement>(source);
        // Create columns based on domain.indices
        // If all columns are selected, use wildcard for better readability
        std::vector<std::shared_ptr<sql::ast::Selectable>> columns;
        if (static_cast<int>(projection.projected_indices.size()) == table->arity) {
          auto wildcard = std::make_shared<sql::ast::Wildcard>();
          columns.push_back(wildcard);
        } else {
          for (int index : projection.projected_indices) {
            std::string column_name = table->GetAttributeName(index);
            auto column = std::make_shared<sql::ast::Column>(column_name, source);
            auto term_selectable = std::make_shared<sql::ast::TermSelectable>(column);
            columns.push_back(term_selectable);
          }
        }
        select = std::make_shared<sql::ast::SelectStatement>(columns, from);
      }

      selects.push_back(select);
    }

    // Create final source - either from union or single select
    std::shared_ptr<sql::ast::Source> source;
    if (elem.domain.size() > 1) {
      auto union_cte = std::make_shared<sql::ast::Union>(selects);
      source = std::make_shared<sql::ast::Source>(union_cte, GenerateTableAlias("S"), true, elem.variables);
    } else {
      source = std::make_shared<sql::ast::Source>(selects[0], GenerateTableAlias("S"), true, elem.variables);
    }

    cte_map[elem] = source;
  }

  return cte_map;
}

std::vector<std::shared_ptr<sql::ast::Selectable>> SQLVisitor::ComputeBindingsOutput(
    const std::unordered_map<BindingsBound, std::shared_ptr<sql::ast::Source>>& safe_result) {
  /*
   * Computes the output for the bindings.
   */
  std::vector<std::shared_ptr<sql::ast::Selectable>> output;
  std::unordered_set<std::string> seen_vars;

  for (auto [tuple_binding, cte] : safe_result) {
    const auto& [vars, union_domain] = tuple_binding;
    for (auto var : vars) {
      if (seen_vars.find(var) != seen_vars.end()) continue;

      auto column = std::make_shared<sql::ast::Column>(var, cte);
      auto selectable = std::make_shared<sql::ast::TermSelectable>(column, fmt::format("A{}", output.size() + 1));

      output.push_back(selectable);
      seen_vars.insert(var);
    }
  }

  return output;
}

std::shared_ptr<sql::ast::Condition> SQLVisitor::BindingsEqualityShorthand(
    antlr4::ParserRuleContext* expr,
    const std::unordered_map<BindingsBound, std::shared_ptr<sql::ast::Source>>& safe_result) {
  /*
   * Generates a condition that equates all the repeated variables in the input map. This is the EQ function
   * in the paper.
   */

  auto expr_free_vars = GetNode(expr).free_variables;

  auto expr_source = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(expr).sql_expression);

  std::unordered_map<std::string, std::vector<std::shared_ptr<sql::ast::Source>>> repetition_map;

  std::vector<std::shared_ptr<sql::ast::Condition>> conditions;

  for (auto const& [tuple_binding, cte] : safe_result) {
    auto const& [vars, union_domain] = tuple_binding;
    for (auto const& var : vars) {
      if (expr_free_vars.find(var) != expr_free_vars.end()) {
        auto condition = std::make_shared<sql::ast::ComparisonCondition>(
            std::make_shared<sql::ast::Column>(var, cte), sql::ast::CompOp::EQ,
            std::make_shared<sql::ast::Column>(var, expr_source));

        conditions.push_back(condition);
      }
      repetition_map[var].push_back(cte);
    }
  }

  for (auto const& [var, sources] : repetition_map) {
    if (sources.size() < 2) continue;

    for (size_t i = 0; i < sources.size(); i++) {
      for (size_t j = i + 1; j < sources.size(); j++) {
        auto lhs = std::make_shared<sql::ast::Column>(var, sources[i]);
        auto rhs = std::make_shared<sql::ast::Column>(var, sources[j]);

        conditions.push_back(std::make_shared<sql::ast::ComparisonCondition>(lhs, sql::ast::CompOp::EQ, rhs));
      }
    }
  }

  return std::make_shared<sql::ast::LogicalCondition>(conditions, sql::ast::LogicalOp::AND);
}

}  // namespace rel2sql

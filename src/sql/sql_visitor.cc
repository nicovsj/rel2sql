#include "sql_visitor.h"

#include "optimizer/replacers.h"
#include "sql_ast/sql_ast.h"
#include "support/exceptions.h"

namespace rel2sql {

SQLVisitor::SQLVisitor(std::shared_ptr<RelAST> ast) : BaseVisitor(ast) {}

SQLVisitor::~SQLVisitor() = default;

std::shared_ptr<sql::ast::Sourceable> SQLVisitor::TryGetTopLevelIDSelect(psr::RelAbsContext* ctx) {
  if (!ctx) return nullptr;
  if (ctx->expr().size() != 1) return nullptr;

  auto single_expr_ctx = ctx->expr()[0];
  auto term_expr_ctx = dynamic_cast<psr::TermExprContext*>(single_expr_ctx);
  if (!term_expr_ctx) return nullptr;

  auto id_term_ctx = dynamic_cast<psr::IDTermContext*>(term_expr_ctx->term());
  if (!id_term_ctx) return nullptr;

  std::string id = id_term_ctx->T_ID()->getText();
  auto expr = GetExpressionFromID(term_expr_ctx, id, true);
  return std::dynamic_pointer_cast<sql::ast::Sourceable>(expr);
}

void SQLVisitor::ApplyDistinctToDefinitionSelects(const std::shared_ptr<sql::ast::Sourceable>& sourceable) {
  if (!sourceable) return;

  if (auto select = std::dynamic_pointer_cast<sql::ast::SelectStatement>(sourceable)) {
    select->is_distinct = true;
    return;
  }

  if (auto union_query = std::dynamic_pointer_cast<sql::ast::Union>(sourceable)) {
    for (auto& member : union_query->members) {
      ApplyDistinctToDefinitionSelects(member);
    }
    return;
  }

  if (auto union_all_query = std::dynamic_pointer_cast<sql::ast::UnionAll>(sourceable)) {
    for (auto& member : union_all_query->members) {
      ApplyDistinctToDefinitionSelects(std::static_pointer_cast<sql::ast::Sourceable>(member));
    }
  }
}

std::any SQLVisitor::visitProgram(psr::ProgramContext* ctx) {
  /*
   * Generates an SQL query from the program.
   */

  std::vector<std::shared_ptr<sql::ast::Expression>> exprs;

  for (auto& def_ctx : ctx->relDef()) {
    if (GetNode(def_ctx)->disabled) {
      continue;
    }
    auto expr = std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(def_ctx));
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

  ApplyDistinctToDefinitionSelects(child_sql);

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

  if (GetNode(ctx)->has_only_literal_values) {
    return SpecialVisitRelAbs(ctx);
  }

  auto query = VisitRelAbsLogic(ctx);

  // If there is only one definition, we can return the query directly
  if (GetNode(ctx)->multiple_defs.empty()) {
    return std::static_pointer_cast<sql::ast::Expression>(query);
  }

  // We do have multiple definitions for this relation abstraction
  std::vector<std::shared_ptr<sql::ast::Sourceable>> queries;

  queries.push_back(query);

  for (auto& additional_ctx : GetNode(ctx)->multiple_defs) {
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

  auto first_node = GetNode(first_ctx);

  auto first_source = std::make_shared<sql::ast::Source>(first_sql, GenerateTableAlias());
  first_node->sql_expression = first_source;

  if (expr_ctxs.size() == 1) {  // Single member relation abstraction
    return first_sql;
  }

  from_sources.push_back(first_source);
  values.push_back({1});

  // We assume that all the expressions have the same arity
  // This is checked in the arity visitor
  int arity = first_node->arity;

  for (size_t i = 1; i < expr_ctxs.size(); i++) {
    auto child_ctx = expr_ctxs[i];

    auto child_sql = std::dynamic_pointer_cast<sql::ast::Sourceable>(
        std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(child_ctx)));

    auto child_source = std::make_shared<sql::ast::Source>(child_sql, GenerateTableAlias());

    GetNode(child_ctx)->sql_expression = child_source;
    from_sources.push_back(child_source);
    values.push_back({static_cast<int>(i + 1)});
  }

  std::vector<antlr4::ParserRuleContext*> source_ctxs{expr_ctxs.begin(), expr_ctxs.end()};

  auto condition = EqualityShorthand(source_ctxs);

  auto selects = VarListShorthand(source_ctxs);

  // Define the VALUES expression used in the FROM clause
  auto values_expr = std::make_shared<sql::ast::Values>(values);
  auto values_alias =
      std::make_shared<sql::ast::AliasStatement>(GenerateTableAlias("I"), std::vector<std::string>{"i"});
  auto values_source = std::make_shared<sql::ast::Source>(values_expr, values_alias);
  from_sources.push_back(values_source);
  auto values_col = std::make_shared<sql::ast::Column>("i", values_source);

  // Define every CASE WHEN in the SELECT clause
  for (int i = 0; i < arity; i++) {
    std::vector<std::pair<std::shared_ptr<sql::ast::Condition>, std::shared_ptr<sql::ast::Term>>> cases;
    for (size_t j = 0; j < ctx->expr().size(); j++) {
      auto column = std::make_shared<sql::ast::Column>(fmt::format("A{}", i + 1), from_sources[j]);
      auto comparison =
          std::make_shared<sql::ast::ComparisonCondition>(values_col, sql::ast::CompOp::EQ, static_cast<int>(j + 1));
      cases.push_back({comparison, column});
    }
    auto case_when = std::make_shared<sql::ast::CaseWhen>(cases);
    auto term_selectable = std::make_shared<sql::ast::TermSelectable>(case_when, fmt::format("A{}", i + 1));
    selects.push_back(term_selectable);
  }

  auto from_statement = std::make_shared<sql::ast::FromStatement>(from_sources, condition);

  auto query = std::make_shared<sql::ast::SelectStatement>(selects, from_statement);

  return query;
}

std::any SQLVisitor::visitLitExpr(psr::LitExprContext* ctx) {
  /*
   * Generates an SQL query from the literal expression.
   */

  auto extended_data = GetNode(ctx);

  if (!extended_data->constant.has_value()) {
    throw std::runtime_error("Literal expression without constant value");
  }

  auto constant = std::make_shared<sql::ast::Constant>(extended_data->constant.value());

  auto selectable = std::make_shared<sql::ast::TermSelectable>(constant, "A1");

  auto select_statement =
      std::make_shared<sql::ast::SelectStatement>(std::vector<std::shared_ptr<sql::ast::Selectable>>{selectable});

  return std::static_pointer_cast<sql::ast::Expression>(select_statement);
}

std::any SQLVisitor::visitTermExpr(psr::TermExprContext* ctx) {
  /*
   * Generates an SQL query from the term expression.
   * Handles both constant terms and identifier terms (which may be relation names).
   */

  // Check if the term is an identifier term
  auto id_term_ctx = dynamic_cast<psr::IDTermContext*>(ctx->term());
  if (id_term_ctx) {
    std::string id = id_term_ctx->T_ID()->getText();
    auto node = GetNode(ctx);

    // Check if it's a variable (should reject for now)
    if (node->variables.find(id) != node->variables.end()) {
      throw VariableException("Terms with variables are not yet supported in expressions");
    }

    // Check if it's a relation name
    if (ast_->GetRelationInfo(id) != std::nullopt) {
      // Treat as relation lookup (like old IDExpr behavior)
      return GetExpressionFromID(ctx, id);
    }

    // If it's neither a variable nor a relation, it shouldn't happen in expression context
    throw InternalException("Identifier term in expression context is neither a variable nor a relation");
  }

  // For non-identifier terms (numeric constants, operations, etc.)
  // Check if term has free variables (only constant terms allowed for now)
  auto node = GetNode(ctx);
  if (!node->free_variables.empty()) {
    throw VariableException("Terms with variables are not yet supported in expressions");
  }

  // Visit the term to get a SQL Term
  auto term = std::any_cast<std::shared_ptr<sql::ast::Term>>(visit(ctx->term()));

  // Wrap the term in TermSelectable with alias "A1"
  auto selectable = std::make_shared<sql::ast::TermSelectable>(term, "A1");

  // Create SelectStatement with the term selectable
  auto select_statement =
      std::make_shared<sql::ast::SelectStatement>(std::vector<std::shared_ptr<sql::ast::Selectable>>{selectable});

  return std::static_pointer_cast<sql::ast::Expression>(select_statement);
}

std::any SQLVisitor::visitProductExpr(psr::ProductExprContext* ctx) {
  /*
   * Generates an SQL query from the product expression.
   */

  if (GetNode(ctx)->has_only_literal_values) {
    return SpecialVisitProductExpr(ctx);
  }

  std::vector<std::shared_ptr<sql::ast::Source>> from_sources;

  auto expr_ctxs = ctx->productInner()->expr();

  for (auto& child_ctx : expr_ctxs) {
    auto child_sql = std::static_pointer_cast<sql::ast::Sourceable>(
        std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(child_ctx)));
    auto child_subquery = std::make_shared<sql::ast::Source>(child_sql, GenerateTableAlias());
    GetNode(child_ctx)->sql_expression = child_subquery;
    from_sources.push_back(child_subquery);
  }

  std::vector<antlr4::ParserRuleContext*> source_ctxs{expr_ctxs.begin(), expr_ctxs.end()};

  auto condition = EqualityShorthand(source_ctxs);

  auto select_columns = VarListShorthand(source_ctxs);

  for (size_t i = 0; i < expr_ctxs.size(); i++) {
    auto child_ctx = expr_ctxs[i];
    auto child_source = std::static_pointer_cast<sql::ast::Source>(GetNode(child_ctx)->sql_expression);
    int child_arity = GetNode(child_ctx)->arity;
    for (int j = 1; j <= child_arity; j++) {
      auto column = std::make_shared<sql::ast::Column>(fmt::format("A{}", j), child_source);
      select_columns.push_back(std::make_shared<sql::ast::TermSelectable>(column, fmt::format("A{}", i + 1)));
    }
  }

  auto from_statement = std::make_shared<sql::ast::FromStatement>(from_sources, condition);

  auto query = std::make_shared<sql::ast::SelectStatement>(select_columns, from_statement);

  return std::static_pointer_cast<sql::ast::Expression>(query);
}

bool SQLVisitor::CollectComparatorOnlyConjuncts(psr::FormulaContext* formula_ctx,
                                                std::vector<psr::ComparisonContext*>& out) const {
  if (!formula_ctx) {
    return false;
  }

  if (auto* comparison_ctx = dynamic_cast<psr::ComparisonContext*>(formula_ctx)) {
    out.push_back(comparison_ctx);
    return true;
  }

  if (auto* paren_ctx = dynamic_cast<psr::ParenContext*>(formula_ctx)) {
    return CollectComparatorOnlyConjuncts(paren_ctx->formula(), out);
  }

  if (auto* bin_op_ctx = dynamic_cast<psr::BinOpContext*>(formula_ctx)) {
    if (!bin_op_ctx->K_and()) {
      return false;
    }
    return CollectComparatorOnlyConjuncts(bin_op_ctx->lhs, out) && CollectComparatorOnlyConjuncts(bin_op_ctx->rhs, out);
  }

  return false;
}

std::shared_ptr<sql::ast::Expression> SQLVisitor::TranslateConditionExprComparatorOnlyRHS(
    psr::ConditionExprContext* ctx, const std::shared_ptr<sql::ast::Sourceable>& lhs_sql,
    const std::vector<psr::ComparisonContext*>& comparator_conjuncts) {
  auto lhs_subquery = std::make_shared<sql::ast::Source>(lhs_sql, GenerateTableAlias());
  GetNode(ctx->lhs)->sql_expression = lhs_subquery;

  auto select_columns = VarListShorthand({ctx->lhs});

  for (size_t i = 1; i <= GetNode(ctx->lhs)->arity; i++) {
    auto column = std::make_shared<sql::ast::Column>(fmt::format("A{}", i), lhs_subquery);
    select_columns.push_back(std::make_shared<sql::ast::TermSelectable>(column));
  }

  auto from_statement =
      std::make_shared<sql::ast::FromStatement>(std::vector<std::shared_ptr<sql::ast::Source>>{lhs_subquery});

  std::vector<std::shared_ptr<sql::ast::Condition>> new_conditions;
  if (from_statement->where.has_value()) {
    new_conditions.push_back(from_statement->where.value());
  }

  std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>> free_var_sources;
  for (const auto& free_var : GetNode(ctx->lhs)->free_variables) {
    free_var_sources.emplace(free_var, lhs_subquery);
  }

  for (auto* comparator_ctx : comparator_conjuncts) {
    auto comparator_expr = std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(comparator_ctx));
    auto comparator_sql = std::dynamic_pointer_cast<sql::ast::ComparisonCondition>(comparator_expr);
    if (!comparator_sql) {
      throw std::runtime_error("Expected comparison to translate to ComparisonCondition");
    }

    SpecialAddSourceToFreeVariablesInTerm(free_var_sources, comparator_sql->lhs);
    SpecialAddSourceToFreeVariablesInTerm(free_var_sources, comparator_sql->rhs);

    new_conditions.push_back(std::static_pointer_cast<sql::ast::Condition>(comparator_sql));
  }

  std::shared_ptr<sql::ast::Condition> new_where;
  if (new_conditions.size() == 1) {
    new_where = new_conditions[0];
  } else {
    new_where = std::make_shared<sql::ast::LogicalCondition>(new_conditions, sql::ast::LogicalOp::AND);
  }

  from_statement->where = new_where;

  auto query = std::make_shared<sql::ast::SelectStatement>(select_columns, from_statement);
  return std::static_pointer_cast<sql::ast::Expression>(query);
}

std::any SQLVisitor::visitConditionExpr(psr::ConditionExprContext* ctx) {
  /*
   * Generates an SQL query from the condition expression.
   */

  std::vector<psr::ComparisonContext*> comparator_conjuncts;
  bool rhs_is_comparator_only = CollectComparatorOnlyConjuncts(ctx->rhs, comparator_conjuncts);

  auto lhs_expr = std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(ctx->lhs));
  auto lhs_sql = std::dynamic_pointer_cast<sql::ast::Sourceable>(lhs_expr);
  if (!lhs_sql) {
    throw std::runtime_error("Condition expression lhs is not a sourceable SQL expression");
  }

  // Special case: RHS is a (possibly parenthesized) conjunction of comparisons only.
  // Translate lhs first, then push comparator conditions into the WHERE clause.
  if (rhs_is_comparator_only) {
    return TranslateConditionExprComparatorOnlyRHS(ctx, lhs_sql, comparator_conjuncts);
  }

  auto rhs_expr = std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(ctx->rhs));
  auto rhs_sql = std::dynamic_pointer_cast<sql::ast::Sourceable>(rhs_expr);
  if (!rhs_sql) {
    throw std::runtime_error("Condition expression rhs is not a sourceable SQL expression");
  }

  auto lhs_subquery = std::make_shared<sql::ast::Source>(lhs_sql, GenerateTableAlias());
  auto rhs_subquery = std::make_shared<sql::ast::Source>(rhs_sql, GenerateTableAlias());

  GetNode(ctx->lhs)->sql_expression = lhs_subquery;
  GetNode(ctx->rhs)->sql_expression = rhs_subquery;

  auto condition = EqualityShorthand({ctx->lhs, ctx->rhs});

  auto select_columns = VarListShorthand({ctx->lhs, ctx->rhs});

  for (size_t i = 1; i <= GetNode(ctx->lhs)->arity; i++) {
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
  GetNode(ctx->relAbs())->sql_expression = child_sql;

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

  // Extract CTEs from nested SelectStatements and lift them to the outer level
  std::vector<std::shared_ptr<sql::ast::Source>> nested_ctes;
  auto nested_select = std::dynamic_pointer_cast<sql::ast::SelectStatement>(expr_sql);
  if (nested_select && !nested_select->ctes.empty() && !nested_select->ctes_are_recursive) {
    // Extract CTEs from the nested SelectStatement
    nested_ctes = nested_select->ctes;

    // Create a new SelectStatement without CTEs to use as the subquery
    // Handle different constructor cases based on what fields are present
    if (nested_select->group_by.has_value() && nested_select->from.has_value()) {
      expr_sql =
          std::make_shared<sql::ast::SelectStatement>(nested_select->columns, nested_select->from.value(),
                                                      nested_select->group_by.value(), nested_select->is_distinct);
    } else if (nested_select->from.has_value()) {
      expr_sql = std::make_shared<sql::ast::SelectStatement>(nested_select->columns, nested_select->from.value(),
                                                             nested_select->is_distinct);
    } else {
      expr_sql = std::make_shared<sql::ast::SelectStatement>(nested_select->columns, nested_select->is_distinct);
    }
  }

  auto expr_source = std::make_shared<sql::ast::Source>(expr_sql, GenerateTableAlias());

  GetNode(ctx->expr())->sql_expression = expr_source;

  auto free_variables = GetNode(ctx)->free_variables;

  auto safe_result = SafeFunction(ctx->bindingInner(), ctx->expr());

  auto cte_map = ComputeBindingsCTEs(safe_result);

  auto select_columns = VarListShorthand({{ctx, expr_source}});

  auto binding_output = ComputeBindingsOutput(cte_map, ctx->bindingInner());

  select_columns.insert(select_columns.end(), binding_output.begin(), binding_output.end());

  int expr_arity = GetNode(ctx->expr())->arity;

  for (int i = 1; i <= expr_arity; i++) {
    auto column = std::make_shared<sql::ast::Column>(fmt::format("A{}", i), expr_source);
    select_columns.push_back(
        std::make_shared<sql::ast::TermSelectable>(column, fmt::format("A{}", binding_output.size() + i)));
  }

  std::vector<std::shared_ptr<sql::ast::Source>> from_sources = {expr_source};

  // Collect stored CTEs from full_appl_ctes_ that are referenced in safe_result FIRST
  // (so they're defined before binding CTEs that reference them)
  auto [extracted_stored_ctes, stored_ctes] = CollectReferencedStoredCTEs(safe_result);

  std::vector<std::shared_ptr<sql::ast::Source>> ctes;
  // Add extracted nested CTEs from stored CTEs first
  ctes.insert(ctes.end(), extracted_stored_ctes.begin(), extracted_stored_ctes.end());

  // Add nested CTEs from the expression (if any) - these come after extracted stored CTEs
  // but before stored CTEs and binding CTEs that may reference them
  ctes.insert(ctes.end(), nested_ctes.begin(), nested_ctes.end());

  // Add the stored CTEs (with cleaned sourceables)
  ctes.insert(ctes.end(), stored_ctes.begin(), stored_ctes.end());

  // Then add binding CTEs (which may reference the stored CTEs above)
  for (auto& [_, cte] : cte_map) {
    ctes.push_back(cte);
  }

  // Only add binding CTEs to from_sources, NOT stored RA CTEs
  // Stored CTEs are only in the CTE list for definition, not in the FROM clause
  for (auto& [_, cte] : cte_map) {
    from_sources.push_back(cte);
  }

  auto condition = BindingsEqualityShorthand(ctx->expr(), cte_map);

  auto from = std::make_shared<sql::ast::FromStatement>(from_sources, condition);

  auto query = std::make_shared<sql::ast::SelectStatement>(select_columns, from, ctes);

  return std::static_pointer_cast<sql::ast::Expression>(query);
}

std::any SQLVisitor::visitBindingsFormula(psr::BindingsFormulaContext* ctx) {
  /*
   * Generates an SQL query from the bindings formula.
   */

  auto bindings_formula_node = GetNode(ctx);
  std::shared_ptr<sql::ast::Source> formula_source;
  std::vector<std::shared_ptr<sql::ast::Source>> formula_ctes;

  auto formula_sql = std::dynamic_pointer_cast<sql::ast::Sourceable>(
      std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(ctx->formula())));

  // Extract CTEs from nested SelectStatements and lift them to the outer level
  std::vector<std::shared_ptr<sql::ast::Source>> nested_ctes;
  auto nested_select = std::dynamic_pointer_cast<sql::ast::SelectStatement>(formula_sql);
  if (nested_select && !nested_select->ctes.empty() && !nested_select->ctes_are_recursive) {
    // Extract CTEs from the nested SelectStatement
    nested_ctes = nested_select->ctes;

    // Create a new SelectStatement without CTEs to use as the subquery
    // Handle different constructor cases based on what fields are present
    if (nested_select->group_by.has_value() && nested_select->from.has_value()) {
      formula_sql =
          std::make_shared<sql::ast::SelectStatement>(nested_select->columns, nested_select->from.value(),
                                                      nested_select->group_by.value(), nested_select->is_distinct);
    } else if (nested_select->from.has_value()) {
      formula_sql = std::make_shared<sql::ast::SelectStatement>(nested_select->columns, nested_select->from.value(),
                                                                nested_select->is_distinct);
    } else {
      formula_sql = std::make_shared<sql::ast::SelectStatement>(nested_select->columns, nested_select->is_distinct);
    }
  }

  // Check if this bindings formula is recursive
  std::shared_ptr<sql::ast::Source> recursive_cte_source;
  if (bindings_formula_node->is_recursive && !bindings_formula_node->recursive_definition_name.empty()) {
    // Get binding variables from bindingInner (these are the free variables)
    std::vector<std::string> binding_vars;
    for (auto binding : ctx->bindingInner()->binding()) {
      if (binding->id) {
        binding_vars.push_back(binding->id->getText());
      }
    }

    // Get arity for A1, A2, etc. column names
    int arity = static_cast<int>(binding_vars.size());

    // Create recursive CTE from formula with A1, A2, etc. as column names
    std::tie(recursive_cte_source, formula_ctes) =
        CreateRecursiveCTEFromFormula(formula_sql, bindings_formula_node->recursive_definition_name, arity);

    // Wrap R0 in a subquery that maps A1, A2, etc. to x, y, etc.
    std::vector<std::shared_ptr<sql::ast::Selectable>> subquery_select_columns;
    for (size_t i = 0; i < binding_vars.size(); i++) {
      auto col_name = fmt::format("A{}", i + 1);
      auto column = std::make_shared<sql::ast::Column>(col_name, recursive_cte_source);
      subquery_select_columns.push_back(std::make_shared<sql::ast::TermSelectable>(column, binding_vars[i]));
    }

    auto subquery_from =
        std::make_shared<sql::ast::FromStatement>(std::vector<std::shared_ptr<sql::ast::Source>>{recursive_cte_source});
    auto subquery = std::make_shared<sql::ast::SelectStatement>(subquery_select_columns, subquery_from);
    formula_source = std::make_shared<sql::ast::Source>(subquery, GenerateTableAlias());
  } else {
    formula_source = std::make_shared<sql::ast::Source>(formula_sql, GenerateTableAlias());
  }

  GetNode(ctx->formula())->sql_expression = formula_source;

  // Continue with normal bindings formula flow
  auto safe_result = SafeFunction(ctx->bindingInner(), ctx->formula());

  auto cte_map = ComputeBindingsCTEs(safe_result);

  auto select_columns = VarListShorthand({{ctx, formula_source}});

  auto binding_output = ComputeBindingsOutput(cte_map, ctx->bindingInner());

  select_columns.insert(select_columns.end(), binding_output.begin(), binding_output.end());

  std::vector<std::shared_ptr<sql::ast::Source>> from_sources = {formula_source};

  // Collect stored CTEs from full_appl_ctes_ that are referenced in safe_result FIRST
  // (so they're defined before binding CTEs that reference them)
  auto [extracted_stored_ctes, stored_ctes] = CollectReferencedStoredCTEs(safe_result);

  std::vector<std::shared_ptr<sql::ast::Source>> ctes;
  // Add extracted nested CTEs from stored CTEs first
  ctes.insert(ctes.end(), extracted_stored_ctes.begin(), extracted_stored_ctes.end());

  // Add nested CTEs from the formula (if any) - these come after extracted stored CTEs
  // but before stored CTEs, formula CTEs and binding CTEs that may reference them
  ctes.insert(ctes.end(), nested_ctes.begin(), nested_ctes.end());

  // Add the stored CTEs (with cleaned sourceables)
  ctes.insert(ctes.end(), stored_ctes.begin(), stored_ctes.end());

  // Add formula CTEs (for recursive case, these are CTEs from the recursive definition)
  ctes.insert(ctes.end(), formula_ctes.begin(), formula_ctes.end());

  // Then add binding CTEs (which may reference the stored CTEs above)
  for (auto& [_, cte] : cte_map) {
    ctes.push_back(cte);
  }

  // If recursive, add the recursive CTE after binding CTEs
  if (bindings_formula_node->is_recursive && !bindings_formula_node->recursive_definition_name.empty()) {
    ctes.push_back(recursive_cte_source);
  }

  // Only add binding CTEs to from_sources, NOT stored RA CTEs
  // Stored CTEs are only in the CTE list for definition, not in the FROM clause
  for (auto& [_, cte] : cte_map) {
    from_sources.push_back(cte);
  }

  auto condition = BindingsEqualityShorthand(ctx->formula(), cte_map);

  auto from = std::make_shared<sql::ast::FromStatement>(from_sources, condition);

  // For recursive case, mark the query as recursive
  bool is_recursive = bindings_formula_node->is_recursive && !bindings_formula_node->recursive_definition_name.empty();

  auto query = std::make_shared<sql::ast::SelectStatement>(select_columns, from, ctes, false, is_recursive);

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

  std::shared_ptr<sql::ast::Source> stored_cte = nullptr;

  if (ctx->applBase()->relAbs()) {
    // We should check if the base of the full application is a relation abstraction with free variables.
    // If it is, then it will happen that both the safety bound will use the relational abstraction SQL as the source,
    // alongside this very same full application. We solve this SQL duplication by storing the CTE in the
    // full_appl_ctes_ map. This way, we can reuse the same CTE for both the safety bound and the full application.
    auto node = GetNode(ctx);

    if (node->safety.Size() != 1) {
      throw InternalException("Full application on a relational abstraction with wrong number of bounds");
    }

    auto bound = *node->safety.bounds.begin();
    auto projection = *bound.domain.begin();
    auto promised_source = std::dynamic_pointer_cast<PromisedSource>(projection.source);

    if (!promised_source) {
      throw InternalException("Source of relational abstraction is not promised");
    }

    // Check if this Full Application has free variables (only store CTE for relAbs with free vars)
    if (!node->free_variables.empty()) {
      // Create a CTE from the relational abstraction SQL
      auto cte_alias = GenerateTableAlias("RA");
      auto cte = std::make_shared<sql::ast::Source>(ra_sql, cte_alias, true);

      // Store the CTE keyed by projection (with PromisedSource) for later reuse in ComputeBindingsCTEs
      // Note: We store with the original projection containing PromisedSource
      full_appl_ctes_[projection] = cte;
      stored_cte = cte;

      // Fulfill the PromisedSource with the sourceable
      // When resolved later, it will become a GenericSource that we can match to the stored CTE
      promised_source->Fulfill(ra_sql);
    } else {
      // No free variables, just fulfill with the sourceable directly
      promised_source->Fulfill(ra_sql);
    }
  }

  std::shared_ptr<sql::ast::Source> ra_source;

  if (stored_cte) {
    ra_source = stored_cte;
  } else {
    ra_source = std::make_shared<sql::ast::Source>(ra_sql, GenerateTableAlias());
  }

  GetNode(ctx->applBase())->sql_expression = ra_source;

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
    auto source_subquery = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(ctx)->sql_expression);
    from_sources.push_back(source_subquery);
  }

  conditions.push_back(EqualityShorthand(source_ctxs));

  std::shared_ptr<sql::ast::Condition> condition;

  if (conditions.size() == 1) {
    condition = conditions[0];
  } else {
    condition = std::make_shared<sql::ast::LogicalCondition>(conditions, sql::ast::LogicalOp::AND);
  }

  auto select_cols = SpecialAppliedVarList(ctx->applBase(), non_var_params, var_params, non_var_param_by_free_vars);

  int m = ctx->applParams()->applParam().size();

  for (size_t i = 1; i <= GetNode(ctx->applBase())->arity - m; i++) {
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

  std::shared_ptr<sql::ast::Source> stored_cte = nullptr;

  if (ctx->applBase()->relAbs()) {
    // We should check if the base of the full application is a relation abstraction with free variables.
    // If it is, then it will happen that both the safety bound will use the relational abstraction SQL as the source,
    // alongside this very same full application. We solve this SQL duplication by storing the CTE in the
    // full_appl_ctes_ map. This way, we can reuse the same CTE for both the safety bound and the full application.
    auto node = GetNode(ctx);

    if (node->safety.Size() != 1) {
      throw InternalException("Full application on a relational abstraction with wrong number of bounds");
    }

    auto bound = *node->safety.bounds.begin();
    auto projection = *bound.domain.begin();
    auto promised_source = std::dynamic_pointer_cast<PromisedSource>(projection.source);

    if (!promised_source) {
      throw InternalException("Source of relational abstraction is not promised");
    }

    // Check if this Full Application has free variables (only store CTE for relAbs with free vars)
    if (!node->free_variables.empty()) {
      // Create a CTE from the relational abstraction SQL
      auto cte_alias = GenerateTableAlias("RA");
      auto cte = std::make_shared<sql::ast::Source>(ra_sql, cte_alias, true);

      // Store the CTE keyed by projection (with PromisedSource) for later reuse in ComputeBindingsCTEs
      // Note: We store with the original projection containing PromisedSource
      full_appl_ctes_[projection] = cte;
      stored_cte = cte;

      // Fulfill the PromisedSource with the sourceable
      // When resolved later, it will become a GenericSource that we can match to the stored CTE
      promised_source->Fulfill(ra_sql);
    } else {
      // No free variables, just fulfill with the sourceable directly
      promised_source->Fulfill(ra_sql);
    }
  }

  // Use the stored CTE as the source if available, otherwise create a new source from ra_sql
  std::shared_ptr<sql::ast::Source> ra_source;
  if (stored_cte) {
    // Use the stored CTE directly - it's already a Source with is_cte=true
    ra_source = stored_cte;
  } else {
    // No stored CTE, create a new source from the sourceable
    ra_source = std::make_shared<sql::ast::Source>(ra_sql, GenerateTableAlias());
  }

  auto base_node = GetNode(ctx->applBase());

  base_node->sql_expression = ra_source;

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
    auto source_subquery = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(ctx)->sql_expression);
    from_sources.push_back(source_subquery);
  }

  conditions.push_back(EqualityShorthand(source_ctxs));

  std::shared_ptr<sql::ast::Condition> condition;

  if (conditions.size() == 1) {
    condition = conditions[0];
  } else {
    condition = std::make_shared<sql::ast::LogicalCondition>(conditions, sql::ast::LogicalOp::AND);
  }

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

  // Negation should be handled by the conjunction case only. So here we just pass the formula to the next visitor and
  // let the caller handle the correct construction.
  if (ctx->K_not()) {
    return visit(ctx->formula());
  }

  throw SemanticException("Unknown unary operation", ErrorCode::UNKNOWN_UNARY_OPERATOR);
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
  } else if (comparator == "!=") {
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
    auto rel_abs_sql = std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(ctx->relAbs()));
    return std::dynamic_pointer_cast<sql::ast::Sourceable>(rel_abs_sql);
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
  return std::static_pointer_cast<sql::ast::Term>(std::make_shared<sql::ast::Constant>(GetNode(ctx)->constant.value()));
}

std::any SQLVisitor::visitOpTerm(psr::OpTermContext* ctx) {
  /*
   * Generates an SQL query from the application term.
   */

  auto lhs_term = std::any_cast<std::shared_ptr<sql::ast::Term>>(visit(ctx->lhs));
  auto rhs_term = std::any_cast<std::shared_ptr<sql::ast::Term>>(visit(ctx->rhs));

  return std::static_pointer_cast<sql::ast::Term>(
      std::make_shared<sql::ast::Operation>(lhs_term, rhs_term, ctx->arithmeticOperator()->getText()));
}

std::any SQLVisitor::visitParenthesisTerm(psr::ParenthesisTermContext* ctx) {
  /*
   * Generates an SQL query from the parenthesized term.
   */
  auto term = std::any_cast<std::shared_ptr<sql::ast::Term>>(visit(ctx->term()));
  return std::static_pointer_cast<sql::ast::Term>(std::make_shared<sql::ast::ParenthesisTerm>(term));
}

std::any SQLVisitor::VisitConjunction(psr::BinOpContext* ctx) {
  /*
   * Generates an SQL query from the conjunction of the two formulas.
   */

  auto node = GetNode(ctx);

  // There is a special case for conjunctions with terms, which are already partitioned
  // by the balancing visitor.
  if (node->IsConjunctionWithTerms()) {
    return VisitConjunctionWithTerms(ctx);
  }

  if (node->IsConjunctionWithNegations()) {
    return VisitConjunctionWithNegations(ctx);
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

    GetNode(subformula)->sql_expression = subquery;
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

  std::vector<antlr4::ParserRuleContext*> other_formulas = GetNode(ctx)->non_comparator_conjuncts;
  std::vector<antlr4::ParserRuleContext*> comparator_formulas = GetNode(ctx)->comparator_conjuncts;

  auto select_expression = std::static_pointer_cast<sql::ast::SelectStatement>(
      std::any_cast<std::shared_ptr<sql::ast::Expression>>(VisitGeneralizedConjunction(other_formulas)));

  auto from_statement = select_expression->from.value();

  std::vector<std::shared_ptr<sql::ast::Condition>> new_conditions;

  if (from_statement->where.has_value()) {
    new_conditions.push_back(from_statement->where.value());
  }

  std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>> free_var_sources;

  for (auto source_ctx : other_formulas) {
    for (auto free_var : GetNode(source_ctx)->free_variables) {
      if (free_var_sources.find(free_var) == free_var_sources.end()) {
        free_var_sources[free_var] = std::static_pointer_cast<sql::ast::Source>(GetNode(source_ctx)->sql_expression);
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

  std::shared_ptr<sql::ast::Condition> new_where;

  if (new_conditions.size() == 1) {
    new_where = new_conditions[0];
  } else {
    new_where = std::make_shared<sql::ast::LogicalCondition>(new_conditions, sql::ast::LogicalOp::AND);
  }

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

std::any SQLVisitor::VisitConjunctionWithNegations(psr::BinOpContext* ctx) {
  // We know that this subtree is a conjunction of formulas of the form:
  //
  // C1 and C2 and ... and Cn and not F1 and not F2 and ... and not Fm,
  //
  // where each Ci is a formula that has to be translated, and each Fi is a formula that has to be negated.

  std::vector<antlr4::ParserRuleContext*> other_formulas = GetNode(ctx)->non_negated_conjuncts;
  std::vector<antlr4::ParserRuleContext*> negated_formulas = GetNode(ctx)->negated_conjuncts;

  auto select_expression = std::static_pointer_cast<sql::ast::SelectStatement>(
      std::any_cast<std::shared_ptr<sql::ast::Expression>>(VisitGeneralizedConjunction(other_formulas)));

  auto from_statement = select_expression->from.value();

  std::vector<std::shared_ptr<sql::ast::Condition>> new_conditions;

  if (from_statement->where.has_value()) {
    new_conditions.push_back(from_statement->where.value());
  }

  std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>> free_var_sources;

  for (auto source_ctx : other_formulas) {
    for (auto free_var : GetNode(source_ctx)->free_variables) {
      if (free_var_sources.find(free_var) == free_var_sources.end()) {
        free_var_sources[free_var] = std::static_pointer_cast<sql::ast::Source>(GetNode(source_ctx)->sql_expression);
      }
    }
  }

  for (auto negated_formula : negated_formulas) {
    auto negated_formula_sql = std::static_pointer_cast<sql::ast::SelectStatement>(
        std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(negated_formula)));

    auto negated_formula_node = GetNode(negated_formula);

    std::vector<std::shared_ptr<sql::ast::Column>> columns;

    for (auto free_var : negated_formula_node->free_variables) {
      auto it = free_var_sources.find(free_var);
      if (it != free_var_sources.end()) {
        columns.push_back(std::make_shared<sql::ast::Column>(free_var, it->second));
      } else {
        throw std::runtime_error("Free variable not found in free variable sources");
      }
    }

    auto inclusion = std::make_shared<sql::ast::Inclusion>(columns, negated_formula_sql, true);

    new_conditions.push_back(inclusion);
  }

  std::shared_ptr<sql::ast::Condition> new_where;

  if (new_conditions.size() == 1) {
    new_where = new_conditions[0];
  } else {
    new_where = std::make_shared<sql::ast::LogicalCondition>(new_conditions, sql::ast::LogicalOp::AND);
  }

  from_statement->where = new_where;

  return std::static_pointer_cast<sql::ast::Expression>(select_expression);
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

  GetNode(ctx->lhs)->sql_expression = lhs_subquery;
  GetNode(ctx->rhs)->sql_expression = rhs_subquery;

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

  auto free_vars = GetNode(ctx)->free_variables;

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

  std::shared_ptr<sql::ast::Condition> condition;

  if (conditions.size() == 1) {
    condition = conditions[0];
  } else {
    condition = std::make_shared<sql::ast::LogicalCondition>(conditions, sql::ast::LogicalOp::AND);
  }

  auto from = std::make_shared<sql::ast::FromStatement>(sources, condition);

  auto query = std::make_shared<sql::ast::SelectStatement>(select_columns, from);

  return std::static_pointer_cast<sql::ast::Expression>(query);
}

std::any SQLVisitor::VisitUniversal(psr::QuantificationContext* ctx) {
  /*
   * Generates an SQL query from the universal quantification.
   */

  auto free_vars = GetNode(ctx)->free_variables;

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

  GetNode(expr_ctx)->sql_expression = subquery;

  int arity = GetNode(expr_ctx)->arity;

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

  auto current_node = GetNode(ctx);
  for (auto more_ctx : current_node->multiple_defs) {
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

  size_t arity = GetNode(expr_ctxs[0])->arity;

  for (auto expr_ctx : expr_ctxs) {
    auto expr_node = GetNode(expr_ctx);
    if (expr_node->arity != arity) {
      throw std::runtime_error("Inconsistent arity in relation abstraction");
    }

    if (auto product_inner = dynamic_cast<psr::ProductExprContext*>(expr_ctx)) {
      auto product_inner_ctx = product_inner->productInner();
      if (product_inner_ctx) {
        std::vector<sql::ast::Constant> row;
        for (auto term_ctx : product_inner_ctx->expr()) {
          auto constant = GetNode(term_ctx)->constant;
          auto sql_constant = std::make_shared<sql::ast::Constant>(constant.value());
          row.push_back(*sql_constant);
        }
        values.push_back(row);
      } else {
        throw std::runtime_error("Invalid product expression: missing productInner");
      }
    } else if (auto lit_expr_ctx = dynamic_cast<psr::LitExprContext*>(expr_ctx)) {
      auto constant = GetNode(lit_expr_ctx)->constant;
      auto sql_constant = std::make_shared<sql::ast::Constant>(constant.value());
      std::vector<sql::ast::Constant> row{*sql_constant};
      values.push_back(row);
    } else {
      throw std::runtime_error("Invalid expression in relation abstraction: expected product expression");
    }
  }

  auto values_expr = std::make_shared<sql::ast::Values>(values);

  std::vector<std::string> column_names;
  for (size_t i = 1; i <= arity; ++i) {
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
    if (!GetNode(child_ctx)->constant.has_value()) {
      throw std::runtime_error("Special product expression with non-constant member");
    }
    auto constant = std::make_shared<sql::ast::Constant>(GetNode(child_ctx)->constant.value());
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
  auto edb_info = ast_->GetRelationInfo(table_name);
  if (edb_info && edb_info->arity > 0) {
    std::vector<std::string> attribute_names;
    for (int i = 0; i < edb_info->arity; ++i) {
      attribute_names.push_back(edb_info->AttributeName(i));
    }
    table = std::make_shared<sql::ast::Table>(table_name, edb_info->arity, attribute_names);
    // Create a source with an alias that includes the attribute names
    auto alias = std::make_shared<sql::ast::AliasStatement>(GenerateTableAlias());
    return std::make_shared<sql::ast::Source>(table, alias);
  } else {
    // Fallback to default table creation if EDBInfo not found
    table = std::make_shared<sql::ast::Table>(table_name, ast_->GetArity(table_name));
    return std::make_shared<sql::ast::Source>(table);
  }
}

std::string SQLVisitor::GetEDBColumnName(const std::string& table_name, int index) const {
  /*
   * Gets the proper column name for an EDB table at the given index.
   */
  // Always use EDBInfo for attribute names (whether custom or standard A1, A2, etc.)
  auto edb_info = ast_->GetRelationInfo(table_name);
  if (edb_info) {
    return edb_info->AttributeName(index);
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
    auto variable = *(GetNode(numbered_ctx.ctx)->variables.begin());
    params_by_variable[variable].insert(numbered_ctx);
  }

  std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>> seen_vars;

  auto ra_subquery = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(base_appl_ctx)->sql_expression);

  for (auto [explicit_variable, variable_params] : params_by_variable) {
    auto found = params_by_free_vars.find(explicit_variable);
    if (found != params_by_free_vars.end()) {
      // Then the variable is also in one of the sub queries, and we need to equate the respective columns
      auto found_numbered_ctx = found->second;

      auto min_sql = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(found_numbered_ctx.ctx)->sql_expression);

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

  for (size_t i = 1; i < non_variable_param_ctxs.size(); i++) {
    auto ctx = non_variable_param_ctxs[i].ctx;
    auto index = non_variable_param_ctxs[i].index;
    auto sq = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(ctx)->sql_expression);
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
    for (auto const& var : GetNode(ctx)->variables) {
      repetition_map[var].push_back(ctx);
    }
  }

  std::vector<std::shared_ptr<sql::ast::Condition>> conditions;

  for (auto const& [var, ctxs] : repetition_map) {
    if (ctxs.size() < 2) continue;

    for (size_t i = 0; i < ctxs.size(); i++) {
      for (size_t j = i + 1; j < ctxs.size(); j++) {
        auto lhs_source = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(ctxs[i])->sql_expression);
        auto rhs_source = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(ctxs[j])->sql_expression);

        auto lhs = std::make_shared<sql::ast::Column>(var, lhs_source);
        auto rhs = std::make_shared<sql::ast::Column>(var, rhs_source);

        conditions.push_back(std::make_shared<sql::ast::ComparisonCondition>(lhs, sql::ast::CompOp::EQ, rhs));
      }
    }
  }

  std::shared_ptr<sql::ast::Condition> condition;

  if (conditions.size() == 1) {
    condition = conditions[0];
  } else {
    condition = std::make_shared<sql::ast::LogicalCondition>(conditions, sql::ast::LogicalOp::AND);
  }

  return condition;
}

std::vector<std::shared_ptr<sql::ast::Selectable>> SQLVisitor::VarListShorthand(
    std::vector<antlr4::ParserRuleContext*> input_ctxs) {
  std::unordered_set<std::string> seen_vars;
  std::vector<std::shared_ptr<sql::ast::Selectable>> columns;

  for (auto const& ctx : input_ctxs) {
    auto node = GetNode(ctx);
    auto source = std::dynamic_pointer_cast<sql::ast::Source>(node->sql_expression);

    for (auto const& var : node->free_variables) {
      if (seen_vars.find(var) != seen_vars.end()) continue;

      if (!source) {
        throw InternalException("Context not translated to sql::ast::Source");
      }

      auto column = std::make_shared<sql::ast::Column>(var, source);
      auto selectable = std::make_shared<sql::ast::TermSelectable>(column);

      columns.push_back(selectable);
      seen_vars.insert(var);
    }
  }

  return columns;
}

std::vector<std::shared_ptr<sql::ast::Selectable>> SQLVisitor::VarListShorthand(
    std::vector<ContextSourcePair> ctx_source_pairs) {
  std::unordered_set<std::string> seen_vars;
  std::vector<std::shared_ptr<sql::ast::Selectable>> columns;

  for (auto const& [ctx, source] : ctx_source_pairs) {
    for (auto const& var : GetNode(ctx)->free_variables) {
      if (seen_vars.find(var) != seen_vars.end()) continue;

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
                       return free_vars_in_non_variable_params.find(*GetNode(ctx.ctx)->variables.begin()) !=
                              free_vars_in_non_variable_params.end();
                     }),
      variable_param_ctxs.end());

  // Map the non-free (bound) variable parameter indexes to the variable names
  for (auto const& [ctx, index] : variable_param_ctxs) {
    auto variables = GetNode(ctx)->variables;

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

  auto ra_subquery = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(base_ctx)->sql_expression);

  for (auto const& [ctx, index] : final_ctxs) {
    for (auto const& var : GetNode(ctx)->free_variables) {
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
        auto subquery = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(ctx)->sql_expression);

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
  if (node->variables.find(id) != node->variables.end()) {
    throw InternalException("Non-parameter variable expressions not yet implemented.");
  }

  // Always create table with attribute names from EDBInfo (whether custom or standard A1, A2, etc.)
  auto edb_info = ast_->GetRelationInfo(id);
  std::shared_ptr<sql::ast::Table> table;
  if (edb_info && edb_info->arity > 0) {
    std::vector<std::string> attribute_names;
    for (int i = 0; i < edb_info->arity; ++i) {
      attribute_names.push_back(edb_info->AttributeName(i));
    }
    table = std::make_shared<sql::ast::Table>(id, edb_info->arity, attribute_names);
  } else {
    // Fallback to default table creation if EDBInfo not found
    table = std::make_shared<sql::ast::Table>(id, ast_->GetArity(id));
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
    for (auto var : GetNode(indexed_ctx.ctx)->free_variables) {
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

  for (size_t i = 0; i < params.size(); i++) {
    auto param = params[i];
    auto param_node = GetNode(param);

    if (param->T_UNDERSCORE()) continue;

    // Check if the parameter is a variable before visiting it
    auto term_expr_ctx = dynamic_cast<psr::TermExprContext*>(param->expr());
    if (term_expr_ctx) {
      auto id_term_ctx = dynamic_cast<psr::IDTermContext*>(term_expr_ctx->term());
      if (id_term_ctx && param_node->variables.size() == 1) {
        // This is a variable parameter - create Column directly
        auto variable = *param_node->variables.begin();
        auto variable_sql = std::make_shared<sql::ast::Column>(variable);
        param_node->sql_expression = variable_sql;
        var_params.push_back({param, i + 1});
        continue;
      }
    }

    // Not a variable, let the visitor handle it
    auto param_sql = std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(param));

    if (auto subquery_sql = std::dynamic_pointer_cast<sql::ast::SelectStatement>(param_sql)) {
      // Then the parameter is not a variable
      auto param_subquery = std::make_shared<sql::ast::Source>(subquery_sql, GenerateTableAlias());
      GetNode(param)->sql_expression = param_subquery;

      non_var_params.push_back({param, i + 1});
    }
  }

  return std::make_pair(var_params, non_var_params);
}

std::unordered_set<Bound> SQLVisitor::SafeFunction(psr::BindingInnerContext* binding_ctx,
                                                   antlr4::ParserRuleContext* expr_ctx) {
  /*
   * Computes the safe function from the paper.
   */

  std::unordered_set<Bound> safe_result;

  std::set<std::string> binding_vars(GetNode(binding_ctx)->variables);

  for (auto binding : binding_ctx->binding()) {
    if (!binding->id_domain) continue;

    int domain_arity = ast_->GetArity(binding->id_domain->getText());

    auto table_source = TableSource(binding->id_domain->getText(), domain_arity);
    auto projection_table = Projection(table_source);

    auto bindings_bound = Bound({binding->id->getText()}, {projection_table});
    safe_result.insert(bindings_bound);

    binding_vars.erase(binding->id->getText());
  }

  if (binding_vars.empty()) {
    return safe_result;
  }

  auto safeness = GetNode(expr_ctx)->safety;

  for (auto [vars, union_domain] : safeness.bounds) {
    for (auto var : vars) {
      if (binding_vars.find(var) != binding_vars.end()) {
        binding_vars.erase(var);
      }
    }
  }

  if (binding_vars.empty()) {
    safe_result.insert(safeness.bounds.begin(), safeness.bounds.end());
    return safe_result;
  }

  throw SemanticException("Not all variables are bound", ErrorCode::UNBALANCED_VARIABLE);
}

std::pair<std::vector<std::shared_ptr<sql::ast::Source>>, std::vector<std::shared_ptr<sql::ast::Source>>>
SQLVisitor::CollectReferencedStoredCTEs(const std::unordered_set<Bound>& safe_result) {
  /*
   * Collects stored CTEs from full_appl_ctes_ that are referenced in safe_result.
   * These CTEs are needed first (before binding CTEs that may reference them).
   * Matching is done by checking if GenericSource's sourceable matches stored CTE's sourceable.
   * Also extracts nested CTEs from stored CTEs and returns them separately.
   * Returns a pair: (extracted nested CTEs, stored CTEs with cleaned sourceables)
   */
  std::vector<std::shared_ptr<sql::ast::Source>> stored_ctes;
  std::vector<std::shared_ptr<sql::ast::Source>> extracted_ctes;
  std::unordered_set<std::shared_ptr<sql::ast::Source>> seen_ctes;

  for (const auto& bound : safe_result) {
    for (const auto& projection : bound.domain) {
      auto bound_source = ResolvePromisedSource(projection.source);
      auto generic_bound = std::dynamic_pointer_cast<GenericSource>(bound_source);
      if (!generic_bound) {
        continue;
      }

      // Check if this GenericSource matches any stored CTE
      for (const auto& [stored_proj, stored_cte] : full_appl_ctes_) {
        if (stored_proj.projected_indices == projection.projected_indices &&
            stored_cte->sourceable == generic_bound->source) {
          // Add CTE if not already seen
          if (seen_ctes.insert(stored_cte).second) {
            // Check if the stored CTE's sourceable has nested CTEs
            auto nested_select = std::dynamic_pointer_cast<sql::ast::SelectStatement>(stored_cte->sourceable);
            if (nested_select && !nested_select->ctes.empty() && !nested_select->ctes_are_recursive) {
              // Extract nested CTEs
              extracted_ctes.insert(extracted_ctes.end(), nested_select->ctes.begin(), nested_select->ctes.end());

              // Create a new SelectStatement without CTEs
              std::shared_ptr<sql::ast::Sourceable> cleaned_sourceable;
              if (nested_select->group_by.has_value() && nested_select->from.has_value()) {
                cleaned_sourceable = std::make_shared<sql::ast::SelectStatement>(
                    nested_select->columns, nested_select->from.value(), nested_select->group_by.value(),
                    nested_select->is_distinct);
              } else if (nested_select->from.has_value()) {
                cleaned_sourceable = std::make_shared<sql::ast::SelectStatement>(
                    nested_select->columns, nested_select->from.value(), nested_select->is_distinct);
              } else {
                cleaned_sourceable =
                    std::make_shared<sql::ast::SelectStatement>(nested_select->columns, nested_select->is_distinct);
              }

              // Create a new Source with the cleaned sourceable
              std::shared_ptr<sql::ast::Source> cleaned_cte;
              if (stored_cte->alias.has_value()) {
                cleaned_cte = std::make_shared<sql::ast::Source>(cleaned_sourceable, stored_cte->alias.value(), true,
                                                                 stored_cte->def_columns);
              } else {
                cleaned_cte = std::make_shared<sql::ast::Source>(cleaned_sourceable, GenerateTableAlias(), true,
                                                                 stored_cte->def_columns);
              }
              stored_ctes.push_back(cleaned_cte);
            } else {
              // No nested CTEs, use the stored CTE as-is
              stored_ctes.push_back(stored_cte);
            }
          }
          // If already seen, skip (already added to stored_ctes)
          break;  // Found matching CTE for this projection
        }
      }
    }
  }

  return std::make_pair(extracted_ctes, stored_ctes);
}

std::unordered_map<Bound, std::shared_ptr<sql::ast::Source>> SQLVisitor::ComputeBindingsCTEs(
    std::unordered_set<Bound>& safe_result) {
  /*
   * Computes the CTEs for the bindings. Associates each safe result with the CTE that it generates for after use.
   * Also collects stored CTEs from full_appl_ctes_ that are used, which should be included in higher-level queries.
   */

  std::unordered_map<Bound, std::shared_ptr<sql::ast::Source>> cte_map;

  for (auto& elem : safe_result) {
    if (elem.domain.empty()) {
      throw std::runtime_error("Empty union domain");
    }

    // Build selects for each domain
    std::vector<std::shared_ptr<sql::ast::Sourceable>> selects;
    for (auto projection : elem.domain) {
      // Create table with attribute names from EDBInfo (whether custom or standard A1, A2, etc.)

      auto bound_source = ResolvePromisedSource(projection.source);
      auto table_bound = std::dynamic_pointer_cast<TableSource>(bound_source);
      auto constant_bound = std::dynamic_pointer_cast<ConstantSource>(bound_source);
      auto generic_bound = std::dynamic_pointer_cast<GenericSource>(bound_source);

      std::shared_ptr<sql::ast::SelectStatement> select;
      bool select_created = false;

      if (!constant_bound && !table_bound && !generic_bound) {
        throw std::runtime_error("Unknown domain source type");
      } else if (generic_bound) {
        // Check if there's a stored CTE for this projection (from a Full Application with relAbs)
        // We need to match by sourceable and projected_indices since the projection may have been resolved
        std::shared_ptr<sql::ast::Source> matching_cte = nullptr;
        for (const auto& [stored_proj, stored_cte] : full_appl_ctes_) {
          // Check if projected_indices match and if the GenericSource's sourceable matches the stored CTE's sourceable
          if (stored_proj.projected_indices == projection.projected_indices &&
              stored_cte->sourceable == generic_bound->source) {
            matching_cte = stored_cte;
            break;
          }
        }

        if (matching_cte) {
          // Create a SELECT that references the stored CTE in its FROM clause
          // The stored CTE is already a complete Source with is_cte=true
          auto from = std::make_shared<sql::ast::FromStatement>(matching_cte);

          std::vector<std::shared_ptr<sql::ast::Selectable>> columns;
          if (projection.projected_indices.size() == generic_bound->arity) {
            // All columns selected - use wildcard
            auto wildcard = std::make_shared<sql::ast::Wildcard>();
            columns.push_back(wildcard);
          } else {
            // Select specific columns based on projected_indices
            for (int index : projection.projected_indices) {
              std::string column_name = GetColumnNameFromSource(matching_cte, index);
              auto column = std::make_shared<sql::ast::Column>(column_name, matching_cte);
              auto term_selectable = std::make_shared<sql::ast::TermSelectable>(column, fmt::format("A{}", index + 1));
              columns.push_back(term_selectable);
            }
          }

          select = std::make_shared<sql::ast::SelectStatement>(columns, from);
          select_created = true;
        } else {
          // No stored CTE, proceed with normal GenericSource handling
          auto sourceable = generic_bound->source;
          auto alias = std::make_shared<sql::ast::AliasStatement>(GenerateTableAlias());
          auto source = std::make_shared<sql::ast::Source>(sourceable, alias);
          auto from = std::make_shared<sql::ast::FromStatement>(source);

          std::vector<std::shared_ptr<sql::ast::Selectable>> columns;
          if (projection.projected_indices.size() == generic_bound->arity) {
            auto wildcard = std::make_shared<sql::ast::Wildcard>();
            columns.push_back(wildcard);
          } else {
            for (int index : projection.projected_indices) {
              std::string column_name = GetColumnNameFromSource(source, index);
              auto column = std::make_shared<sql::ast::Column>(column_name, source);
              auto term_selectable = std::make_shared<sql::ast::TermSelectable>(column, fmt::format("A{}", index + 1));
              columns.push_back(term_selectable);
            }
          }

          select = std::make_shared<sql::ast::SelectStatement>(columns, from);
          select_created = true;
        }
      } else if (constant_bound) {
        auto sql_constant = std::make_shared<sql::ast::Constant>(constant_bound->value);
        auto selectable = std::make_shared<sql::ast::TermSelectable>(sql_constant, "A1");

        select =
            std::make_shared<sql::ast::SelectStatement>(std::vector<std::shared_ptr<sql::ast::Selectable>>{selectable});
        select_created = true;

      } else {
        auto edb_info = ast_->GetRelationInfo(table_bound->table_name);
        std::shared_ptr<sql::ast::Table> table;
        if (edb_info && edb_info->arity > 0) {
          std::vector<std::string> attribute_names;
          for (int i = 0; i < edb_info->arity; ++i) {
            attribute_names.push_back(edb_info->AttributeName(i));
          }
          table = std::make_shared<sql::ast::Table>(table_bound->table_name, edb_info->arity, attribute_names);
        } else {
          table = std::make_shared<sql::ast::Table>(table_bound->table_name, ast_->GetArity(table_bound->table_name));
        }

        auto alias = std::make_shared<sql::ast::AliasStatement>(GenerateTableAlias());

        auto source = std::make_shared<sql::ast::Source>(table, alias);
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
            auto term_selectable = std::make_shared<sql::ast::TermSelectable>(column, fmt::format("A{}", index + 1));
            columns.push_back(term_selectable);
          }
        }
        select = std::make_shared<sql::ast::SelectStatement>(columns, from);
        select_created = true;
      }

      if (select_created) {
        selects.push_back(select);
      }
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
    const std::unordered_map<Bound, std::shared_ptr<sql::ast::Source>>& cte_map,
    psr::BindingInnerContext* binding_ctx) {
  /*
   * Computes the output for the bindings.
   */
  std::vector<std::shared_ptr<sql::ast::Selectable>> output;

  // Create a function that resolves the CTE for a given binding variable. This is used to avoid
  // recomputing the CTE for the same binding variable multiple times.
  auto resolve_binding_cte = MakeBindingCTEResolver(cte_map);

  for (auto binding : binding_ctx->binding()) {
    auto binding_var = binding->id->getText();
    auto binding_cte = resolve_binding_cte(binding_var);
    auto column = std::make_shared<sql::ast::Column>(binding_var, binding_cte);
    auto selectable = std::make_shared<sql::ast::TermSelectable>(column, fmt::format("A{}", output.size() + 1));
    output.push_back(selectable);
  }

  return output;
}

std::function<std::shared_ptr<sql::ast::Source>(const std::string&)> SQLVisitor::MakeBindingCTEResolver(
    const std::unordered_map<Bound, std::shared_ptr<sql::ast::Source>>& cte_map) const {
  std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>> seen_vars;

  auto resolver = [&cte_map, seen_vars](const std::string& binding_var) mutable -> std::shared_ptr<sql::ast::Source> {
    if (auto cached = seen_vars.find(binding_var); cached != seen_vars.end()) {
      return cached->second;
    }

    for (auto const& [binding_bound, cte] : cte_map) {
      for (auto const& bounded_var : binding_bound.variables) {
        if (bounded_var == binding_var) {
          seen_vars.emplace(binding_var, cte);
          return cte;
        }
      }
    }

    throw InternalException(fmt::format("Binding CTE not found for {}", binding_var));
  };

  return resolver;
}

std::shared_ptr<sql::ast::Condition> SQLVisitor::BindingsEqualityShorthand(
    antlr4::ParserRuleContext* expr, const std::unordered_map<Bound, std::shared_ptr<sql::ast::Source>>& safe_result) {
  /*
   * Generates a condition that equates all the repeated variables in the input map. This is the EQ function
   * in the paper.
   */

  auto expr_free_vars = GetNode(expr)->free_variables;

  auto expr_source = std::dynamic_pointer_cast<sql::ast::Source>(GetNode(expr)->sql_expression);

  std::unordered_map<std::string, std::vector<std::shared_ptr<sql::ast::Source>>> repetition_map;

  std::vector<std::shared_ptr<sql::ast::Condition>> conditions;

  for (auto const& [bound, cte] : safe_result) {
    auto const& [vars, union_domain] = bound;
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

  std::shared_ptr<sql::ast::Condition> condition;

  if (conditions.size() == 1) {
    condition = conditions[0];
  } else {
    condition = std::make_shared<sql::ast::LogicalCondition>(conditions, sql::ast::LogicalOp::AND);
  }

  return condition;
}

std::pair<std::shared_ptr<sql::ast::Source>, std::vector<std::shared_ptr<sql::ast::Source>>>
SQLVisitor::CreateRecursiveCTEFromFormula(std::shared_ptr<sql::ast::Sourceable> formula_sql,
                                          const std::string& recursive_definition_name, int arity) {
  /*
   * Creates a recursive CTE from a formula with A1, A2, etc. as column names.
   * Returns the recursive CTE source and any CTEs from the formula.
   */
  std::string recursive_alias = GenerateTableAlias("R");

  // Update table names in the recursive definition
  sql::ast::TableNameUpdater updater(recursive_definition_name, recursive_alias);
  formula_sql->Accept(updater);

  // Extract CTEs from the formula (if it's a SelectStatement)
  std::vector<std::shared_ptr<sql::ast::Source>> formula_ctes;
  auto select_stmt = std::dynamic_pointer_cast<sql::ast::SelectStatement>(formula_sql);
  if (select_stmt) {
    formula_ctes.insert(formula_ctes.end(), select_stmt->ctes.begin(), select_stmt->ctes.end());
    select_stmt->ctes.clear();
  }

  // Create recursive CTE R0 with columns named A1, A2, etc. (not free variables)
  std::vector<std::string> def_columns;
  for (int i = 1; i <= arity; i++) {
    def_columns.push_back(fmt::format("A{}", i));
  }

  auto recursive_source = std::make_shared<sql::ast::Source>(formula_sql, recursive_alias, true, def_columns);

  return std::make_pair(recursive_source, formula_ctes);
}

}  // namespace rel2sql

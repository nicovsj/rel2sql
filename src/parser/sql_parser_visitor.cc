#include "sql_parser_visitor.h"

#include "sql_ast/sql_ast.h"
#include "support/exceptions.h"

namespace rel2sql {

SqlParserVisitor::~SqlParserVisitor() = default;

std::any SqlParserVisitor::visitStatements(psr::StatementsContext* ctx) {
  std::vector<std::shared_ptr<sql::ast::Expression>> statements;

  for (auto* stmt_ctx : ctx->statement()) {
    auto expr = std::any_cast<std::shared_ptr<sql::ast::Expression>>(visit(stmt_ctx));
    statements.push_back(expr);
  }

  if (statements.size() == 1) {
    return statements[0];
  }

  return std::make_shared<sql::ast::MultipleStatements>(statements);
}

std::any SqlParserVisitor::visitStatement(psr::StatementContext* ctx) {
  if (ctx->select()) {
    auto select = std::any_cast<std::shared_ptr<sql::ast::Select>>(visit(ctx->select()));
    return std::static_pointer_cast<sql::ast::Expression>(select);
  } else if (ctx->values()) {
    auto values_expr = std::any_cast<std::shared_ptr<sql::ast::Values>>(visit(ctx->values()));
    return std::static_pointer_cast<sql::ast::Expression>(values_expr);
  } else if (ctx->unionClause()) {
    // Union clause visitor will return a Sourceable, cast to Expression
    auto union_expr = std::any_cast<std::shared_ptr<sql::ast::Sourceable>>(visit(ctx->unionClause()));
    return std::static_pointer_cast<sql::ast::Expression>(union_expr);
  } else if (ctx->createView()) {
    return visit(ctx->createView());
  } else if (ctx->createTable()) {
    return visit(ctx->createTable());
  }
  throw InternalException("Unknown SQL statement type");
}

std::any SqlParserVisitor::visitSelect(psr::SelectContext* ctx) {
  // Push new per-SELECT scope frame
  select_stack_.emplace_back();

  // Handle WITH clause (CTEs)
  std::vector<std::shared_ptr<sql::ast::Source>> ctes;
  bool with_recursive_ctes = false;
  if (ctx->with()) {
    auto ctes_result = visit(ctx->with());
    ctes = std::any_cast<std::vector<std::shared_ptr<sql::ast::Source>>>(ctes_result);
    with_recursive_ctes = ctx->with()->RECURSIVE() != nullptr;
  }

  // Handle DISTINCT
  bool is_distinct = ctx->DISTINCT() != nullptr;

  // Get select list - selectList is not an alternative, so we iterate over selectItem directly
  std::vector<std::shared_ptr<sql::ast::Selectable>> columns;
  for (auto* select_item_ctx : ctx->selectList()->selectItem()) {
    auto selectable = std::any_cast<std::shared_ptr<sql::ast::Selectable>>(visit(select_item_ctx));
    columns.push_back(selectable);
  }

  // Handle FROM clause and WHERE clause together
  std::optional<std::shared_ptr<sql::ast::From>> from;
  if (ctx->from()) {
    auto from_result = visit(ctx->from());
    from = std::any_cast<std::shared_ptr<sql::ast::From>>(from_result);

    // WHERE clause is part of the FROM clause in the grammar
    if (ctx->where()) {
      auto where_result = visit(ctx->where());
      auto where_cond = std::any_cast<std::shared_ptr<sql::ast::Condition>>(where_result);
      // Rebuild FROM with WHERE
      from = std::make_shared<sql::ast::From>(from.value()->sources, where_cond);
    }
  } else if (ctx->where()) {
    throw ParseException("WHERE clause requires FROM clause");
  }

  // Handle GROUP BY
  std::optional<std::shared_ptr<sql::ast::GroupBy>> group_by;
  if (ctx->groupBy()) {
    auto group_by_result = visit(ctx->groupBy());
    group_by = std::any_cast<std::shared_ptr<sql::ast::GroupBy>>(group_by_result);
  }

  // If we have CTEs but no FROM clause, we need to resolve pending references here
  // since visitFrom won't be called to do it
  if (!ctx->from()) {
    ResolvePendingReferences();
  }

  // Create Select
  // Handle CTEs without FROM clause (SQL allows this, but we need an empty FROM)
  std::shared_ptr<sql::ast::From> effective_from = nullptr;
  if (!ctes.empty() && !from.has_value()) {
    effective_from = std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{});
  } else if (from.has_value()) {
    effective_from = from.value();
  }

  // Build Select with appropriate constructor
  std::shared_ptr<sql::ast::Select> result;
  if (!ctes.empty()) {
    result =
        std::make_shared<sql::ast::Select>(columns, effective_from, ctes, is_distinct, with_recursive_ctes);
  } else if (group_by.has_value()) {
    result = std::make_shared<sql::ast::Select>(columns, effective_from, group_by.value(), is_distinct);
  } else if (from.has_value()) {
    result = std::make_shared<sql::ast::Select>(columns, from.value(), is_distinct);
  } else {
    result = std::make_shared<sql::ast::Select>(columns, is_distinct);
  }

  // Pop per-SELECT scope frame (avoid leaking inner state)
  select_stack_.pop_back();

  return result;
}

std::any SqlParserVisitor::visitSelectList(psr::SelectListContext* ctx) {
  std::vector<std::shared_ptr<sql::ast::Selectable>> selectables;

  for (auto* item_ctx : ctx->selectItem()) {
    auto selectable = std::any_cast<std::shared_ptr<sql::ast::Selectable>>(visit(item_ctx));
    selectables.push_back(selectable);
  }

  return selectables;
}

std::any SqlParserVisitor::visitSelectTerm(psr::SelectTermContext* ctx) {
  auto term_result = visit(ctx->term());
  auto term = std::any_cast<std::shared_ptr<sql::ast::Term>>(term_result);

  std::shared_ptr<sql::ast::TermSelectable> term_selectable;

  if (ctx->alias) {
    term_selectable = std::make_shared<sql::ast::TermSelectable>(term, ctx->alias->getText());
  } else {
    term_selectable = std::make_shared<sql::ast::TermSelectable>(term);
  }

  return std::static_pointer_cast<sql::ast::Selectable>(term_selectable);
}

std::any SqlParserVisitor::visitSelectWildcard(psr::SelectWildcardContext* ctx) {
  auto wildcard = std::any_cast<std::shared_ptr<sql::ast::Wildcard>>(visit(ctx->wildcard()));
  return std::static_pointer_cast<sql::ast::Selectable>(wildcard);
}

std::any SqlParserVisitor::visitSimpleWildcard(psr::SimpleWildcardContext* _) {
  return std::make_shared<sql::ast::Wildcard>();
}

std::any SqlParserVisitor::visitQualifiedWildcard(psr::QualifiedWildcardContext* ctx) {
  std::string alias = ctx->tableAlias->getText();
  auto source = FindSourceByAlias(alias);

  // If source not found yet, defer resolution (for CTEs referenced before FROM)
  std::shared_ptr<sql::ast::Wildcard> wildcard;
  if (!source) {
    wildcard = std::make_shared<sql::ast::Wildcard>();
    if (select_stack_.empty()) select_stack_.emplace_back();
    select_stack_.back().pending_wildcard_refs.push_back({wildcard, alias});
  } else {
    wildcard = std::make_shared<sql::ast::Wildcard>(source);
  }

  return wildcard;
}

std::any SqlParserVisitor::visitFrom(psr::FromContext* ctx) {
  std::vector<std::shared_ptr<sql::ast::Source>> sources;

  for (auto* source_ctx : ctx->source()) {
    auto source_result = visit(source_ctx);
    auto source = std::any_cast<std::shared_ptr<sql::ast::Source>>(source_result);
    sources.push_back(source);

    // Register alias in map
    if (source->alias.has_value()) {
      if (select_stack_.empty()) select_stack_.emplace_back();
      select_stack_.back().alias_map[source->alias.value()->Access()] = source;
    } else if (auto table = std::dynamic_pointer_cast<sql::ast::Table>(source->sourceable)) {
      if (select_stack_.empty()) select_stack_.emplace_back();
      select_stack_.back().alias_map[table->name] = source;
    }
  }

  // Now that all sources are registered, resolve any pending references
  ResolvePendingReferences();

  // Note: WHERE clause is handled separately in visitSelect
  // to match the AST structure where WHERE is part of From
  return std::make_shared<sql::ast::From>(sources);
}

std::any SqlParserVisitor::visitTableSource(psr::TableSourceContext* ctx) {
  std::string table_name = ctx->tableName->getText();

  int arity = 0;
  std::vector<std::string> attribute_names;
  // Try to look up table info in EDB map for better schema information
  auto edb_it = edb_map_.map.find(table_name);
  if (edb_it != edb_map_.map.end()) {
    arity = edb_it->second.arity;
    attribute_names = edb_it->second.attribute_names;
  }
  auto table = std::make_shared<sql::ast::Table>(table_name, arity, attribute_names);

  if (ctx->alias) {
    return std::make_shared<sql::ast::Source>(table, ctx->alias->getText());
  }

  return std::make_shared<sql::ast::Source>(table);
}

std::any SqlParserVisitor::visitSubquerySource(psr::SubquerySourceContext* ctx) {
  auto select_result = visit(ctx->select());
  auto select = std::any_cast<std::shared_ptr<sql::ast::Select>>(select_result);
  auto selectable = std::static_pointer_cast<sql::ast::Sourceable>(select);

  if (ctx->sourceAlias()) {
    auto alias_result = visit(ctx->sourceAlias());
    auto alias_info = std::any_cast<std::pair<std::string, std::vector<std::string>>>(alias_result);
    std::string alias_name = alias_info.first;
    std::vector<std::string> def_columns = alias_info.second;

    return std::make_shared<sql::ast::Source>(selectable, alias_name, false, def_columns);
  }

  return std::make_shared<sql::ast::Source>(selectable);
}

std::any SqlParserVisitor::visitValuesSource(psr::ValuesSourceContext* ctx) {
  auto values_result = visit(ctx->values());
  auto values = std::any_cast<std::shared_ptr<sql::ast::Values>>(values_result);

  if (ctx->sourceAlias()) {
    auto alias_result = visit(ctx->sourceAlias());
    auto alias_info = std::any_cast<std::pair<std::string, std::vector<std::string>>>(alias_result);
    std::string alias_name = alias_info.first;
    std::vector<std::string> def_columns = alias_info.second;

    return std::make_shared<sql::ast::Source>(values, alias_name, false, def_columns);
  }

  return std::make_shared<sql::ast::Source>(values);
}

std::any SqlParserVisitor::visitSourceAlias(psr::SourceAliasContext* ctx) {
  std::string alias_name = ctx->alias->getText();

  // Handle column list if present
  std::vector<std::string> def_columns;
  if (ctx->columnList()) {
    for (auto* col_ctx : ctx->columnList()->IDENTIFIER()) {
      def_columns.push_back(col_ctx->getText());
    }
  }

  return std::make_pair(alias_name, def_columns);
}

std::any SqlParserVisitor::visitWhere(psr::WhereContext* ctx) { return visit(ctx->condition()); }

std::any SqlParserVisitor::visitGroupBy(psr::GroupByContext* ctx) {
  std::vector<std::shared_ptr<sql::ast::Selectable>> columns;

  for (auto* item_ctx : ctx->groupByItem()) {
    if (item_ctx->term()) {
      auto term_result = visit(item_ctx->term());
      auto term = std::any_cast<std::shared_ptr<sql::ast::Term>>(term_result);
      columns.push_back(std::make_shared<sql::ast::TermSelectable>(term));
    } else if (item_ctx->wildcard()) {
      auto wildcard_result = visit(item_ctx->wildcard());
      columns.push_back(std::any_cast<std::shared_ptr<sql::ast::Selectable>>(wildcard_result));
    }
  }

  return std::make_shared<sql::ast::GroupBy>(columns);
}

std::any SqlParserVisitor::visitUnionAll(psr::UnionAllContext* ctx) {
  std::vector<std::shared_ptr<sql::ast::Select>> members;

  // First SELECT
  auto first_result = visit(ctx->select(0));
  auto first = std::any_cast<std::shared_ptr<sql::ast::Select>>(first_result);
  members.push_back(first);

  // Remaining SELECTs
  for (size_t i = 1; i < ctx->select().size(); i++) {
    auto select_result = visit(ctx->select(i));
    auto select = std::any_cast<std::shared_ptr<sql::ast::Select>>(select_result);
    members.push_back(select);
  }

  auto union_all = std::make_shared<sql::ast::UnionAll>(members);
  return std::static_pointer_cast<sql::ast::Sourceable>(union_all);
}

std::any SqlParserVisitor::visitUnionSimple(psr::UnionSimpleContext* ctx) {
  std::vector<std::shared_ptr<sql::ast::Sourceable>> members;

  // First SELECT
  auto first_result = visit(ctx->select(0));
  auto first = std::any_cast<std::shared_ptr<sql::ast::Select>>(first_result);
  members.push_back(std::static_pointer_cast<sql::ast::Sourceable>(first));

  // Remaining SELECTs
  for (size_t i = 1; i < ctx->select().size(); i++) {
    auto select_result = visit(ctx->select(i));
    auto select = std::any_cast<std::shared_ptr<sql::ast::Select>>(select_result);
    members.push_back(std::static_pointer_cast<sql::ast::Sourceable>(select));
  }

  auto union_expr = std::make_shared<sql::ast::Union>(members);
  return std::static_pointer_cast<sql::ast::Sourceable>(union_expr);
}

std::any SqlParserVisitor::visitValues(psr::ValuesContext* ctx) {
  std::vector<std::vector<sql::ast::Constant>> values;

  for (auto* row_ctx : ctx->valueRow()) {
    std::vector<sql::ast::Constant> row;
    for (auto* const_ctx : row_ctx->constant()) {
      auto const_result = visit(const_ctx);
      auto constant = std::any_cast<std::shared_ptr<sql::ast::Constant>>(const_result);
      row.push_back(*constant);
    }
    values.push_back(row);
  }

  return std::make_shared<sql::ast::Values>(values);
}

std::any SqlParserVisitor::visitWith(psr::WithContext* ctx) {
  std::vector<std::shared_ptr<sql::ast::Source>> ctes;

  for (auto* cte_ctx : ctx->cte()) {
    auto cte_result = visit(cte_ctx);
    auto cte_source = std::any_cast<std::shared_ptr<sql::ast::Source>>(cte_result);
    ctes.push_back(cte_source);

    // Register CTE alias
    if (cte_source->alias.has_value()) {
      if (select_stack_.empty()) select_stack_.emplace_back();
      select_stack_.back().alias_map[cte_source->alias.value()->Access()] = cte_source;
    }
  }

  return ctes;
}

std::any SqlParserVisitor::visitCte(psr::CteContext* ctx) {
  std::string cte_name = ctx->cteName->getText();

  // Get the SELECT statement
  auto select = std::any_cast<std::shared_ptr<sql::ast::Select>>(visit(ctx->select()));
  auto sourceable = std::static_pointer_cast<sql::ast::Sourceable>(select);

  // Handle column list if present
  std::vector<std::string> def_columns;
  if (ctx->columnList()) {
    for (auto* col_ctx : ctx->columnList()->IDENTIFIER()) {
      def_columns.push_back(col_ctx->getText());
    }
  }

  auto source = std::make_shared<sql::ast::Source>(sourceable, cte_name, true, def_columns);

  return source;
}

std::any SqlParserVisitor::visitNotCondition(psr::NotConditionContext* ctx) {
  auto cond_result = visit(ctx->condition());
  auto condition = std::any_cast<std::shared_ptr<sql::ast::Condition>>(cond_result);

  auto logical_condition = std::make_shared<sql::ast::LogicalCondition>(
      std::vector<std::shared_ptr<sql::ast::Condition>>{condition}, sql::ast::LogicalOp::NOT);
  return std::static_pointer_cast<sql::ast::Condition>(logical_condition);
}

std::any SqlParserVisitor::visitAndCondition(psr::AndConditionContext* ctx) {
  std::vector<std::shared_ptr<sql::ast::Condition>> conditions;

  auto left_result = visit(ctx->condition(0));
  conditions.push_back(std::any_cast<std::shared_ptr<sql::ast::Condition>>(left_result));

  auto right_result = visit(ctx->condition(1));
  conditions.push_back(std::any_cast<std::shared_ptr<sql::ast::Condition>>(right_result));

  auto logical_condition = std::make_shared<sql::ast::LogicalCondition>(conditions, sql::ast::LogicalOp::AND);
  return std::static_pointer_cast<sql::ast::Condition>(logical_condition);
}

std::any SqlParserVisitor::visitOrCondition(psr::OrConditionContext* ctx) {
  std::vector<std::shared_ptr<sql::ast::Condition>> conditions;

  auto left_result = visit(ctx->condition(0));
  conditions.push_back(std::any_cast<std::shared_ptr<sql::ast::Condition>>(left_result));

  auto right_result = visit(ctx->condition(1));
  conditions.push_back(std::any_cast<std::shared_ptr<sql::ast::Condition>>(right_result));

  auto logical_condition = std::make_shared<sql::ast::LogicalCondition>(conditions, sql::ast::LogicalOp::OR);
  return std::static_pointer_cast<sql::ast::Condition>(logical_condition);
}

std::any SqlParserVisitor::visitComparisonCondition(psr::ComparisonConditionContext* ctx) {
  auto lhs_result = visit(ctx->term(0));
  auto lhs = std::any_cast<std::shared_ptr<sql::ast::Term>>(lhs_result);

  auto op = ParseComparisonOp(ctx->comparisonOp());

  auto rhs_result = visit(ctx->term(1));
  auto rhs = std::any_cast<std::shared_ptr<sql::ast::Term>>(rhs_result);

  auto comparison_condition = std::make_shared<sql::ast::ComparisonCondition>(lhs, op, rhs);
  return std::static_pointer_cast<sql::ast::Condition>(comparison_condition);
}

std::any SqlParserVisitor::visitExistsCondition(psr::ExistsConditionContext* ctx) {
  auto select_result = visit(ctx->select());
  auto select = std::any_cast<std::shared_ptr<sql::ast::Select>>(select_result);

  auto exists_condition = std::make_shared<sql::ast::Exists>(select);
  return std::static_pointer_cast<sql::ast::Condition>(exists_condition);
}

std::any SqlParserVisitor::visitInclusionCondition(psr::InclusionConditionContext* ctx) {
  auto inclusion = std::any_cast<std::shared_ptr<sql::ast::Inclusion>>(visit(ctx->inclusion()));
  return std::static_pointer_cast<sql::ast::Condition>(inclusion);
}

std::any SqlParserVisitor::visitInclusion(psr::InclusionContext* ctx) {
  std::vector<std::shared_ptr<sql::ast::Column>> columns;
  bool is_not = ctx->NOT() != nullptr;

  // Parse columns - can be single term or parenthesized list
  if (ctx->term()) {
    // Single column
    auto term_result = visit(ctx->term());
    auto term = std::any_cast<std::shared_ptr<sql::ast::Term>>(term_result);
    auto column = std::dynamic_pointer_cast<sql::ast::Column>(term);
    if (!column) {
      throw ParseException("IN condition requires a column reference");
    }
    columns.push_back(column);
  } else if (ctx->columnRef().size() > 0) {
    // Multiple columns in parentheses
    for (auto* col_ctx : ctx->columnRef()) {
      auto col_result = visit(col_ctx);
      auto col = std::any_cast<std::shared_ptr<sql::ast::Column>>(col_result);
      columns.push_back(col);
    }
  }

  auto select_result = visit(ctx->select());
  auto select = std::any_cast<std::shared_ptr<sql::ast::Select>>(select_result);

  return std::make_shared<sql::ast::Inclusion>(columns, select, is_not);
}

std::any SqlParserVisitor::visitParenCondition(psr::ParenConditionContext* ctx) { return visit(ctx->condition()); }

std::any SqlParserVisitor::visitConstantTerm(psr::ConstantTermContext* ctx) {
  auto constant_expr = std::any_cast<std::shared_ptr<sql::ast::Constant>>(visit(ctx->constant()));
  return std::static_pointer_cast<sql::ast::Term>(constant_expr);
}

std::any SqlParserVisitor::visitColumnTerm(psr::ColumnTermContext* ctx) {
  auto column_expr = std::any_cast<std::shared_ptr<sql::ast::Column>>(visit(ctx->columnRef()));
  return std::static_pointer_cast<sql::ast::Term>(column_expr);
}

std::any SqlParserVisitor::visitFunctionTerm(psr::FunctionTermContext* ctx) {
  auto function_expr = std::any_cast<std::shared_ptr<sql::ast::Function>>(visit(ctx->aggregateFunction()));
  return std::static_pointer_cast<sql::ast::Term>(function_expr);
}

std::any SqlParserVisitor::visitCaseTerm(psr::CaseTermContext* ctx) {
  auto case_when_expr = std::any_cast<std::shared_ptr<sql::ast::CaseWhen>>(visit(ctx->caseWhenExpr()));
  return std::static_pointer_cast<sql::ast::Term>(case_when_expr);
}

std::any SqlParserVisitor::visitMultDivTerm(psr::MultDivTermContext* ctx) {
  auto lhs_result = visit(ctx->term(0));
  auto lhs = std::any_cast<std::shared_ptr<sql::ast::Term>>(lhs_result);

  std::string op;
  if (ctx->ASTERISK()) {
    op = "*";
  } else if (ctx->DIV()) {
    op = "/";
  }

  auto rhs_result = visit(ctx->term(1));
  auto rhs = std::any_cast<std::shared_ptr<sql::ast::Term>>(rhs_result);

  auto operation = std::make_shared<sql::ast::Operation>(lhs, rhs, op);
  return std::static_pointer_cast<sql::ast::Term>(operation);
}

std::any SqlParserVisitor::visitAddSubTerm(psr::AddSubTermContext* ctx) {
  auto lhs_result = visit(ctx->term(0));
  auto lhs = std::any_cast<std::shared_ptr<sql::ast::Term>>(lhs_result);

  std::string op;
  if (ctx->PLUS()) {
    op = "+";
  } else if (ctx->MINUS()) {
    op = "-";
  }

  auto rhs_result = visit(ctx->term(1));
  auto rhs = std::any_cast<std::shared_ptr<sql::ast::Term>>(rhs_result);

  auto operation = std::make_shared<sql::ast::Operation>(lhs, rhs, op);
  return std::static_pointer_cast<sql::ast::Term>(operation);
}

std::any SqlParserVisitor::visitParenTerm(psr::ParenTermContext* ctx) { return visit(ctx->term()); }

std::any SqlParserVisitor::visitSubqueryTerm(psr::SubqueryTermContext* ctx) {
  // Scalar subquery - for now, we'll just return the select statement
  // In a full implementation, this might need special handling
  return visit(ctx->select());
}

std::any SqlParserVisitor::visitSimpleColumn(psr::SimpleColumnContext* ctx) {
  std::string name = ctx->columnName->getText();
  return std::make_shared<sql::ast::Column>(name);
}

std::any SqlParserVisitor::visitQualifiedColumn(psr::QualifiedColumnContext* ctx) {
  std::string alias = ctx->tableAlias->getText();
  std::string name = ctx->columnName->getText();

  auto source = FindSourceByAlias(alias);

  // If source not found yet, defer resolution (for CTEs referenced before FROM)
  auto column = std::make_shared<sql::ast::Column>(name);
  if (!source) {
    if (select_stack_.empty()) select_stack_.emplace_back();
    select_stack_.back().pending_column_refs.push_back({column, alias});
  } else {
    column->source = source;
  }

  return column;
}

std::any SqlParserVisitor::visitCountFunction(psr::CountFunctionContext* ctx) {
  std::shared_ptr<sql::ast::Term> arg;

  if (ctx->ASTERISK()) {
    // COUNT(*) - use a special constant or term
    // For now, we'll use a dummy column or constant
    arg = std::make_shared<sql::ast::Constant>(1);
  } else if (ctx->term()) {
    auto term_result = visit(ctx->term());
    arg = std::any_cast<std::shared_ptr<sql::ast::Term>>(term_result);
  }

  return std::make_shared<sql::ast::Function>(sql::ast::AggregateFunction::COUNT, arg);
}

std::any SqlParserVisitor::visitSumFunction(psr::SumFunctionContext* ctx) {
  auto term_result = visit(ctx->term());
  auto arg = std::any_cast<std::shared_ptr<sql::ast::Term>>(term_result);

  return std::make_shared<sql::ast::Function>(sql::ast::AggregateFunction::SUM, arg);
}

std::any SqlParserVisitor::visitAvgFunction(psr::AvgFunctionContext* ctx) {
  auto term_result = visit(ctx->term());
  auto arg = std::any_cast<std::shared_ptr<sql::ast::Term>>(term_result);

  return std::make_shared<sql::ast::Function>(sql::ast::AggregateFunction::AVG, arg);
}

std::any SqlParserVisitor::visitMinFunction(psr::MinFunctionContext* ctx) {
  auto term_result = visit(ctx->term());
  auto arg = std::any_cast<std::shared_ptr<sql::ast::Term>>(term_result);

  return std::make_shared<sql::ast::Function>(sql::ast::AggregateFunction::MIN, arg);
}

std::any SqlParserVisitor::visitMaxFunction(psr::MaxFunctionContext* ctx) {
  auto term_result = visit(ctx->term());
  auto arg = std::any_cast<std::shared_ptr<sql::ast::Term>>(term_result);

  return std::make_shared<sql::ast::Function>(sql::ast::AggregateFunction::MAX, arg);
}

std::any SqlParserVisitor::visitCaseWhenExpr(psr::CaseWhenExprContext* ctx) {
  std::vector<std::pair<std::shared_ptr<sql::ast::Condition>, std::shared_ptr<sql::ast::Term>>> cases;

  for (auto* when_ctx : ctx->whenClause()) {
    auto when_result = visit(when_ctx);
    auto pair =
        std::any_cast<std::pair<std::shared_ptr<sql::ast::Condition>, std::shared_ptr<sql::ast::Term>>>(when_result);
    cases.push_back(pair);
  }

  return std::make_shared<sql::ast::CaseWhen>(cases);
}

std::any SqlParserVisitor::visitWhenClause(psr::WhenClauseContext* ctx) {
  auto cond_result = visit(ctx->condition());
  auto condition = std::any_cast<std::shared_ptr<sql::ast::Condition>>(cond_result);

  auto term_result = visit(ctx->term());
  auto term = std::any_cast<std::shared_ptr<sql::ast::Term>>(term_result);

  return std::make_pair(condition, term);
}

std::any SqlParserVisitor::visitIntConstant(psr::IntConstantContext* ctx) {
  std::string text = ctx->INTEGER_LITERAL()->getText();
  int value = std::stoi(text);
  return std::make_shared<sql::ast::Constant>(value);
}

std::any SqlParserVisitor::visitFloatConstant(psr::FloatConstantContext* ctx) {
  std::string text = ctx->FLOAT_LITERAL()->getText();
  double value = std::stod(text);
  return std::make_shared<sql::ast::Constant>(value);
}

std::any SqlParserVisitor::visitStringConstant(psr::StringConstantContext* ctx) {
  std::string quoted = ctx->STRING_LITERAL()->getText();
  std::string unquoted = UnquoteString(quoted);
  return std::make_shared<sql::ast::Constant>(unquoted);
}

std::any SqlParserVisitor::visitTrueConstant(psr::TrueConstantContext* _) {
  return std::make_shared<sql::ast::Constant>(true);
}

std::any SqlParserVisitor::visitFalseConstant(psr::FalseConstantContext* _) {
  return std::make_shared<sql::ast::Constant>(false);
}

sql::ast::CompOp SqlParserVisitor::ParseComparisonOp(psr::ComparisonOpContext* ctx) {
  if (ctx->EQ()) {
    return sql::ast::CompOp::EQ;
  } else if (ctx->NEQ()) {
    return sql::ast::CompOp::NEQ;
  } else if (ctx->LT()) {
    return sql::ast::CompOp::LT;
  } else if (ctx->GT()) {
    return sql::ast::CompOp::GT;
  } else if (ctx->LTE()) {
    return sql::ast::CompOp::LTE;
  } else if (ctx->GTE()) {
    return sql::ast::CompOp::GTE;
  }
  throw InternalException("Unknown comparison operator");
}

sql::ast::AggregateFunction SqlParserVisitor::ParseAggregateFunctionName(const std::string& name) {
  if (name == "COUNT" || name == "count") {
    return sql::ast::AggregateFunction::COUNT;
  } else if (name == "SUM" || name == "sum") {
    return sql::ast::AggregateFunction::SUM;
  } else if (name == "AVG" || name == "avg") {
    return sql::ast::AggregateFunction::AVG;
  } else if (name == "MIN" || name == "min") {
    return sql::ast::AggregateFunction::MIN;
  } else if (name == "MAX" || name == "max") {
    return sql::ast::AggregateFunction::MAX;
  }
  throw ParseException("Unknown aggregate function: " + name);
}

std::string SqlParserVisitor::UnquoteString(const std::string& quoted) {
  // Remove leading and trailing quotes
  if (quoted.length() >= 2 && quoted[0] == '\'' && quoted[quoted.length() - 1] == '\'') {
    std::string unquoted = quoted.substr(1, quoted.length() - 2);

    // Handle escape sequences (simplified)
    std::string result;
    for (size_t i = 0; i < unquoted.length(); i++) {
      if (unquoted[i] == '\\' && i + 1 < unquoted.length()) {
        switch (unquoted[i + 1]) {
          case 'n':
            result += '\n';
            break;
          case 't':
            result += '\t';
            break;
          case 'r':
            result += '\r';
            break;
          case '\\':
            result += '\\';
            break;
          case '\'':
            result += '\'';
            break;
          default:
            result += unquoted[i + 1];
            break;
        }
        i++;
      } else {
        result += unquoted[i];
      }
    }
    return result;
  }
  return quoted;
}

std::shared_ptr<sql::ast::Source> SqlParserVisitor::FindSourceByAlias(const std::string& alias) {
  if (select_stack_.empty()) {
    return nullptr;
  }
  auto& current_map = select_stack_.back().alias_map;
  auto it = current_map.find(alias);
  if (it != current_map.end()) {
    return it->second;
  }
  return nullptr;
}

void SqlParserVisitor::ResolvePendingReferences() {
  // Resolve pending column references
  if (!select_stack_.empty()) {
    for (auto& [column, alias] : select_stack_.back().pending_column_refs) {
      auto source = FindSourceByAlias(alias);
      if (!source) {
        throw ParseException("Unknown table alias: " + alias);
      }
      column->source = source;
    }
    select_stack_.back().pending_column_refs.clear();

    // Resolve pending wildcard references
    for (auto& [wildcard, alias] : select_stack_.back().pending_wildcard_refs) {
      auto source = FindSourceByAlias(alias);
      if (!source) {
        throw ParseException("Unknown table alias: " + alias);
      }
      wildcard->source = source;
    }
    select_stack_.back().pending_wildcard_refs.clear();
  }
}

std::any SqlParserVisitor::visitCreateView(psr::CreateViewContext* ctx) {
  std::string view_name = ctx->viewName->getText();

  // Get the SELECT statement
  auto select = std::any_cast<std::shared_ptr<sql::ast::Select>>(visit(ctx->select()));
  auto sourceable = std::static_pointer_cast<sql::ast::Sourceable>(select);

  // Handle column list if present
  std::vector<std::string> def_columns;
  if (ctx->columnList()) {
    for (auto* col_ctx : ctx->columnList()->IDENTIFIER()) {
      def_columns.push_back(col_ctx->getText());
    }
  }

  // Create a Source with the view name as alias and column list
  // is_cte is false for views (only true for CTEs)
  auto source = std::make_shared<sql::ast::Source>(sourceable, view_name, false, def_columns);

  // Create View expression
  auto view = std::make_shared<sql::ast::View>(source);
  return std::static_pointer_cast<sql::ast::Expression>(view);
}

std::any SqlParserVisitor::visitCreateTable(psr::CreateTableContext* ctx) {
  std::string table_name = ctx->tableName->getText();

  // Get the SELECT statement
  auto select = std::any_cast<std::shared_ptr<sql::ast::Select>>(visit(ctx->select()));
  auto sourceable = std::static_pointer_cast<sql::ast::Sourceable>(select);

  // Handle column list if present
  std::vector<std::string> def_columns;
  if (ctx->columnList()) {
    for (auto* col_ctx : ctx->columnList()->IDENTIFIER()) {
      def_columns.push_back(col_ctx->getText());
    }
  }

  // Create a Source with the table name as alias and column list
  // is_cte is false for tables (only true for CTEs)
  auto source = std::make_shared<sql::ast::Source>(sourceable, table_name, false, def_columns);

  // Create CreateTable expression
  auto create_table = std::make_shared<sql::ast::CreateTable>(source);
  return std::static_pointer_cast<sql::ast::Expression>(create_table);
}

}  // namespace rel2sql

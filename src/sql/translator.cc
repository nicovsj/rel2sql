#include "sql/translator.h"

#include <fmt/core.h>

#include "optimizer/replacers.h"
#include "rel_ast/rel_ast.h"
#include "sql/aggregate_map.h"
#include "sql_ast/sql_ast.h"
#include "support/exceptions.h"

namespace rel2sql {

std::shared_ptr<sql::ast::Expression> Translator::Translate() {
  auto root = context_.Root();
  Visit(root);
  return root->sql_expression;
}

std::shared_ptr<RelProgram> Translator::Visit(const std::shared_ptr<RelProgram>& node) {
  std::vector<std::shared_ptr<sql::ast::Expression>> exprs;
  for (auto& def : node->defs) {
    if (!def || def->disabled) continue;
    Visit(def);
    if (def->sql_expression) {
      exprs.push_back(def->sql_expression);
    }
  }
  node->sql_expression = std::make_shared<sql::ast::MultipleStatements>(exprs);
  return node;
}

std::shared_ptr<sql::ast::Sourceable> TryGetTopLevelIDSelect(RelUnion* body, Translator* visitor);

std::shared_ptr<RelDef> Translator::Visit(const std::shared_ptr<RelDef>& node) {
  if (!node->body) return node;

  std::shared_ptr<sql::ast::Sourceable> child_sql;
  auto special = TryGetTopLevelIDSelect(node->body.get());
  if (special) {
    child_sql = special;
  } else {
    Visit(node->body);
    child_sql = std::dynamic_pointer_cast<sql::ast::Sourceable>(node->body->sql_expression);
  }

  if (!child_sql) return node;

  ApplyDistinctToDefinitionSelects(child_sql);

  if (node->name != "output") {
    node->sql_expression = std::make_shared<sql::ast::View>(child_sql, node->name);
  } else {
    node->sql_expression = std::static_pointer_cast<sql::ast::Expression>(child_sql);
  }
  return node;
}

std::shared_ptr<sql::ast::Sourceable> Translator::TryGetTopLevelIDSelect(RelUnion* body) {
  if (!body || body->exprs.size() != 1) return nullptr;

  auto expr = body->exprs[0];
  auto id_term = std::dynamic_pointer_cast<RelIDTerm>(expr);
  if (!id_term) return nullptr;

  auto expr_result = GetExpressionFromID(*expr, id_term->id, true);
  return std::dynamic_pointer_cast<sql::ast::Sourceable>(expr_result);
}

std::shared_ptr<sql::ast::Expression> Translator::BuildLiteralRelationAbstractionRel(
    const std::shared_ptr<RelUnion>& node) {
  std::vector<std::shared_ptr<RelExpr>> all_exprs = node->exprs;
  if (all_exprs.empty()) {
    throw std::runtime_error("Relation abstraction with no member");
  }
  size_t arity = all_exprs[0]->arity;
  std::vector<std::vector<sql::ast::constant_t>> values;
  for (auto& expr : all_exprs) {
    if (expr->arity != static_cast<size_t>(arity)) {
      throw std::runtime_error("Inconsistent arity in relation abstraction");
    }
    auto product = std::dynamic_pointer_cast<RelProduct>(expr);
    if (product) {
      std::vector<sql::ast::constant_t> row;
      for (auto& child : product->exprs) {
        if (!child->constant.has_value()) {
          throw std::runtime_error("Special product expression with non-constant member");
        }
        row.push_back(child->constant.value());
      }
      values.push_back(std::move(row));
    } else {
      auto lit = std::dynamic_pointer_cast<RelLiteral>(expr);
      if (lit && lit->constant.has_value()) {
        values.push_back({lit->constant.value()});
      } else {
        throw std::runtime_error("Invalid expression in literal relation abstraction");
      }
    }
  }
  auto values_expr = std::make_shared<sql::ast::Values>(values);
  std::vector<std::string> column_names;
  for (size_t i = 1; i <= arity; ++i) {
    column_names.push_back(fmt::format("A{}", i));
  }
  auto alias = std::make_shared<sql::ast::Alias>(GenerateTableAlias(), column_names);
  auto source = std::make_shared<sql::ast::Source>(values_expr, alias);
  auto from = std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{source});
  auto wildcard = std::make_shared<sql::ast::Wildcard>();
  auto select =
      std::make_shared<sql::ast::Select>(std::vector<std::shared_ptr<sql::ast::Selectable>>{wildcard}, from, true);
  return std::static_pointer_cast<sql::ast::Expression>(select);
}

std::shared_ptr<RelUnion> Translator::Visit(const std::shared_ptr<RelUnion>& node) {
  if (node->has_only_literal_values) {
    node->sql_expression = BuildLiteralRelationAbstractionRel(node);
    return node;
  }

  // VisitRelAbsLogic: single or multiple exprs
  if (node->exprs.empty()) {
    throw std::runtime_error("Relation abstraction with no member");
  }

  Visit(node->exprs[0]);

  auto first_sql = std::dynamic_pointer_cast<sql::ast::Sourceable>(node->exprs[0]->sql_expression);
  if (!first_sql) return node;

  auto first_source = std::make_shared<sql::ast::Source>(first_sql, GenerateTableAlias());
  node->exprs[0]->sql_expression = first_source;

  if (node->exprs.size() == 1) {
    node->sql_expression = std::static_pointer_cast<sql::ast::Expression>(first_sql);
    return node;
  }

  // Multi-expression: CROSS JOIN each expr's subquery with VALUES(1),(2),... and CASE to pick branch per column.
  std::vector<std::shared_ptr<sql::ast::Source>> from_sources;
  from_sources.push_back(first_source);

  std::vector<std::vector<sql::ast::constant_t>> index_values;
  index_values.push_back({1});

  for (size_t i = 1; i < node->exprs.size(); i++) {
    Visit(node->exprs[i]);

    auto child_sql = std::dynamic_pointer_cast<sql::ast::Sourceable>(node->exprs[i]->sql_expression);

    if (!child_sql) {
      throw std::runtime_error("Multi-expression relation abstraction: member did not translate to Sourceable");
    }

    auto child_source = std::make_shared<sql::ast::Source>(child_sql, GenerateTableAlias());
    node->exprs[i]->sql_expression = child_source;
    from_sources.push_back(child_source);
    index_values.push_back({static_cast<int>(i + 1)});
  }

  auto values_expr = std::make_shared<sql::ast::Values>(index_values);
  auto values_alias = std::make_shared<sql::ast::Alias>(GenerateTableAlias("I"), std::vector<std::string>{"i"});
  auto values_source = std::make_shared<sql::ast::Source>(values_expr, values_alias);
  from_sources.push_back(values_source);

  auto index_col = std::make_shared<sql::ast::Column>("i", values_source);
  size_t arity = node->exprs[0]->arity;

  // Like the old VisitRelAbsLogic: EqualityShorthand + VarListShorthand, then CASE for arity columns.
  std::vector<RelNode*> expr_ptrs;
  for (auto& e : node->exprs) expr_ptrs.push_back(e.get());
  auto condition = EqualityShorthandRel(expr_ptrs);

  std::vector<std::pair<RelNode*, std::shared_ptr<sql::ast::Source>>> node_source_pairs;
  for (size_t j = 0; j < node->exprs.size(); j++) {
    node_source_pairs.push_back({node->exprs[j].get(), from_sources[j]});
  }
  auto selects = VarListShorthandRel(node_source_pairs);

  // Arity columns: CASE to pick column from the right branch.
  for (size_t col = 0; col < arity; col++) {
    std::vector<std::pair<std::shared_ptr<sql::ast::Condition>, std::shared_ptr<sql::ast::Term>>> cases;
    for (size_t j = 0; j < node->exprs.size(); j++) {
      auto column = std::make_shared<sql::ast::Column>(fmt::format("A{}", col + 1), from_sources[j]);
      auto comparison =
          std::make_shared<sql::ast::ComparisonCondition>(index_col, sql::ast::CompOp::EQ, static_cast<int>(j + 1));
      cases.push_back({comparison, column});
    }
    auto case_when = std::make_shared<sql::ast::CaseWhen>(cases);
    selects.push_back(std::make_shared<sql::ast::TermSelectable>(case_when, fmt::format("A{}", col + 1)));
  }

  auto from = std::make_shared<sql::ast::From>(from_sources, condition);
  auto select = std::make_shared<sql::ast::Select>(selects, from);
  node->sql_expression = std::static_pointer_cast<sql::ast::Expression>(select);
  return node;
}

std::shared_ptr<RelExpr> Translator::Visit(const std::shared_ptr<RelLiteral>& node) {
  if (!node->constant.has_value()) {
    throw std::runtime_error("Literal expression without constant value");
  }
  auto constant = std::make_shared<sql::ast::Constant>(node->constant.value());
  auto selectable = std::make_shared<sql::ast::TermSelectable>(constant, "A1");
  node->sql_expression =
      std::make_shared<sql::ast::Select>(std::vector<std::shared_ptr<sql::ast::Selectable>>{selectable});
  return node;
}

std::shared_ptr<RelExpr> Translator::Visit(const std::shared_ptr<RelProduct>& node) {
  if (node->has_only_literal_values) {
    std::vector<std::shared_ptr<sql::ast::Selectable>> selects;
    for (auto& expr : node->exprs) {
      Visit(expr);
      if (!expr->constant.has_value()) {
        throw std::runtime_error("Special product expression with non-constant member");
      }
      auto constant = std::make_shared<sql::ast::Constant>(expr->constant.value());
      selects.push_back(std::make_shared<sql::ast::TermSelectable>(constant));
    }
    node->sql_expression = std::make_shared<sql::ast::Select>(selects);
    return node;
  }

  // Product of relations: CROSS JOIN with equality on repeated variables (EqualityShorthandRel).
  std::vector<std::shared_ptr<sql::ast::Source>> from_sources;
  std::vector<std::pair<RelNode*, std::shared_ptr<sql::ast::Source>>> node_source_pairs;

  for (auto& expr : node->exprs) {
    if (!expr) continue;
    Visit(expr);

    auto child_sql = ExpectSourceable(expr->sql_expression);

    auto child_source = std::make_shared<sql::ast::Source>(child_sql, GenerateTableAlias());
    expr->sql_expression = child_source;
    from_sources.push_back(child_source);
    node_source_pairs.emplace_back(expr.get(), child_source);
  }

  if (from_sources.empty()) {
    throw std::runtime_error("Product expression has no members");
  }

  std::vector<RelNode*> expr_ptrs;
  for (auto& expr : node->exprs) {
    if (expr) expr_ptrs.push_back(expr.get());
  }
  auto condition = EqualityShorthandRel(expr_ptrs);
  auto select_cols = VarListShorthandRel(node_source_pairs);

  size_t out_col = 1;
  for (const auto& [expr_ptr, child_source] : node_source_pairs) {
    size_t child_arity = expr_ptr->arity;
    for (size_t j = 1; j <= child_arity; j++) {
      std::string col_name = std::format("A{}", j);
      auto column = std::make_shared<sql::ast::Column>(col_name, child_source);
      select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(column, std::format("A{}", out_col++)));
    }
  }

  std::shared_ptr<sql::ast::From> from;
  if (condition) {
    from = std::make_shared<sql::ast::From>(from_sources, condition);
  } else {
    from = std::make_shared<sql::ast::From>(from_sources);
  }
  node->sql_expression = std::make_shared<sql::ast::Select>(select_cols, from);
  return node;
}

std::shared_ptr<sql::ast::Expression> Translator::GetExpressionFromID(RelNode& node, const std::string& id,
                                                                      bool is_top_level) {
  if (node.variables.count(id)) {
    throw NotImplementedException("Non-parameter variable expressions not yet implemented.");
  }
  auto edb_info = context_.GetRelationInfo(id);
  std::shared_ptr<sql::ast::Table> table;
  if (edb_info && edb_info->arity > 0) {
    std::vector<std::string> attribute_names;
    for (int i = 0; i < edb_info->arity; ++i) {
      attribute_names.push_back(i < static_cast<int>(edb_info->attribute_names.size()) ? edb_info->attribute_names[i]
                                                                                       : ("A" + std::to_string(i + 1)));
    }
    table = std::make_shared<sql::ast::Table>(id, edb_info->arity, attribute_names);
  } else {
    table = std::make_shared<sql::ast::Table>(id, context_.GetArity(id));
  }
  if (is_top_level) {
    auto source = std::make_shared<sql::ast::Source>(table, GenerateTableAlias());
    std::vector<std::shared_ptr<sql::ast::Selectable>> select_columns;
    select_columns.reserve(table->arity);
    for (int i = 0; i < table->arity; ++i) {
      auto col_name = table->GetAttributeName(i);
      auto col = std::make_shared<sql::ast::Column>(col_name, source);
      select_columns.push_back(std::make_shared<sql::ast::TermSelectable>(col, col_name));
    }
    auto from = std::make_shared<sql::ast::From>(source);
    return std::make_shared<sql::ast::Select>(select_columns, from);
  }
  return table;
}

std::shared_ptr<sql::ast::Sourceable> Translator::GetBaseSourceableFromApplBase(
    RelNode& node, const std::shared_ptr<RelApplBase>& base) {
  if (auto id_base = dynamic_cast<RelIDApplBase*>(base.get())) {
    auto ra_expr = GetExpressionFromID(node, id_base->id, false);
    return ExpectSourceable(ra_expr);
  }
  if (auto abs_base = dynamic_cast<RelExprApplBase*>(base.get())) {
    Visit(abs_base->expr);
    return ExpectSourceable(abs_base->expr->sql_expression);
  }
  throw NotImplementedException("SQLVisitorRel: unknown application base");
}

Translator::FullApplParamSlots Translator::CollectApplParams(RelNode& node,
                                                             const std::vector<std::shared_ptr<RelApplParam>>& params) {
  FullApplParamSlots slots;
  size_t param_idx = 0;

  for (const auto& param : params) {
    if (!param || param->IsWildcard()) continue;

    auto expr = param->GetExpr();
    if (!expr) continue;

    param_idx++;

    auto term = std::dynamic_pointer_cast<RelTerm>(expr);

    // Non-term param: Accept and make a sourceable.
    if (!term) {
      Visit(expr);
      auto expr_sql = ExpectSourceable(expr->sql_expression);
      auto param_source = std::make_shared<sql::ast::Source>(expr_sql, GenerateTableAlias());
      expr->sql_expression = param_source;
      slots.non_term_param_slots.push_back({param_idx, param_source, expr.get()});
      continue;
    }

    auto id_term = dynamic_cast<RelIDTerm*>(term.get());

    if (id_term) {
      // If the ID term is a relation, get the expression from the relation and make a sourceable.
      if (context_.IsRelation(id_term->id)) {
        auto rel_expr = GetExpressionFromID(node, id_term->id, true);
        auto rel_sourceable = ExpectSourceable(rel_expr);

        auto rel_source = std::make_shared<sql::ast::Source>(rel_sourceable, GenerateTableAlias());
        slots.relation_param_sources.push_back({param_idx, rel_source});
        continue;
      }
      // If the ID term is a variable, add it to the term param slots.
      slots.term_param_slots.push_back({id_term, param_idx});
      continue;
    }

    // Non-ID term: Check if it is a term of constants only.
    if (term->variables.empty()) {
      Visit(expr);
      auto expr_sql = ExpectSourceable(expr->sql_expression);

      auto param_source = std::make_shared<sql::ast::Source>(expr_sql, GenerateTableAlias());
      expr->sql_expression = param_source;
      slots.non_term_param_slots.push_back({param_idx, param_source, expr.get()});
      continue;
    }

    if (term->variables.size() != 1) {
      throw VariableException("Term parameter must have exactly one variable for full application");
    }
    if (term->IsInvalidTermExpression() || term->IsNullPolynomialTerm() ||
        !term->term_linear_coeffs.has_value()) {
      throw VariableException("Invalid or null polynomial term in parameter.");
    }
    slots.term_param_slots.push_back({term.get(), param_idx});
  }

  return slots;
}

std::shared_ptr<sql::ast::Term> Translator::MakeTermForVariableFromParamSlotRel(
    RelNode* term_node, const std::string& column_name, const std::shared_ptr<sql::ast::Source>& ra_source) const {
  auto column = std::make_shared<sql::ast::Column>(column_name, ra_source);
  std::shared_ptr<sql::ast::Term> term = column;
  if (!term_node->term_linear_coeffs.has_value() || term_node->IsInvalidTermExpression() ||
      term_node->IsNullPolynomialTerm()) {
    return term;
  }
  auto [a, b] = term_node->term_linear_coeffs.value();
  if (a != 0.0) {
    if (b < 0.0) {
      term = std::make_shared<sql::ast::Operation>(term, std::make_shared<sql::ast::Constant>(-b), "+");
    } else if (b > 0.0) {
      term = std::make_shared<sql::ast::Operation>(term, std::make_shared<sql::ast::Constant>(b), "-");
    }
    if (a != 1.0) {
      if (b != 0.0) {
        term = std::make_shared<sql::ast::ParenthesisTerm>(term);
      }
      term = std::make_shared<sql::ast::Operation>(term, std::make_shared<sql::ast::Constant>(a), "/");
    }
  }
  return term;
}

Translator::FullApplSqlParts Translator::BuildFullApplSql(
    const FullApplParamSlots& slots, const std::shared_ptr<sql::ast::Source>& ra_source,
    const std::shared_ptr<sql::ast::Sourceable>& base_sourceable,
    const std::function<std::string(size_t)>& column_name_for_index) {
  const auto& term_param_slots = slots.term_param_slots;
  const auto& relation_param_sources = slots.relation_param_sources;
  const auto& non_term_param_slots = slots.non_term_param_slots;

  FullApplSqlParts parts;
  parts.from_sources = {ra_source};

  // Free variables from non-term params (term params with these vars are dropped from select)
  std::unordered_set<std::string> non_term_free_vars;
  for (const auto& [_, __, expr_ptr] : non_term_param_slots) {
    if (expr_ptr) {
      for (const auto& v : expr_ptr->free_variables) non_term_free_vars.insert(v);
    }
  }

  // FROM: ra_source then all subquery params in param order
  std::vector<std::pair<size_t, std::shared_ptr<sql::ast::Source>>> all_param_sources;
  for (const auto& p : relation_param_sources) all_param_sources.push_back(p);
  for (const auto& t : non_term_param_slots) all_param_sources.push_back({std::get<0>(t), std::get<1>(t)});
  std::sort(all_param_sources.begin(), all_param_sources.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  for (const auto& [_, src] : all_param_sources) parts.from_sources.push_back(src);

  // Conditions (ApplicationVariableConditions-style): chained equalities, join param to base, term=non-term var, shared
  // vars in non-term
  std::vector<std::shared_ptr<sql::ast::Condition>> conditions =
      AddChainedEqualitiesForTermParams(term_param_slots, column_name_for_index, ra_source);

  for (const auto& [idx, rel_source] : relation_param_sources) {
    std::string base_col = column_name_for_index(idx);
    std::string rel_col = "A1";
    conditions.push_back(std::make_shared<sql::ast::ComparisonCondition>(
        std::make_shared<sql::ast::Column>(base_col, ra_source), sql::ast::CompOp::EQ,
        std::make_shared<sql::ast::Column>(rel_col, rel_source)));
  }

  for (const auto& [idx, param_source, __] : non_term_param_slots) {
    std::string base_col = column_name_for_index(idx);
    std::string sub_col = "A1";
    conditions.push_back(std::make_shared<sql::ast::ComparisonCondition>(
        std::make_shared<sql::ast::Column>(base_col, ra_source), sql::ast::CompOp::EQ,
        std::make_shared<sql::ast::Column>(sub_col, param_source)));
  }

  std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>> first_non_term_source_by_var;
  for (const auto& [_, src, expr_ptr] : non_term_param_slots) {
    if (!expr_ptr) continue;
    for (const auto& v : expr_ptr->free_variables) first_non_term_source_by_var.emplace(v, src);
  }
  for (const auto& [param_node, idx] : term_param_slots) {
    std::string var = *param_node->variables.begin();
    auto it = first_non_term_source_by_var.find(var);
    if (it == first_non_term_source_by_var.end()) continue;
    // LHS: variable value from term param (e.g. (A1-1) for x+1); RHS: variable column from non-term subquery.
    auto lhs_term = MakeTermForVariableFromParamSlotRel(param_node, column_name_for_index(idx), ra_source);
    conditions.push_back(std::make_shared<sql::ast::ComparisonCondition>(
        lhs_term, sql::ast::CompOp::EQ, std::make_shared<sql::ast::Column>(var, it->second)));
  }

  std::unordered_map<std::string, std::vector<std::shared_ptr<sql::ast::Source>>> sources_by_var;
  for (const auto& [_, src, expr_ptr] : non_term_param_slots) {
    if (!expr_ptr) continue;
    for (const auto& v : expr_ptr->free_variables) sources_by_var[v].push_back(src);
  }
  for (const auto& [var, srcs] : sources_by_var) {
    if (srcs.size() < 2) continue;
    for (size_t i = 0; i + 1 < srcs.size(); ++i) {
      conditions.push_back(std::make_shared<sql::ast::ComparisonCondition>(
          std::make_shared<sql::ast::Column>(var, srcs[i]), sql::ast::CompOp::EQ,
          std::make_shared<sql::ast::Column>(var, srcs[i + 1])));
    }
  }

  if (conditions.size() == 1) {
    parts.where = conditions[0];
  } else if (conditions.size() > 1) {
    parts.where = std::make_shared<sql::ast::LogicalCondition>(conditions, sql::ast::LogicalOp::AND);
  }

  // Select columns (SpecialAppliedVarList-style): param order then remaining base columns
  std::unordered_set<size_t> param_indices;
  for (const auto& [_, idx] : term_param_slots) param_indices.insert(idx);
  for (const auto& [idx, _, __] : non_term_param_slots) param_indices.insert(idx);
  for (const auto& [idx, _] : relation_param_sources) param_indices.insert(idx);

  struct Slot {
    size_t param_idx;
    bool is_term;
    RelNode* term_node;
    std::shared_ptr<sql::ast::Source> source;
    RelNode* non_term_node;
  };
  std::vector<Slot> ordered_slots;
  for (const auto& [n, idx] : term_param_slots) ordered_slots.push_back({idx, true, n, nullptr, nullptr});
  for (const auto& [idx, src, n] : non_term_param_slots) ordered_slots.push_back({idx, false, nullptr, src, n});
  std::sort(ordered_slots.begin(), ordered_slots.end(),
            [](const Slot& a, const Slot& b) { return a.param_idx < b.param_idx; });

  std::unordered_set<std::string> seen_vars;
  for (const Slot& slot : ordered_slots) {
    if (slot.is_term && slot.term_node) {
      std::string var = *slot.term_node->variables.begin();
      if (non_term_free_vars.count(var)) continue;
      if (seen_vars.count(var)) continue;
      seen_vars.insert(var);
      std::shared_ptr<sql::ast::Term> term =
          MakeTermForVariableFromParamSlotRel(slot.term_node, column_name_for_index(slot.param_idx), ra_source);
      parts.select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(term, var));
    } else if (!slot.is_term && slot.non_term_node) {
      for (const auto& var : slot.non_term_node->free_variables) {
        if (seen_vars.count(var)) continue;
        seen_vars.insert(var);
        parts.select_cols.push_back(
            std::make_shared<sql::ast::TermSelectable>(std::make_shared<sql::ast::Column>(var, slot.source)));
      }
    }
  }

  size_t base_arity = GetArityForSourceable(base_sourceable);
  size_t out_idx = 1;
  for (size_t k = 1; k <= base_arity; ++k) {
    if (param_indices.count(k)) continue;
    parts.select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(
        std::make_shared<sql::ast::Column>(column_name_for_index(k), ra_source), fmt::format("A{}", out_idx++)));
  }

  return parts;
}

std::shared_ptr<sql::ast::Select> Translator::VisitAggregateRel(const std::shared_ptr<RelExpr>& expr,
                                                                sql::ast::AggregateFunction function) {
  std::shared_ptr<sql::ast::Sourceable> expr_sql;
  std::shared_ptr<sql::ast::Source> subquery;

  // Simple relation ID (e.g. sum[A]): use table directly so we get "FROM A AS T0" not an extra subquery.
  if (auto* term = dynamic_cast<RelIDTerm*>(expr.get())) {
    if (context_.IsRelation(term->id)) {
      auto ra_expr = GetExpressionFromID(*expr, term->id, false);
      expr_sql = std::dynamic_pointer_cast<sql::ast::Sourceable>(ra_expr);
      if (expr_sql) {
        subquery = std::make_shared<sql::ast::Source>(expr_sql, GenerateTableAlias());
        expr->sql_expression = subquery;
      }
    }
  }

  if (!expr_sql) {
    Visit(expr);
    expr_sql = ExpectSourceable(expr->sql_expression);
    subquery = std::make_shared<sql::ast::Source>(expr_sql, GenerateTableAlias());
    expr->sql_expression = subquery;
  }
  auto arity = expr->arity;
  std::string column_name = std::format("A{}", arity);
  auto column = std::make_shared<sql::ast::Column>(column_name, subquery);
  auto aggregate_selectable =
      std::make_shared<sql::ast::TermSelectable>(std::make_shared<sql::ast::Function>(function, column), "A1");

  auto group_cols = VarListShorthandRel({{expr.get(), subquery}});
  std::vector<std::shared_ptr<sql::ast::Selectable>> select_cols = group_cols;
  select_cols.push_back(aggregate_selectable);

  std::shared_ptr<sql::ast::GroupBy> group_by;
  if (!group_cols.empty()) {
    group_by = std::make_shared<sql::ast::GroupBy>(group_cols);
  }

  auto from = std::make_shared<sql::ast::From>(subquery);
  if (group_by) {
    return std::make_shared<sql::ast::Select>(select_cols, from, group_by);
  }
  return std::make_shared<sql::ast::Select>(select_cols, from);
}

std::shared_ptr<RelExpr> Translator::Visit(const std::shared_ptr<RelCondition>& node) {
  if (!node->lhs || !node->rhs) return node;

  Visit(node->lhs);
  Visit(node->rhs);

  auto lhs_sql = std::dynamic_pointer_cast<sql::ast::Sourceable>(node->lhs->sql_expression);
  auto rhs_sql = std::dynamic_pointer_cast<sql::ast::Sourceable>(node->rhs->sql_expression);

  if (!lhs_sql || !rhs_sql) {
    throw NotImplementedException("SQLVisitorRel: condition expr requires Sourceable lhs and rhs");
  }

  auto lhs_source = std::make_shared<sql::ast::Source>(lhs_sql, GenerateTableAlias());
  auto rhs_source = std::make_shared<sql::ast::Source>(rhs_sql, GenerateTableAlias());

  node->lhs->sql_expression = lhs_source;
  node->rhs->sql_expression = rhs_source;

  std::vector<RelNode*> ctxs = {node->lhs.get(), node->rhs.get()};

  auto cond = EqualityShorthandRel(ctxs);

  auto select_cols = VarListShorthandRel({{node->lhs.get(), lhs_source}, {node->rhs.get(), rhs_source}});

  for (size_t i = 1; i <= node->lhs->arity; i++) {
    auto column = std::make_shared<sql::ast::Column>(std::format("A{}", i), lhs_source);
    select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(column));
  }

  std::shared_ptr<sql::ast::From> from;

  if (cond) {
    from =
        std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{lhs_source, rhs_source}, cond);
  } else {
    from = std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{lhs_source, rhs_source});
  }

  node->sql_expression = std::make_shared<sql::ast::Select>(select_cols, from);
  return node;
}

std::shared_ptr<RelExpr> Translator::Visit(const std::shared_ptr<RelExprAbstraction>& node) {
  if (!node->expr) return node;

  Visit(node->expr);
  auto expr_sql = ExpectSourceable(node->expr->sql_expression);

  auto expr_source = std::make_shared<sql::ast::Source>(expr_sql, GenerateTableAlias());
  node->expr->sql_expression = expr_source;

  auto select_cols = VarListShorthandRel({{node.get(), expr_source}});

  for (size_t i = 0; i < node->bindings.size(); i++) {
    const auto& b = node->bindings[i];
    std::string alias = std::format("A{}", i + 1);
    if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) {
      if (node->expr->free_variables.count(vb->id) == 0) {
        throw VariableException("Bindings variable is not free in inner expression: " + vb->id);
      }
      auto column = std::make_shared<sql::ast::Column>(vb->id, expr_source);
      select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(column, alias));
    } else if (auto* lb = dynamic_cast<RelLiteralBinding*>(b.get())) {
      sql::ast::constant_t c = std::visit([](const auto& v) -> sql::ast::constant_t { return v; }, lb->value);
      select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(std::make_shared<sql::ast::Constant>(c), alias));
    }
  }

  size_t expr_arity = node->expr->arity;
  size_t binding_count = node->bindings.size();
  for (size_t i = 1; i <= expr_arity; i++) {
    std::string col_name = std::format("A{}", i);
    auto column = std::make_shared<sql::ast::Column>(col_name, expr_source);
    select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(column, std::format("A{}", binding_count + i)));
  }

  auto from = std::make_shared<sql::ast::From>(expr_source);
  node->sql_expression = std::make_shared<sql::ast::Select>(select_cols, from);
  return node;
}

std::pair<std::shared_ptr<sql::ast::Source>, std::vector<std::shared_ptr<sql::ast::Source>>>
Translator::CreateRecursiveCTEFromFormula(const std::shared_ptr<sql::ast::Sourceable>& formula_sql,
                                          const std::string& recursive_definition_name, int arity) {
  std::string recursive_alias = GenerateTableAlias("R");

  sql::ast::TableNameUpdater updater(recursive_definition_name, recursive_alias);
  updater.Visit(*formula_sql);

  std::vector<std::shared_ptr<sql::ast::Source>> formula_ctes;
  auto select = std::dynamic_pointer_cast<sql::ast::Select>(formula_sql);
  if (select) {
    formula_ctes.insert(formula_ctes.end(), select->ctes.begin(), select->ctes.end());
    select->ctes.clear();
  }

  std::vector<std::string> def_columns;
  for (int i = 1; i <= arity; i++) {
    def_columns.push_back(std::format("A{}", i));
  }

  auto recursive_source = std::make_shared<sql::ast::Source>(formula_sql, recursive_alias, true, def_columns);
  return {recursive_source, formula_ctes};
}

std::shared_ptr<sql::ast::Source> Translator::BuildBindingsFormulaSource(
    const std::shared_ptr<sql::ast::Sourceable>& formula_sql, bool is_recursive,
    const std::string& recursive_definition_name, const std::vector<std::shared_ptr<RelBinding>>& bindings,
    std::vector<std::shared_ptr<sql::ast::Source>>* out_ctes, bool* out_ctes_are_recursive) {
  if (!is_recursive || recursive_definition_name.empty()) {
    if (out_ctes) out_ctes->clear();
    if (out_ctes_are_recursive) *out_ctes_are_recursive = false;
    return std::make_shared<sql::ast::Source>(formula_sql, GenerateTableAlias());
  }
  std::vector<std::string> binding_vars;
  for (const auto& b : bindings) {
    if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) {
      binding_vars.push_back(vb->id);
    }
  }
  int arity = static_cast<int>(binding_vars.size());
  auto [recursive_cte_source, formula_ctes] =
      CreateRecursiveCTEFromFormula(formula_sql, recursive_definition_name, arity);

  if (out_ctes) {
    out_ctes->clear();
    out_ctes->push_back(recursive_cte_source);
    out_ctes->insert(out_ctes->end(), formula_ctes.begin(), formula_ctes.end());
  }
  if (out_ctes_are_recursive) *out_ctes_are_recursive = true;

  std::vector<std::shared_ptr<sql::ast::Selectable>> subquery_select_cols;
  for (size_t i = 0; i < binding_vars.size(); i++) {
    std::string col_name = std::format("A{}", i + 1);
    auto column = std::make_shared<sql::ast::Column>(col_name, recursive_cte_source);
    subquery_select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(column, binding_vars[i]));
  }
  auto subquery_from =
      std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{recursive_cte_source});
  auto subquery = std::make_shared<sql::ast::Select>(subquery_select_cols, subquery_from);
  return std::make_shared<sql::ast::Source>(subquery, GenerateTableAlias());
}

std::shared_ptr<RelExpr> Translator::Visit(const std::shared_ptr<RelFormulaAbstraction>& node) {
  if (!node->formula) return node;

  Visit(node->formula);

  auto formula_sql = ExpectSourceable(node->formula->sql_expression);

  std::vector<std::shared_ptr<sql::ast::Source>> ctes;
  bool ctes_are_recursive = false;
  std::shared_ptr<sql::ast::Source> formula_source = BuildBindingsFormulaSource(
      formula_sql, node->is_recursive, node->recursive_definition_name, node->bindings, &ctes, &ctes_are_recursive);

  node->formula->sql_expression = formula_source;

  auto select_cols = VarListShorthandRel({{node.get(), formula_source}});

  for (size_t i = 0; i < node->bindings.size(); i++) {
    const auto& b = node->bindings[i];
    std::string alias = std::format("A{}", i + 1);
    if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) {
      if (node->formula->free_variables.count(vb->id) == 0) {
        throw VariableException("Bindings variable is not free in inner formula: " + vb->id);
      }
      auto column = std::make_shared<sql::ast::Column>(vb->id, formula_source);
      select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(column, alias));
    } else if (auto* lb = dynamic_cast<RelLiteralBinding*>(b.get())) {
      sql::ast::constant_t c = std::visit([](const auto& v) -> sql::ast::constant_t { return v; }, lb->value);
      select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(std::make_shared<sql::ast::Constant>(c), alias));
    }
  }

  auto from = std::make_shared<sql::ast::From>(formula_source);
  if (!ctes.empty()) {
    node->sql_expression = std::make_shared<sql::ast::Select>(select_cols, from, ctes, false, ctes_are_recursive);
  } else {
    node->sql_expression = std::make_shared<sql::ast::Select>(select_cols, from);
  }
  return node;
}

std::shared_ptr<RelExpr> Translator::Visit(const std::shared_ptr<RelPartialApplication>& node) {
  // Aggregate special case: sum[expr], max[expr], etc. (single param, base is aggregate ID)
  if (auto* id_base = dynamic_cast<RelIDApplBase*>(node->base.get())) {
    auto it = GetAggregateMap().find(id_base->id);
    if (it != GetAggregateMap().end()) {
      if (node->params.size() != 1) {
        throw std::runtime_error("Aggregate function requires exactly one parameter");
      }
      auto expr = node->params[0]->GetExpr();
      if (!expr) {
        throw std::runtime_error("Aggregate function parameter must be an expression");
      }
      node->sql_expression = VisitAggregateRel(expr, it->second);
      return node;
    }
  }

  auto base_sourceable = GetBaseSourceableFromApplBase(*node, node->base);
  auto column_name_for_index = [this, &base_sourceable](size_t idx) {
    return GetColumnNameForSourceable(base_sourceable, idx);
  };
  auto ra_source = std::make_shared<sql::ast::Source>(base_sourceable, GenerateTableAlias());
  auto slots = CollectApplParams(*node, node->params);
  auto parts = BuildFullApplSql(slots, ra_source, base_sourceable, column_name_for_index);
  node->sql_expression = std::make_shared<sql::ast::Select>(
      parts.select_cols, std::make_shared<sql::ast::From>(parts.from_sources, parts.where));
  return node;
}

std::shared_ptr<RelFormula> Translator::Visit(const std::shared_ptr<RelFullApplication>& node) {
  auto base_sourceable = GetBaseSourceableFromApplBase(*node, node->base);
  auto column_name_for_index = [this, &base_sourceable](size_t idx) {
    return GetColumnNameForSourceable(base_sourceable, idx);
  };
  auto ra_source = std::make_shared<sql::ast::Source>(base_sourceable, GenerateTableAlias());

  auto slots = CollectApplParams(*node, node->params);

  auto parts = BuildFullApplSql(slots, ra_source, base_sourceable, column_name_for_index);

  node->sql_expression = std::make_shared<sql::ast::Select>(
      parts.select_cols, std::make_shared<sql::ast::From>(parts.from_sources, parts.where));
  return node;
}

std::shared_ptr<RelFormula> Translator::Visit(const std::shared_ptr<RelConjunction>& node) {
  if (!node->lhs || !node->rhs) return nullptr;

  Visit(node->lhs);
  Visit(node->rhs);

  auto lhs_sourceable = ExpectSourceable(node->lhs->sql_expression);
  auto rhs_sourceable = ExpectSourceable(node->rhs->sql_expression);

  auto lhs_source = std::make_shared<sql::ast::Source>(lhs_sourceable, GenerateTableAlias());
  auto rhs_source = std::make_shared<sql::ast::Source>(rhs_sourceable, GenerateTableAlias());

  node->lhs->sql_expression = lhs_source;
  node->rhs->sql_expression = rhs_source;

  std::vector<RelNode*> ctxs = {node->lhs.get(), node->rhs.get()};

  auto cond = EqualityShorthandRel(ctxs);
  auto select_cols = VarListShorthandRel({{node->lhs.get(), lhs_source}, {node->rhs.get(), rhs_source}});
  std::shared_ptr<sql::ast::From> from;
  if (cond) {
    from =
        std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{lhs_source, rhs_source}, cond);
  } else {
    from = std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{lhs_source, rhs_source});
  }

  node->sql_expression = std::make_shared<sql::ast::Select>(select_cols, from);
  return node;
}

std::shared_ptr<RelFormula> Translator::Visit(const std::shared_ptr<RelDisjunction>& node) {
  if (!node->lhs || !node->rhs) return nullptr;

  Visit(node->lhs);
  Visit(node->rhs);

  auto lhs_sourceable = ExpectSourceable(node->lhs->sql_expression);
  auto rhs_sourceable = ExpectSourceable(node->rhs->sql_expression);

  // For now we build a simple UNION of the two sides.
  node->sql_expression = std::make_shared<sql::ast::Union>(lhs_sourceable, rhs_sourceable);

  return node;
}

std::shared_ptr<RelFormula> Translator::Visit(const std::shared_ptr<RelNegation>& node) {
  throw TranslationException("Translation for negation not available", ErrorCode::UNKNOWN_UNARY_OPERATOR,
                             SourceLocation(0, 0));
  if (node->formula) {
    Visit(node->formula);
    node->sql_expression = node->formula->sql_expression;
  }
  return node;
}

std::shared_ptr<RelFormula> Translator::Visit(const std::shared_ptr<RelParen>& node) {
  if (node->formula) {
    Visit(node->formula);
    node->sql_expression = node->formula->sql_expression;
  }
  return node;
}

std::shared_ptr<RelFormula> Translator::Visit(const std::shared_ptr<RelComparison>& node) {
  if (!node->lhs || !node->rhs) return node;

  throw TranslationException("SQLVisitorRel: comparison between terms not available",
                             ErrorCode::UNKNOWN_BINARY_OPERATOR, SourceLocation(0, 0));

  Visit(node->lhs);
  Visit(node->rhs);

  auto lhs_term = std::dynamic_pointer_cast<sql::ast::Term>(node->lhs->sql_expression);
  auto rhs_term = std::dynamic_pointer_cast<sql::ast::Term>(node->rhs->sql_expression);

  if (!lhs_term || !rhs_term) return node;

  sql::ast::CompOp op;

  switch (node->op) {
    case RelCompOp::EQ:
      op = sql::ast::CompOp::EQ;
      break;
    case RelCompOp::NEQ:
      op = sql::ast::CompOp::NEQ;
      break;
    case RelCompOp::LT:
      op = sql::ast::CompOp::LT;
      break;
    case RelCompOp::GT:
      op = sql::ast::CompOp::GT;
      break;
    case RelCompOp::LTE:
      op = sql::ast::CompOp::LTE;
      break;
    case RelCompOp::GTE:
      op = sql::ast::CompOp::GTE;
      break;
  }

  node->sql_expression = std::make_shared<sql::ast::ComparisonCondition>(lhs_term, op, rhs_term);
  return node;
}

std::shared_ptr<RelTerm> Translator::Visit(const std::shared_ptr<RelIDTerm>& node) {
  node->sql_expression = std::make_shared<sql::ast::Column>(node->id);
  return node;
}

std::shared_ptr<RelTerm> Translator::Visit(const std::shared_ptr<RelNumTerm>& node) {
  node->sql_expression = std::make_shared<sql::ast::Constant>(node->value);
  return node;
}

std::shared_ptr<RelTerm> Translator::Visit(const std::shared_ptr<RelOpTerm>& node) {
  if (!node->lhs || !node->rhs) return node;

  Visit(node->lhs);
  Visit(node->rhs);

  auto lhs_term = std::dynamic_pointer_cast<sql::ast::Term>(node->lhs->sql_expression);
  auto rhs_term = std::dynamic_pointer_cast<sql::ast::Term>(node->rhs->sql_expression);

  if (!lhs_term || !rhs_term) return node;

  const char* op_str = "+";

  switch (node->op) {
    case RelTermOp::ADD:
      op_str = "+";
      break;
    case RelTermOp::SUB:
      op_str = "-";
      break;
    case RelTermOp::MUL:
      op_str = "*";
      break;
    case RelTermOp::DIV:
      op_str = "/";
      break;
  }
  node->sql_expression = std::make_shared<sql::ast::Operation>(lhs_term, rhs_term, op_str);
  return node;
}

std::shared_ptr<RelTerm> Translator::Visit(const std::shared_ptr<RelParenthesisTerm>& node) {
  if (node->term) {
    Visit(node->term);
    auto inner = std::dynamic_pointer_cast<sql::ast::Term>(node->term->sql_expression);
    node->sql_expression = inner ? std::make_shared<sql::ast::ParenthesisTerm>(inner) : node->term->sql_expression;
  }
  return node;
}

std::vector<std::shared_ptr<sql::ast::Selectable>> Translator::VarListShorthandRel(
    const std::vector<RelNode*>& nodes, const std::shared_ptr<sql::ast::Source>& source) {
  std::unordered_set<std::string> seen_vars;
  std::vector<std::shared_ptr<sql::ast::Selectable>> columns;
  for (RelNode* node : nodes) {
    if (!node) continue;
    for (const auto& var : node->free_variables) {
      if (seen_vars.count(var)) continue;
      auto column = std::make_shared<sql::ast::Column>(var, source);
      columns.push_back(std::make_shared<sql::ast::TermSelectable>(column));
      seen_vars.insert(var);
    }
  }
  return columns;
}

std::vector<std::shared_ptr<sql::ast::Selectable>> Translator::VarListShorthandRel(
    const std::vector<std::pair<RelNode*, std::shared_ptr<sql::ast::Source>>>& node_source_pairs) {
  std::unordered_set<std::string> seen_vars;
  std::vector<std::shared_ptr<sql::ast::Selectable>> columns;
  for (const auto& [node, source] : node_source_pairs) {
    if (!node || !source) continue;
    for (const auto& var : node->free_variables) {
      if (seen_vars.count(var)) continue;
      auto column = std::make_shared<sql::ast::Column>(var, source);
      columns.push_back(std::make_shared<sql::ast::TermSelectable>(column));
      seen_vars.insert(var);
    }
  }
  return columns;
}

std::shared_ptr<sql::ast::Condition> Translator::EqualityShorthandRel(const std::vector<RelNode*>& nodes) {
  std::unordered_map<std::string, std::vector<RelNode*>> repetition_map;
  for (RelNode* node : nodes) {
    if (!node) continue;
    auto source = std::dynamic_pointer_cast<sql::ast::Source>(node->sql_expression);
    if (!source) continue;
    for (const auto& var : node->variables) {
      repetition_map[var].push_back(node);
    }
  }
  std::vector<std::shared_ptr<sql::ast::Condition>> conditions;
  for (const auto& [var, nds] : repetition_map) {
    if (nds.size() < 2) continue;
    for (size_t i = 0; i < nds.size(); i++) {
      for (size_t j = i + 1; j < nds.size(); j++) {
        auto src_i = std::dynamic_pointer_cast<sql::ast::Source>(nds[i]->sql_expression);
        auto src_j = std::dynamic_pointer_cast<sql::ast::Source>(nds[j]->sql_expression);
        if (!src_i || !src_j) continue;
        auto lhs = std::make_shared<sql::ast::Column>(var, src_i);
        auto rhs = std::make_shared<sql::ast::Column>(var, src_j);
        conditions.push_back(std::make_shared<sql::ast::ComparisonCondition>(lhs, sql::ast::CompOp::EQ, rhs));
      }
    }
  }
  if (conditions.empty()) return nullptr;
  if (conditions.size() == 1) return conditions[0];
  return std::make_shared<sql::ast::LogicalCondition>(conditions, sql::ast::LogicalOp::AND);
}

std::vector<std::shared_ptr<sql::ast::Condition>> Translator::AddChainedEqualitiesForTermParams(
    const std::vector<std::pair<RelNode*, size_t>>& term_param_slots,
    const std::function<std::string(size_t)>& column_name_for_index,
    const std::shared_ptr<sql::ast::Source>& ra_source) {
  std::vector<std::shared_ptr<sql::ast::Condition>> conditions;
  // Group (term_node, param_idx) by variable so we can equate the variable's value across positions.
  std::unordered_map<std::string, std::vector<std::pair<RelNode*, size_t>>> slots_by_var;
  for (const auto& [param_node, idx] : term_param_slots) {
    if (!param_node) continue;
    if (param_node->variables.empty()) continue;
    std::string var = *param_node->variables.begin();
    slots_by_var[var].push_back({param_node, idx});
  }

  // For each variable that appears multiple times, add a chain of equalities between
  // the SQL terms that represent the variable at each position (e.g. A(x+1,x-1) => (A1-1) = (A2+1)).
  for (const auto& [var, slots] : slots_by_var) {
    if (slots.size() <= 1) continue;
    for (size_t i = 0; i + 1 < slots.size(); ++i) {
      auto [node1, idx1] = slots[i];
      auto [node2, idx2] = slots[i + 1];

      auto lhs = MakeTermForVariableFromParamSlotRel(node1, column_name_for_index(idx1), ra_source);
      auto rhs = MakeTermForVariableFromParamSlotRel(node2, column_name_for_index(idx2), ra_source);

      conditions.push_back(std::make_shared<sql::ast::ComparisonCondition>(lhs, sql::ast::CompOp::EQ, rhs));
    }
  }
  return conditions;
}

std::shared_ptr<RelFormula> Translator::Visit(const std::shared_ptr<RelExistential>& node) {
  if (!node->formula) return nullptr;

  // Translate inner formula to a Sourceable subquery.
  Visit(node->formula);

  auto inner_expr = node->formula->sql_expression;
  auto inner_srcable = ExpectSourceable(inner_expr);

  auto subquery = std::make_shared<sql::ast::Source>(inner_srcable, GenerateTableAlias());

  // SELECT free variables from the subquery.
  std::vector<std::shared_ptr<sql::ast::Selectable>> select_columns;
  for (const auto& var : node->free_variables) {
    auto col = std::make_shared<sql::ast::Column>(var, subquery);
    select_columns.push_back(std::make_shared<sql::ast::TermSelectable>(col));
  }

  // Build sources and equality conditions for bindings with domains.
  std::vector<std::shared_ptr<sql::ast::Source>> sources;
  sources.push_back(subquery);

  std::vector<std::shared_ptr<sql::ast::Condition>> conditions;

  for (const auto& b : node->bindings) {
    auto* vb = dynamic_cast<RelVarBinding*>(b.get());
    if (!vb) {
      throw NotImplementedException("SQLVisitorRel: literal bindings in quantification not yet supported");
    }
    if (!vb->domain.has_value()) {
      // Unbounded variable: it only appears in the inner formula; no extra domain join needed.
      continue;
    }

    const std::string& domain_name = *vb->domain;
    auto domain_source = CreateTableSource(domain_name);
    sources.push_back(domain_source);

    auto table = std::dynamic_pointer_cast<sql::ast::Table>(domain_source->sourceable);
    if (!table) {
      throw TranslationException("SQLVisitorRel: domain of quantification must be a base table",
                                 ErrorCode::UNKNOWN_BINARY_OPERATOR, SourceLocation(0, 0));
    }

    auto domain_col_name = table->GetAttributeName(0);
    auto domain_col = std::make_shared<sql::ast::Column>(domain_col_name, domain_source);
    auto var_col = std::make_shared<sql::ast::Column>(vb->id, subquery);

    conditions.push_back(std::make_shared<sql::ast::ComparisonCondition>(var_col, sql::ast::CompOp::EQ, domain_col));
  }

  std::shared_ptr<sql::ast::Condition> condition;
  if (conditions.empty()) {
    condition = nullptr;
  } else if (conditions.size() == 1) {
    condition = conditions[0];
  } else {
    condition = std::make_shared<sql::ast::LogicalCondition>(conditions, sql::ast::LogicalOp::AND);
  }

  auto from =
      condition ? std::make_shared<sql::ast::From>(sources, condition) : std::make_shared<sql::ast::From>(sources);

  node->sql_expression = std::make_shared<sql::ast::Select>(select_columns, from);
  return node;
}

std::shared_ptr<RelFormula> Translator::Visit(const std::shared_ptr<RelUniversal>& node) {
  if (!node->formula) return nullptr;

  // Universal quantification requires domains for all bindings.
  std::vector<std::shared_ptr<sql::ast::Source>> bound_domain_sources;
  std::vector<std::string> bound_var_names;

  for (const auto& b : node->bindings) {
    auto* vb = dynamic_cast<RelVarBinding*>(b.get());
    if (!vb) {
      throw NotImplementedException("SQLVisitorRel: literal bindings in universal quantification not yet supported");
    }
    if (!vb->domain.has_value()) {
      throw TranslationException("SQLVisitorRel: universal quantification requires domains for all bindings",
                                 ErrorCode::UNKNOWN_BINARY_OPERATOR, SourceLocation(0, 0));
    }
    const std::string& domain_name = *vb->domain;
    auto domain_source = CreateTableSource(domain_name);
    bound_domain_sources.push_back(domain_source);
    bound_var_names.push_back(vb->id);
  }

  // Translate inner formula to a Sourceable subquery.
  Visit(node->formula);
  auto inner_expr = node->formula->sql_expression;
  auto inner_srcable = ExpectSourceable(inner_expr);

  auto subquery = std::make_shared<sql::ast::Source>(inner_srcable, GenerateTableAlias());

  // Build columns for free variables from the subquery.
  std::vector<std::shared_ptr<sql::ast::Column>> free_var_columns;
  std::vector<std::shared_ptr<sql::ast::Selectable>> select_columns;
  for (const auto& var : node->free_variables) {
    auto col = std::make_shared<sql::ast::Column>(var, subquery);
    free_var_columns.push_back(col);
    select_columns.push_back(std::make_shared<sql::ast::TermSelectable>(col));
  }

  // Build inclusion tuple: (free_vars, bound_vars) where bound_vars come from domain tables.
  std::vector<std::shared_ptr<sql::ast::Column>> inclusion_tuple = free_var_columns;

  for (size_t i = 0; i < bound_var_names.size(); i++) {
    auto domain_source = bound_domain_sources[i];
    auto table = std::dynamic_pointer_cast<sql::ast::Table>(domain_source->sourceable);
    if (!table) {
      throw TranslationException("SQLVisitorRel: domain of universal quantification must be a base table",
                                 ErrorCode::UNKNOWN_BINARY_OPERATOR, SourceLocation(0, 0));
    }
    auto domain_col_name = table->GetAttributeName(0);
    auto domain_col = std::make_shared<sql::ast::Column>(domain_col_name, domain_source);
    inclusion_tuple.push_back(domain_col);
  }

  // Build intermediate inner select: SELECT * FROM subquery
  auto wildcard = std::make_shared<sql::ast::Wildcard>();
  auto inter_inner_from = std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{subquery});
  auto inter_inner_select = std::make_shared<sql::ast::Select>(
      std::vector<std::shared_ptr<sql::ast::Selectable>>{wildcard}, inter_inner_from);

  // Build inclusion condition: (free_vars, bound_vars) NOT IN (SELECT * FROM subquery)
  auto inclusion = std::make_shared<sql::ast::Inclusion>(inclusion_tuple, inter_inner_select, /*is_not=*/true);

  // Build inner select: SELECT * FROM domains WHERE inclusion
  auto wildcard2 = std::make_shared<sql::ast::Wildcard>();
  auto inner_from = std::make_shared<sql::ast::From>(bound_domain_sources, inclusion);
  auto inner_select =
      std::make_shared<sql::ast::Select>(std::vector<std::shared_ptr<sql::ast::Selectable>>{wildcard2}, inner_from);

  // Build NOT EXISTS condition
  auto exists = std::make_shared<sql::ast::Exists>(inner_select);
  auto not_exists = std::make_shared<sql::ast::LogicalCondition>(
      std::vector<std::shared_ptr<sql::ast::Condition>>{exists}, sql::ast::LogicalOp::NOT);

  // Build outer select: SELECT free_vars FROM subquery WHERE NOT EXISTS (...)
  auto outer_from =
      std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{subquery}, not_exists);
  node->sql_expression = std::make_shared<sql::ast::Select>(select_columns, outer_from);
  return node;
}

std::shared_ptr<sql::ast::Expression> Translator::VisitGeneralizedConjunctionRel(
    const std::vector<std::shared_ptr<RelNode>>& subformulas) {
  std::vector<std::shared_ptr<sql::ast::Source>> subqueries;
  std::vector<RelNode*> input_ctxs;
  for (const auto& f : subformulas) {
    if (!f) continue;
    Visit(f);
    auto f_sql = ExpectSourceable(f->sql_expression);
    if (!f_sql) continue;
    auto subq = std::make_shared<sql::ast::Source>(f_sql, GenerateTableAlias());
    f->sql_expression = subq;
    subqueries.push_back(subq);
    input_ctxs.push_back(f.get());
  }
  if (subqueries.empty()) return nullptr;
  auto cond = EqualityShorthandRel(input_ctxs);
  std::vector<std::pair<RelNode*, std::shared_ptr<sql::ast::Source>>> pairs;
  for (size_t i = 0; i < input_ctxs.size() && i < subqueries.size(); i++) {
    pairs.push_back({input_ctxs[i], subqueries[i]});
  }
  auto select_cols = VarListShorthandRel(pairs);
  std::shared_ptr<sql::ast::From> from;
  if (cond) {
    from = std::make_shared<sql::ast::From>(subqueries, cond);
  } else {
    from = std::make_shared<sql::ast::From>(subqueries);
  }
  return std::make_shared<sql::ast::Select>(select_cols, from);
}

std::shared_ptr<sql::ast::Term> Translator::BuildSqlTermFromLinearRelTerm(
    const std::shared_ptr<RelTerm>& rel_term,
    const std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>>& free_var_sources) const {
  if (!rel_term) return nullptr;

  if (auto* num = dynamic_cast<RelNumTerm*>(rel_term.get())) {
    return std::make_shared<sql::ast::Constant>(num->value);
  }

  if (auto* id = dynamic_cast<RelIDTerm*>(rel_term.get())) {
    auto it = free_var_sources.find(id->id);
    if (it == free_var_sources.end()) return nullptr;
    return std::make_shared<sql::ast::Column>(id->id, it->second);
  }

  if (auto* paren = dynamic_cast<RelParenthesisTerm*>(rel_term.get())) {
    return BuildSqlTermFromLinearRelTerm(paren->term, free_var_sources);
  }

  if (auto* op_term = dynamic_cast<RelOpTerm*>(rel_term.get())) {
    if (!op_term->lhs || !op_term->rhs) return nullptr;
    auto lhs_sql = BuildSqlTermFromLinearRelTerm(op_term->lhs, free_var_sources);
    auto rhs_sql = BuildSqlTermFromLinearRelTerm(op_term->rhs, free_var_sources);
    if (!lhs_sql || !rhs_sql) return nullptr;
    const char* op_str = "+";
    switch (op_term->op) {
      case RelTermOp::ADD:
        op_str = "+";
        break;
      case RelTermOp::SUB:
        op_str = "-";
        break;
      case RelTermOp::MUL:
        op_str = "*";
        break;
      case RelTermOp::DIV:
        op_str = "/";
        break;
    }
    return std::make_shared<sql::ast::Operation>(lhs_sql, rhs_sql, op_str);
  }

  return nullptr;
}

void Translator::SpecialAddSourceToFreeVariablesInTerm(
    const std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>>& free_var_sources,
    std::shared_ptr<sql::ast::Term>& term) {
  if (auto col = std::dynamic_pointer_cast<sql::ast::Column>(term)) {
    auto it = free_var_sources.find(col->name);
    if (it != free_var_sources.end()) col->source = it->second;
  } else if (auto op = std::dynamic_pointer_cast<sql::ast::Operation>(term)) {
    SpecialAddSourceToFreeVariablesInTerm(free_var_sources, op->lhs);
    SpecialAddSourceToFreeVariablesInTerm(free_var_sources, op->rhs);
  } else if (auto paren = std::dynamic_pointer_cast<sql::ast::ParenthesisTerm>(term)) {
    SpecialAddSourceToFreeVariablesInTerm(free_var_sources, paren->term);
  }
}

void Translator::ApplyDistinctToDefinitionSelects(const std::shared_ptr<sql::ast::Sourceable>& sourceable) {
  if (!sourceable) return;
  if (auto select = std::dynamic_pointer_cast<sql::ast::Select>(sourceable)) {
    select->is_distinct = true;
    return;
  }
  if (auto union_query = std::dynamic_pointer_cast<sql::ast::Union>(sourceable)) {
    for (auto& member : union_query->members) {
      ApplyDistinctToDefinitionSelects(member);
    }
    return;
  }
  if (auto union_all = std::dynamic_pointer_cast<sql::ast::UnionAll>(sourceable)) {
    for (auto& member : union_all->members) {
      ApplyDistinctToDefinitionSelects(std::static_pointer_cast<sql::ast::Sourceable>(member));
    }
  }
}

std::string Translator::GenerateTableAlias(const std::string& prefix) {
  if (table_alias_prefix_counter_.find(prefix) == table_alias_prefix_counter_.end()) {
    table_alias_prefix_counter_[prefix] = 0;
  }
  return fmt::format("{}{}", prefix, table_alias_prefix_counter_[prefix]++);
}

std::string Translator::GetColumnNameForSourceable(const std::shared_ptr<sql::ast::Sourceable>& src, size_t idx) const {
  if (!src) return fmt::format("A{}", idx);

  if (auto table = std::dynamic_pointer_cast<sql::ast::Table>(src)) {
    if (idx >= 1 && idx <= static_cast<size_t>(table->arity)) {
      return table->GetAttributeName(static_cast<int>(idx) - 1);
    }
    return fmt::format("A{}", idx);
  }

  if (auto select = std::dynamic_pointer_cast<sql::ast::Select>(src)) {
    if (idx >= 1 && idx <= select->columns.size()) {
      return select->columns[idx - 1]->Alias();
    }
    return fmt::format("A{}", idx);
  }

  if (auto uni = std::dynamic_pointer_cast<sql::ast::Union>(src)) {
    if (!uni->members.empty()) return GetColumnNameForSourceable(uni->members.front(), idx);
    return fmt::format("A{}", idx);
  }

  if (auto uni_all = std::dynamic_pointer_cast<sql::ast::UnionAll>(src)) {
    if (!uni_all->members.empty()) {
      return GetColumnNameForSourceable(std::static_pointer_cast<sql::ast::Sourceable>(uni_all->members.front()), idx);
    }
    return fmt::format("A{}", idx);
  }

  return fmt::format("A{}", idx);
}

size_t Translator::GetArityForSourceable(const std::shared_ptr<sql::ast::Sourceable>& src) const {
  if (!src) return 0;
  if (auto table = std::dynamic_pointer_cast<sql::ast::Table>(src)) {
    return static_cast<size_t>(table->arity);
  }
  if (auto select = std::dynamic_pointer_cast<sql::ast::Select>(src)) {
    return select->columns.size();
  }
  if (auto uni = std::dynamic_pointer_cast<sql::ast::Union>(src)) {
    return uni->members.empty() ? 0 : GetArityForSourceable(uni->members.front());
  }
  if (auto uni_all = std::dynamic_pointer_cast<sql::ast::UnionAll>(src)) {
    return uni_all->members.empty()
               ? 0
               : GetArityForSourceable(std::static_pointer_cast<sql::ast::Sourceable>(uni_all->members.front()));
  }
  return 0;
}

std::shared_ptr<sql::ast::Sourceable> Translator::ExpectSourceable(
    const std::shared_ptr<sql::ast::Expression>& expr) const {
  auto src = std::dynamic_pointer_cast<sql::ast::Sourceable>(expr);
  if (!src) {
    throw TranslationException("SQLVisitorRel: expression must be sourceable", ErrorCode::UNKNOWN_BINARY_OPERATOR,
                               SourceLocation(0, 0));
  }
  return src;
}

std::shared_ptr<sql::ast::Source> Translator::CreateTableSource(const std::string& table_name) {
  auto edb_info = context_.GetRelationInfo(table_name);
  std::shared_ptr<sql::ast::Table> table;
  if (edb_info && edb_info->arity > 0) {
    std::vector<std::string> attribute_names;
    for (int i = 0; i < edb_info->arity; ++i) {
      attribute_names.push_back(i < static_cast<int>(edb_info->attribute_names.size()) ? edb_info->attribute_names[i]
                                                                                       : ("A" + std::to_string(i + 1)));
    }
    table = std::make_shared<sql::ast::Table>(table_name, edb_info->arity, attribute_names);
    auto alias = std::make_shared<sql::ast::Alias>(GenerateTableAlias());
    return std::make_shared<sql::ast::Source>(table, alias);
  }
  table = std::make_shared<sql::ast::Table>(table_name, context_.GetArity(table_name));
  return std::make_shared<sql::ast::Source>(table);
}

}  // namespace rel2sql

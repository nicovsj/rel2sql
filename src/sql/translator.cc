#include "sql/translator.h"

#include <fmt/core.h>

#include <algorithm>
#include <functional>
#include <optional>
#include <sstream>

#include "optimizer/replacers.h"
#include "rel_ast/domain.h"
#include "rel_ast/rel_ast.h"
#include "sql/aggregate_map.h"
#include "sql_ast/sql_ast.h"
#include "support/exceptions.h"

namespace rel2sql {

std::shared_ptr<RelBuiltinAggregateExpr> ExtractAggregateFromLiftedAtom(const RelFormula* lifted_atom);

namespace {

struct AggregateThresholdPattern {
  std::string value_var;
  std::shared_ptr<RelBuiltinAggregateExpr> agg;
  std::string idb_name;
  sql::ast::CompOp thresh_op = sql::ast::CompOp::GT;
};

std::optional<AggregateThresholdPattern> FindAggregateThresholdPattern(const RelContext& ctx,
                                                                       const std::shared_ptr<RelNode>& root);

std::vector<std::shared_ptr<RelFormula>> FlattenConjunctionChain(std::shared_ptr<RelFormula> formula) {
  std::vector<std::shared_ptr<RelFormula>> acc;
  while (formula) {
    auto conj = std::dynamic_pointer_cast<RelConjunction>(formula);
    if (!conj || !conj->rhs) break;
    acc.push_back(conj->rhs);
    formula = std::dynamic_pointer_cast<RelFormula>(conj->lhs);
  }
  if (formula) acc.push_back(formula);
  std::reverse(acc.begin(), acc.end());
  return acc;
}

std::shared_ptr<RelTerm> PeelRelParenthesisTerm(const std::shared_ptr<RelTerm>& term);

struct ScalarAggregateDivLift {
  std::string export_var;
  std::shared_ptr<RelBuiltinAggregateExpr> agg;
  std::shared_ptr<RelExpr> divisor;
};

std::optional<ScalarAggregateDivLift> ParseScalarAggregateDivLift(const std::shared_ptr<RelFormula>& formula) {
  if (!formula) return std::nullopt;
  std::function<std::shared_ptr<RelFormula>(const std::shared_ptr<RelFormula>&)> peel;
  peel = [&](const std::shared_ptr<RelFormula>& f) -> std::shared_ptr<RelFormula> {
    if (!f) return f;
    if (auto ex = std::dynamic_pointer_cast<RelExistential>(f)) {
      return ex->formula ? peel(ex->formula) : f;
    }
    if (auto fab = std::dynamic_pointer_cast<RelFormulaAbstraction>(f)) {
      return fab->formula ? peel(fab->formula) : f;
    }
    return f;
  };
  auto root = peel(formula);
  if (!root) return std::nullopt;
  auto flat = FlattenConjunctionChain(root);
  if (flat.size() != 2) return std::nullopt;

  const RelFullApplication* agg_app = nullptr;
  std::shared_ptr<RelComparison> eq_cmp;
  for (const auto& conjunct : flat) {
    if (auto cmp = std::dynamic_pointer_cast<RelComparison>(conjunct)) {
      if (eq_cmp) return std::nullopt;
      eq_cmp = cmp;
    } else if (auto* app = dynamic_cast<const RelFullApplication*>(conjunct.get())) {
      if (ExtractAggregateFromLiftedAtom(app)) {
        if (agg_app) return std::nullopt;
        agg_app = app;
      } else {
        return std::nullopt;
      }
    } else {
      return std::nullopt;
    }
  }
  if (!agg_app || !eq_cmp || eq_cmp->op != RelCompOp::EQ || !eq_cmp->lhs || !eq_cmp->rhs) return std::nullopt;

  auto parse_div_rhs = [&](const RelIDTerm* export_id,
                           const std::shared_ptr<RelTerm>& rhs) -> std::optional<ScalarAggregateDivLift> {
    if (!export_id) return std::nullopt;
    auto peeled = PeelRelParenthesisTerm(rhs);
    auto* op = dynamic_cast<RelOpTerm*>(peeled.get());
    if (!op || op->op != RelTermOp::DIV || !op->lhs || !op->rhs) return std::nullopt;
    auto* agg_id = dynamic_cast<RelIDTerm*>(op->lhs.get());
    if (!agg_id) return std::nullopt;
    auto* param = dynamic_cast<const RelIDTerm*>(agg_app->params[0] ? agg_app->params[0]->GetExpr().get() : nullptr);
    if (!param || agg_id->id != param->id) return std::nullopt;
    auto agg = ExtractAggregateFromLiftedAtom(agg_app);
    if (!agg || !agg->body) return std::nullopt;
    return ScalarAggregateDivLift{export_id->id, agg, op->rhs};
  };

  if (auto* lhs_id = dynamic_cast<RelIDTerm*>(eq_cmp->lhs.get())) {
    if (auto found = parse_div_rhs(lhs_id, eq_cmp->rhs)) return found;
  }
  if (auto* rhs_id = dynamic_cast<RelIDTerm*>(eq_cmp->rhs.get())) {
    if (auto found = parse_div_rhs(rhs_id, eq_cmp->lhs)) return found;
  }
  return std::nullopt;
}

std::optional<ScalarAggregateDivLift> ParseLiftedAggregateDivExport(const RelFullApplication& agg_app,
                                                                    const RelComparison& eq_cmp) {
  if (eq_cmp.op != RelCompOp::EQ || !eq_cmp.lhs || !eq_cmp.rhs) return std::nullopt;
  auto agg = ExtractAggregateFromLiftedAtom(&agg_app);
  if (!agg || !agg->body) return std::nullopt;
  auto* param = dynamic_cast<const RelExprApplParam*>(agg_app.params[0].get());
  if (!param || !param->expr) return std::nullopt;
  auto* param_id = dynamic_cast<const RelIDTerm*>(param->expr.get());
  if (!param_id) return std::nullopt;

  auto parse_div_rhs = [&](const RelIDTerm* export_id,
                           const std::shared_ptr<RelTerm>& rhs) -> std::optional<ScalarAggregateDivLift> {
    if (!export_id) return std::nullopt;
    auto peeled = PeelRelParenthesisTerm(rhs);
    auto* op = dynamic_cast<RelOpTerm*>(peeled.get());
    if (!op || op->op != RelTermOp::DIV || !op->lhs || !op->rhs) return std::nullopt;
    auto* agg_id = dynamic_cast<RelIDTerm*>(op->lhs.get());
    if (!agg_id || agg_id->id != param_id->id) return std::nullopt;
    return ScalarAggregateDivLift{export_id->id, agg, op->rhs};
  };

  if (auto* lhs_id = dynamic_cast<RelIDTerm*>(eq_cmp.lhs.get())) {
    if (auto found = parse_div_rhs(lhs_id, eq_cmp.rhs)) return found;
  }
  if (auto* rhs_id = dynamic_cast<RelIDTerm*>(eq_cmp.rhs.get())) {
    if (auto found = parse_div_rhs(rhs_id, eq_cmp.lhs)) return found;
  }
  return std::nullopt;
}

std::string BuildOrderBySqlOrdinals(size_t arity, sql::ast::SortDirection dir) {
  if (arity == 0) return "ORDER BY 1";
  std::ostringstream os;
  os << "ORDER BY ";
  for (size_t i = 1; i <= arity; ++i) {
    if (i > 1) os << ", ";
    os << i;
    os << (dir == sql::ast::SortDirection::DESC ? " DESC" : " ASC");
  }
  return os.str();
}

size_t RankedFinalSortOutputPosition(size_t body_col_index) {
  // body col 1..N -> A2, then A3 is wildcard placeholder, then A4..
  return body_col_index < 2 ? body_col_index + 1 : body_col_index + 2;
}

std::shared_ptr<RelExpr> PeelRelParenthesisExpr(const std::shared_ptr<RelExpr>& expr) {
  auto peeled = expr;
  while (peeled) {
    if (auto* eat = dynamic_cast<RelExprAsTerm*>(peeled.get())) {
      peeled = eat->inner;
      continue;
    }
    if (auto par = std::dynamic_pointer_cast<RelParenthesisTerm>(peeled)) {
      peeled = par->term;
      continue;
    }
    break;
  }
  return peeled;
}

std::shared_ptr<RelTerm> PeelRelParenthesisTerm(const std::shared_ptr<RelTerm>& term) {
  auto peeled = term;
  while (peeled) {
    if (auto par = std::dynamic_pointer_cast<RelParenthesisTerm>(peeled)) {
      peeled = par->term;
      continue;
    }
    break;
  }
  return peeled;
}

RelIDTerm* AsPeeledIdTerm(const std::shared_ptr<RelTerm>& term) {
  return dynamic_cast<RelIDTerm*>(PeelRelParenthesisTerm(term).get());
}

bool SourceExposesColumn(const std::shared_ptr<sql::ast::Source>& source, const std::string& col_name) {
  if (!source || !source->sourceable) return false;
  if (auto table = std::dynamic_pointer_cast<sql::ast::Table>(source->sourceable)) {
    for (int i = 0; i < table->arity; i++) {
      if (table->GetAttributeName(i) == col_name) return true;
    }
    return false;
  }
  if (auto select = std::dynamic_pointer_cast<sql::ast::Select>(source->sourceable)) {
    for (const auto& col : select->columns) {
      const auto* ts = dynamic_cast<const sql::ast::TermSelectable*>(col.get());
      if (!ts) continue;
      if (ts->alias.has_value() && *ts->alias == col_name) return true;
      if (const auto* c = dynamic_cast<const sql::ast::Column*>(ts->term.get())) {
        if (c->name == col_name) return true;
      }
    }
  }
  return false;
}

std::string BindingBareName(const std::string& var) {
  if (auto dot = var.rfind('.'); dot != std::string::npos) return var.substr(dot + 1);
  return var;
}

void CollectRelIdTermNames(const std::shared_ptr<RelNode>& node, std::unordered_set<std::string>& out) {
  if (!node) return;
  if (auto* id = dynamic_cast<RelIDTerm*>(node.get())) {
    if (!id->id.empty() && id->id[0] != '_') out.insert(id->id);
  }
  for (const auto& ch : node->Children()) {
    CollectRelIdTermNames(ch, out);
  }
}

std::set<std::string> ComputeAggregateGroupKeys(const std::shared_ptr<RelExpr>& body) {
  if (auto abs = std::dynamic_pointer_cast<RelExprAbstraction>(body)) {
    std::unordered_set<std::string> mentioned;
    CollectRelIdTermNames(abs->expr, mentioned);
    std::unordered_set<std::string> binding_ids;
    for (const auto& b : abs->bindings) {
      if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) binding_ids.insert(vb->id);
    }
    std::set<std::string> keys;
    for (const auto& id : mentioned) {
      if (!binding_ids.count(id)) keys.insert(id);
    }
    if (!keys.empty()) return keys;
  }
  if (body) return body->free_variables;
  return {};
}

bool IdAppearsInRelNode(const std::shared_ptr<RelNode>& node, const std::string& id) {
  if (!node) return false;
  if (auto* idt = dynamic_cast<RelIDTerm*>(node.get())) {
    if (idt->id == id) return true;
  }
  for (const auto& ch : node->Children()) {
    if (IdAppearsInRelNode(ch, id)) return true;
  }
  return false;
}

void CollectIdbTermSources(const std::shared_ptr<RelTerm>& term, const RelContext& ctx,
                           const std::function<std::string()>& gen_alias,
                           std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>>& out) {
  if (!term) return;
  if (auto* id = dynamic_cast<RelIDTerm*>(term.get())) {
    if (ctx.IsIDB(id->id) && !out.contains(id->id)) {
      auto table = std::make_shared<sql::ast::Table>(id->id, ctx.GetArity(id->id));
      out.emplace(id->id, std::make_shared<sql::ast::Source>(table, gen_alias()));
    }
    return;
  }
  if (auto* paren = dynamic_cast<RelParenthesisTerm*>(term.get())) {
    CollectIdbTermSources(paren->term, ctx, gen_alias, out);
    return;
  }
  if (auto* op = dynamic_cast<RelOpTerm*>(term.get())) {
    CollectIdbTermSources(op->lhs, ctx, gen_alias, out);
    CollectIdbTermSources(op->rhs, ctx, gen_alias, out);
  }
}

}  // namespace

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
    if (expr->arity != arity) {
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
  // Explicit A1, A2, ... like RelProduct: SELECT * does not reliably expose VALUES column names to outer references
  // (e.g. R[1] / partial application) across SQL dialects.
  std::vector<std::shared_ptr<sql::ast::Selectable>> selects;
  for (size_t i = 0; i < arity; ++i) {
    auto col_name = fmt::format("A{}", i + 1);
    auto column = std::make_shared<sql::ast::Column>(col_name, source);
    selects.push_back(std::make_shared<sql::ast::TermSelectable>(column, col_name));
  }
  auto select = std::make_shared<sql::ast::Select>(selects, from, true);
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
  if (TryEmitScalarAggregateProduct(node)) return node;

  if (node->has_only_literal_values) {
    std::vector<std::shared_ptr<sql::ast::Selectable>> selects;
    for (size_t i = 0; i < node->exprs.size(); ++i) {
      auto& expr = node->exprs[i];
      Visit(expr);
      if (!expr->constant.has_value()) {
        throw std::runtime_error("Special product expression with non-constant member");
      }
      auto constant = std::make_shared<sql::ast::Constant>(expr->constant.value());
      // Explicit A1, A2, ... so wrapped subqueries (e.g. in disjunctive abstractions) are valid in DuckDB
      // and other engines that do not synthesize PostgreSQL-style column names for SELECT literals.
      selects.push_back(std::make_shared<sql::ast::TermSelectable>(constant, fmt::format("A{}", i + 1)));
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
  auto select = std::make_shared<sql::ast::Select>(select_cols, from);
  node->sql_expression = select;
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
    if (term->IsInvalidTermExpression() || !term->GetSingleVarCoeffs()) {
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
  auto* rel_term = dynamic_cast<RelTerm*>(term_node);
  if (!rel_term) return term;
  auto opt_coeffs = rel_term->GetSingleVarCoeffs();
  if (!opt_coeffs || rel_term->IsInvalidTermExpression()) {
    return term;
  }
  auto [a, b] = *opt_coeffs;
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

std::shared_ptr<sql::ast::Select> Translator::VisitAggregateBindingsExpr(const std::shared_ptr<RelExprAbstraction>& abs,
                                                                         sql::ast::AggregateFunction function,
                                                                         bool count_all) {
  if (!abs->expr) {
    throw TranslationException("aggregate bindings expression requires body", ErrorCode::UNKNOWN_BINARY_OPERATOR,
                               SourceLocation(0, 0));
  }
  Visit(abs->expr);
  auto expr_sql = ExpectSourceable(abs->expr->sql_expression);
  auto expr_select = std::dynamic_pointer_cast<sql::ast::Select>(expr_sql);

  if (expr_select) {
    std::unordered_set<std::string> existing_aliases;
    for (const auto& col : expr_select->columns) {
      const auto* ts = dynamic_cast<const sql::ast::TermSelectable*>(col.get());
      if (ts && ts->alias.has_value()) existing_aliases.insert(*ts->alias);
    }
    std::unordered_set<std::string> binding_ids;
    for (const auto& b : abs->bindings) {
      if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) binding_ids.insert(vb->id);
    }
    std::vector<std::shared_ptr<sql::ast::Selectable>> binding_cols;
    for (const auto& b : abs->bindings) {
      auto* vb = dynamic_cast<RelVarBinding*>(b.get());
      if (!vb) continue;
      if (!IdAppearsInRelNode(abs->expr, vb->id)) {
        throw VariableException("Bindings variable is not free in inner expression: " + vb->id);
      }
      if (existing_aliases.count(vb->id)) continue;
      auto column = MakeColumnForBindingOnExprSource(expr_sql, vb->id);
      binding_cols.push_back(std::make_shared<sql::ast::TermSelectable>(column, vb->id));
      existing_aliases.insert(vb->id);
    }
    std::unordered_set<std::string> extra_ids;
    CollectRelIdTermNames(abs->expr, extra_ids);
    for (const auto& id : extra_ids) {
      if (binding_ids.count(id) || existing_aliases.count(id)) continue;
      auto column = MakeColumnForBindingOnExprSource(expr_sql, id);
      binding_cols.push_back(std::make_shared<sql::ast::TermSelectable>(column, id));
      existing_aliases.insert(id);
    }
    expr_select->columns.insert(expr_select->columns.begin(), binding_cols.begin(), binding_cols.end());
  }

  auto subquery = std::make_shared<sql::ast::Source>(expr_sql, GenerateTableAlias());
  subquery->inhibit_subquery_flatten = true;
  abs->expr->sql_expression = subquery;

  const size_t measure_arity = abs->expr->arity;
  const std::string measure_col = std::format("A{}", measure_arity);

  std::shared_ptr<sql::ast::Term> agg_arg;
  if (count_all && function == sql::ast::AggregateFunction::COUNT) {
    agg_arg = std::make_shared<sql::ast::Constant>(1);
  } else {
    agg_arg = std::make_shared<sql::ast::Column>(measure_col, subquery);
  }
  auto aggregate_selectable =
      std::make_shared<sql::ast::TermSelectable>(std::make_shared<sql::ast::Function>(function, agg_arg), "A1");

  std::vector<std::shared_ptr<sql::ast::Selectable>> group_cols;
  std::set<std::string> group_keys = abs->free_variables;
  if (group_keys.empty()) group_keys = ComputeAggregateGroupKeys(abs);
  for (const auto& var : group_keys) {
    const std::string col_name = ResolveOutputColumnNameForVariableOnSource(subquery, var);
    auto column = std::make_shared<sql::ast::Column>(col_name, subquery);
    group_cols.push_back(std::make_shared<sql::ast::TermSelectable>(column, var));
  }
  std::vector<std::shared_ptr<sql::ast::Selectable>> select_cols = group_cols;
  select_cols.push_back(aggregate_selectable);

  std::shared_ptr<sql::ast::GroupBy> group_by;
  if (!group_cols.empty()) {
    group_by = std::make_shared<sql::ast::GroupBy>(group_cols);
  }
  auto from = std::make_shared<sql::ast::From>(subquery);
  auto select = group_by ? std::make_shared<sql::ast::Select>(select_cols, from, group_by)
                         : std::make_shared<sql::ast::Select>(select_cols, from);
  abs->sql_expression = select;
  return select;
}

std::shared_ptr<sql::ast::Select> Translator::VisitAggregateRel(const std::shared_ptr<RelExpr>& expr,
                                                                sql::ast::AggregateFunction function, bool count_all) {
  if (auto abs = std::dynamic_pointer_cast<RelExprAbstraction>(expr)) {
    if (!abs->bindings.empty()) {
      return VisitAggregateBindingsExpr(abs, function, count_all);
    }
  }
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
  std::shared_ptr<sql::ast::Term> agg_arg;
  if (count_all && function == sql::ast::AggregateFunction::COUNT) {
    agg_arg = std::make_shared<sql::ast::Constant>(1);
  } else {
    auto column = std::make_shared<sql::ast::Column>(column_name, subquery);
    agg_arg = column;
  }
  auto aggregate_selectable =
      std::make_shared<sql::ast::TermSelectable>(std::make_shared<sql::ast::Function>(function, agg_arg), "A1");

  std::vector<std::shared_ptr<sql::ast::Selectable>> group_cols;
  size_t col_idx = 1;
  for (const auto& var : expr->free_variables) {
    std::string physical = GetColumnNameForSourceable(expr_sql, col_idx++);
    auto column = std::make_shared<sql::ast::Column>(physical, subquery);
    group_cols.push_back(std::make_shared<sql::ast::TermSelectable>(column, var));
  }
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

  auto select = std::make_shared<sql::ast::Select>(select_cols, from);
  node->sql_expression = select;
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
      if (!IdAppearsInRelNode(node->expr, vb->id)) {
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
  auto select = std::make_shared<sql::ast::Select>(select_cols, from);
  node->sql_expression = select;
  return node;
}

std::shared_ptr<sql::ast::Sourceable> Translator::DomainToSql(const Domain& domain) {
  if (auto* cd = dynamic_cast<const ConstantDomain*>(&domain)) {
    auto constant = std::make_shared<sql::ast::Constant>(cd->value);
    auto selectable = std::make_shared<sql::ast::TermSelectable>(constant, "A1");
    return std::make_shared<sql::ast::Select>(std::vector<std::shared_ptr<sql::ast::Selectable>>{selectable}, false);
  }

  if (auto* dd = dynamic_cast<const DefinedDomain*>(&domain)) {
    auto table_source = CreateTableSource(dd->table_name);
    auto table = std::dynamic_pointer_cast<sql::ast::Table>(table_source->sourceable);
    if (!table) {
      throw TranslationException("DomainToSql: DefinedDomain must be a table", ErrorCode::UNKNOWN_BINARY_OPERATOR,
                                 SourceLocation(0, 0));
    }
    std::vector<std::shared_ptr<sql::ast::Selectable>> cols;
    for (int i = 0; i < static_cast<int>(dd->table_arity); i++) {
      std::string col_name = table->GetAttributeName(i);
      auto col = std::make_shared<sql::ast::Column>(col_name, table_source);
      cols.push_back(std::make_shared<sql::ast::TermSelectable>(col, fmt::format("A{}", i + 1)));
    }
    auto from = std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{table_source});
    return std::make_shared<sql::ast::Select>(cols, from, false);
  }

  if (auto* proj = dynamic_cast<const Projection*>(&domain)) {
    if (proj->IsEmpty()) {
      throw TranslationException("DomainToSql: empty projection", ErrorCode::UNKNOWN_BINARY_OPERATOR,
                                 SourceLocation(0, 0));
    }
    auto inner_sql = DomainToSql(*proj->domain);
    auto inner_source = std::make_shared<sql::ast::Source>(inner_sql, GenerateTableAlias());
    std::vector<std::shared_ptr<sql::ast::Selectable>> cols;
    for (size_t i = 0; i < proj->projected_indices.size(); i++) {
      size_t idx = proj->projected_indices[i];
      std::string col_name = GetColumnNameForSourceable(inner_sql, idx + 1);
      auto col = std::make_shared<sql::ast::Column>(col_name, inner_source);
      cols.push_back(std::make_shared<sql::ast::TermSelectable>(col, fmt::format("A{}", i + 1)));
    }
    auto from = std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{inner_source});
    return std::make_shared<sql::ast::Select>(cols, from, false);
  }

  if (auto* un = dynamic_cast<const DomainUnion*>(&domain)) {
    auto lhs_sql = DomainToSql(*un->lhs);
    auto rhs_sql = DomainToSql(*un->rhs);
    return std::make_shared<sql::ast::Union>(lhs_sql, rhs_sql);
  }

  if (auto* op = dynamic_cast<const DomainOperation*>(&domain)) {
    if (op->lhs->Arity() != 1 || op->rhs->Arity() != 1) {
      throw TranslationException("DomainToSql: DomainOperation requires arity 1", ErrorCode::UNKNOWN_BINARY_OPERATOR,
                                 SourceLocation(0, 0));
    }
    auto lhs_sql = DomainToSql(*op->lhs);
    auto rhs_sql = DomainToSql(*op->rhs);
    auto lhs_source = std::make_shared<sql::ast::Source>(lhs_sql, GenerateTableAlias());
    auto rhs_source = std::make_shared<sql::ast::Source>(rhs_sql, GenerateTableAlias());
    const char* op_str = "+";
    switch (op->op) {
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
    std::string lhs_col = GetColumnNameForSourceable(lhs_sql, 1);
    std::string rhs_col = GetColumnNameForSourceable(rhs_sql, 1);
    auto lhs_term = std::make_shared<sql::ast::Column>(lhs_col, lhs_source);
    auto rhs_term = std::make_shared<sql::ast::Column>(rhs_col, rhs_source);
    auto result_term = std::make_shared<sql::ast::Operation>(lhs_term, rhs_term, op_str);
    auto selectable = std::make_shared<sql::ast::TermSelectable>(result_term, "A1");
    auto from =
        std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{lhs_source, rhs_source});
    return std::make_shared<sql::ast::Select>(std::vector<std::shared_ptr<sql::ast::Selectable>>{selectable}, from,
                                              false);
  }

  if (auto* intl = dynamic_cast<const IntensionalDomain*>(&domain)) {
    if (!intl->node) {
      throw TranslationException("DomainToSql: IntensionalDomain has no inner expression",
                                 ErrorCode::UNKNOWN_BINARY_OPERATOR, SourceLocation(0, 0));
    }
    if (!intl->node->sql_expression) {
      Visit(intl->node);
    }
    auto sourceable = std::dynamic_pointer_cast<sql::ast::Sourceable>(intl->node->sql_expression);
    if (!sourceable) {
      throw TranslationException("DomainToSql: IntensionalDomain inner did not produce a Sourceable",
                                 ErrorCode::UNKNOWN_BINARY_OPERATOR, SourceLocation(0, 0));
    }
    return sourceable;
  }

  throw TranslationException("DomainToSql: unknown domain type", ErrorCode::UNKNOWN_BINARY_OPERATOR,
                             SourceLocation(0, 0));
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

  if (auto parsed = ParseScalarAggregateDivLift(node->formula)) {
    std::shared_ptr<sql::ast::Select> out;
    if (TryEmitScalarAggregateDiv(parsed->agg, parsed->divisor, *node, out)) {
      if (node->bindings.size() == 1 && out->columns.size() == 1) {
        if (auto* ts = dynamic_cast<sql::ast::TermSelectable*>(out->columns[0].get())) {
          ts->alias = "A1";
        }
        node->formula->sql_expression = out;
        node->sql_expression = out;
        node->arity = 1;
        return node;
      }
      node->formula->sql_expression = out;
      auto formula_sql = ExpectSourceable(out);
      auto formula_source = std::make_shared<sql::ast::Source>(formula_sql, GenerateTableAlias());
      node->formula->sql_expression = formula_source;
      std::vector<std::shared_ptr<sql::ast::Selectable>> select_cols;
      for (size_t i = 0; i < node->bindings.size(); i++) {
        const auto& b = node->bindings[i];
        std::string alias = std::format("A{}", i + 1);
        if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) {
          auto col_name = ResolveOutputColumnNameForVariableOnSource(formula_source, vb->id);
          auto column = std::make_shared<sql::ast::Column>(col_name, formula_source);
          select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(column, alias));
        }
      }
      node->sql_expression =
          std::make_shared<sql::ast::Select>(select_cols, std::make_shared<sql::ast::From>(formula_source));
      return node;
    }
  }

  if (auto pattern = FindAggregateThresholdPattern(context_, node)) {
    auto export_sel = EmitAggregateExportSelect(pattern->agg, pattern->value_var);
    auto inner_src = std::make_shared<sql::ast::Source>(export_sel, GenerateTableAlias());
    inner_src->inhibit_subquery_flatten = true;
    auto thresh_src = std::make_shared<sql::ast::Source>(
        std::make_shared<sql::ast::Table>(pattern->idb_name, context_.GetArity(pattern->idb_name)),
        GenerateTableAlias());
    auto value_col = std::make_shared<sql::ast::Column>(pattern->value_var, inner_src);
    auto thresh_col = std::make_shared<sql::ast::Column>("A1", thresh_src);
    auto cond = std::make_shared<sql::ast::ComparisonCondition>(value_col, pattern->thresh_op, thresh_col);
    std::vector<std::shared_ptr<sql::ast::Selectable>> select_cols;
    for (const auto& var : ComputeAggregateGroupKeys(pattern->agg->body)) {
      if (var == pattern->value_var) continue;
      auto col_name = ResolveOutputColumnNameForVariableOnSource(inner_src, var);
      auto col = std::make_shared<sql::ast::Column>(col_name, inner_src);
      select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(col, var));
    }
    select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(value_col, pattern->value_var));
    node->formula->sql_expression = std::make_shared<sql::ast::Select>(
        select_cols,
        std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{inner_src, thresh_src}, cond));
  } else {
    Visit(node->formula);
  }

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
      if (!IdAppearsInRelNode(node->formula, vb->id)) {
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
  std::shared_ptr<sql::ast::Select> select;
  if (!ctes.empty()) {
    select = std::make_shared<sql::ast::Select>(select_cols, from, ctes, false, ctes_are_recursive);
  } else {
    select = std::make_shared<sql::ast::Select>(select_cols, from);
  }
  node->sql_expression = select;
  return node;
}

std::shared_ptr<RelExpr> Translator::Visit(const std::shared_ptr<RelPartialApplication>& node) {
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

bool Translator::IsTermRewriterLiftedBindingConjunction(const RelConjunction& node) {
  auto* lhs_app = dynamic_cast<RelFullApplication*>(node.lhs.get());
  auto* rhs_cmp = dynamic_cast<RelComparison*>(node.rhs.get());
  if (!lhs_app || !rhs_cmp || !lhs_app->base || lhs_app->params.size() != 1) return false;

  auto* param = dynamic_cast<RelExprApplParam*>(lhs_app->params[0].get());
  if (!param || !param->expr) return false;
  auto* param_id = dynamic_cast<RelIDTerm*>(param->expr.get());
  if (!param_id) return false;

  auto matches_param = [&](const std::shared_ptr<RelTerm>& term) {
    auto* id = AsPeeledIdTerm(term);
    return id && id->id == param_id->id;
  };
  auto is_ground_term = [&](const std::shared_ptr<RelTerm>& term) { return term && term->variables.empty(); };

  const bool lhs_is_param = rhs_cmp->lhs && matches_param(rhs_cmp->lhs);
  const bool rhs_is_param = rhs_cmp->rhs && matches_param(rhs_cmp->rhs);
  if (!lhs_is_param && !rhs_is_param) return false;
  const auto& other = lhs_is_param ? rhs_cmp->rhs : rhs_cmp->lhs;
  if (!other) return false;
  if (AsPeeledIdTerm(other)) return true;
  return is_ground_term(other);
}

std::shared_ptr<RelFormula> Translator::Visit(const std::shared_ptr<RelConjunction>& node) {
  if (!node->lhs || !node->rhs) return nullptr;

  if (TryEmitDateYearLiftPairConjunction(node)) return node;

  if (auto flat = FlattenConjunctionChain(std::dynamic_pointer_cast<RelFormula>(node)); flat.size() >= 2) {
    std::vector<std::shared_ptr<RelNode>> conjuncts(flat.begin(), flat.end());
    if (auto emitted = TryEmitAggregateEqualityWithIdbThresholdConjunction(conjuncts)) {
      node->sql_expression = emitted;
      return node;
    }
  }

  if (auto* lhs_app = dynamic_cast<RelFullApplication*>(node->lhs.get())) {
    if (auto cmp = std::dynamic_pointer_cast<RelComparison>(node->rhs)) {
      if (auto parsed = ParseLiftedAggregateDivExport(*lhs_app, *cmp)) {
        std::shared_ptr<sql::ast::Select> out;
        if (TryEmitScalarAggregateDiv(parsed->agg, parsed->divisor, *node, out)) {
          node->sql_expression = out;
          return node;
        }
      }
    }
  }

  // TermRewriter: exists(z | {agg}(z) and z > c). The application only binds z to the aggregate;
  // RelComparison already materializes z's domain — skip the duplicate cross join.
  if (IsTermRewriterLiftedBindingConjunction(*node)) {
    auto cmp = std::dynamic_pointer_cast<RelComparison>(node->rhs);
    if (cmp && TryEmitLiftedPartialAppLiteralEquality(cmp, node->lhs)) {
      node->sql_expression = cmp->sql_expression;
      return node;
    }
    if (cmp && TryTranslateAggregateConstantComparison(cmp, node->safety.SmallCover(), node->lhs)) {
      node->sql_expression = cmp->sql_expression;
      return node;
    }
    if (cmp && TryTranslateAggregateVariableEquality(cmp, node->safety.SmallCover(), node->lhs)) {
      node->sql_expression = cmp->sql_expression;
      return node;
    }
    Visit(node->rhs);
    node->sql_expression = node->rhs->sql_expression;
    return node;
  }

  Visit(node->lhs);
  Visit(node->rhs);

  auto lhs_sourceable = ExpectSourceable(node->lhs->sql_expression);
  auto rhs_sourceable = ExpectSourceable(node->rhs->sql_expression);

  // Deduplicate CTEs: if rhs has CTEs from the same Bound as lhs, replace references and drop duplicates.
  auto* lhs_query = dynamic_cast<sql::ast::Query*>(lhs_sourceable.get());
  auto* rhs_query = dynamic_cast<sql::ast::Query*>(rhs_sourceable.get());
  if (lhs_query && rhs_query) {
    std::map<std::pair<std::size_t, std::vector<std::string>>, std::shared_ptr<sql::ast::Source>> lhs_cte_map;
    for (const auto& cte : lhs_query->ctes) {
      if (cte->bound_hash) {
        lhs_cte_map[{*cte->bound_hash, cte->def_columns}] = cte;
      }
    }
    for (auto it = rhs_query->ctes.begin(); it != rhs_query->ctes.end();) {
      const auto& cte = *it;
      if (cte->bound_hash) {
        auto key = std::make_pair(*cte->bound_hash, cte->def_columns);
        auto existing = lhs_cte_map.find(key);
        if (existing != lhs_cte_map.end()) {
          sql::ast::SourceReplacer replacer(cte->Alias(), existing->second);
          rhs_sourceable->Accept(replacer);
          it = rhs_query->ctes.erase(it);
          continue;
        }
      }
      ++it;
    }
  }

  auto lhs_source = std::make_shared<sql::ast::Source>(lhs_sourceable, GenerateTableAlias());
  auto rhs_source = std::make_shared<sql::ast::Source>(rhs_sourceable, GenerateTableAlias());
  if (auto lhs_sel = std::dynamic_pointer_cast<sql::ast::Select>(lhs_sourceable)) {
    if (lhs_sel->group_by.has_value()) lhs_source->inhibit_subquery_flatten = true;
  }
  if (auto rhs_sel = std::dynamic_pointer_cast<sql::ast::Select>(rhs_sourceable)) {
    if (rhs_sel->group_by.has_value()) rhs_source->inhibit_subquery_flatten = true;
  }

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

  auto select = std::make_shared<sql::ast::Select>(select_cols, from);
  node->sql_expression = select;
  return node;
}

std::shared_ptr<RelFormula> Translator::Visit(const std::shared_ptr<RelDisjunction>& node) {
  if (!node->lhs || !node->rhs) return nullptr;

  Visit(node->lhs);
  Visit(node->rhs);

  auto lhs_sourceable = ExpectSourceable(node->lhs->sql_expression);
  auto rhs_sourceable = ExpectSourceable(node->rhs->sql_expression);

  const std::set<std::string>& fv1 = node->lhs->free_variables;
  const std::set<std::string>& fv2 = node->rhs->free_variables;
  std::set<std::string> all_fv(fv1.begin(), fv1.end());
  all_fv.insert(fv2.begin(), fv2.end());

  // Safety check: FV(F1) ∪ FV(F2) ⊆ bound(F)
  for (const auto& var : all_fv) {
    if (!node->safety.bound_variables.count(var)) {
      throw TranslationException("Disjunction: variable '" + var + "' is not bound (safety check failed)",
                                 ErrorCode::UNBALANCED_VARIABLE, SourceLocation(0, 0));
    }
  }

  // Symmetric difference: vars that appear in only one disjunct
  std::set<std::string> sym_diff;
  for (const auto& var : fv1) {
    if (!fv2.count(var)) sym_diff.insert(var);
  }
  for (const auto& var : fv2) {
    if (!fv1.count(var)) sym_diff.insert(var);
  }

  if (sym_diff.empty()) {
    // Same free variables: simple UNION
    node->sql_expression = std::make_shared<sql::ast::Union>(lhs_sourceable, rhs_sourceable);
    return node;
  }

  // Different free variables: need CTEs for sym_diff. Get cover from safety.
  BoundSet cover = node->safety.SmallCover();
  std::vector<std::shared_ptr<sql::ast::Source>> cte_sources;
  std::vector<std::pair<std::shared_ptr<sql::ast::Source>, std::set<std::string>>> cte_source_var_pairs;

  for (const auto& bound : cover.bounds) {
    bool has_sym_diff_var = false;
    for (const auto& var : bound.variables) {
      if (sym_diff.count(var)) {
        has_sym_diff_var = true;
        break;
      }
    }
    if (!has_sym_diff_var || !bound.domain) continue;

    auto domain_sql = DomainToSql(*bound.domain);
    std::set<std::string> bound_vars(bound.variables.begin(), bound.variables.end());
    std::vector<std::string> def_cols(bound.variables.begin(), bound.variables.end());
    auto cte_source = std::make_shared<sql::ast::Source>(domain_sql, GenerateTableAlias("E"), true, def_cols);
    cte_sources.push_back(cte_source);
    cte_source_var_pairs.push_back({cte_source, bound_vars});
  }

  auto lhs_source = std::make_shared<sql::ast::Source>(lhs_sourceable, GenerateTableAlias());
  auto rhs_source = std::make_shared<sql::ast::Source>(rhs_sourceable, GenerateTableAlias());
  node->lhs->sql_expression = lhs_source;
  node->rhs->sql_expression = rhs_source;

  // Build EQ for branch 1: T1 (lhs) + CTEs
  std::vector<std::pair<std::shared_ptr<sql::ast::Source>, std::set<std::string>>> branch1_pairs;
  branch1_pairs.push_back({lhs_source, fv1});
  for (const auto& p : cte_source_var_pairs) branch1_pairs.push_back(p);
  auto eq1 = BuildEqualityForSources(branch1_pairs);

  // Build EQ for branch 2: T2 (rhs) + CTEs
  std::vector<std::pair<std::shared_ptr<sql::ast::Source>, std::set<std::string>>> branch2_pairs;
  branch2_pairs.push_back({rhs_source, fv2});
  for (const auto& p : cte_source_var_pairs) branch2_pairs.push_back(p);
  auto eq2 = BuildEqualityForSources(branch2_pairs);

  // Build SELECT for branch 1: use T1 for vars in FV(F1), CTEs for vars only in FV(F2)
  // Use sorted order for consistent column ordering across UNION branches
  std::vector<std::string> ordered_vars(all_fv.begin(), all_fv.end());
  std::sort(ordered_vars.begin(), ordered_vars.end());

  std::vector<std::shared_ptr<sql::ast::Selectable>> select1;
  for (const auto& var : ordered_vars) {
    std::shared_ptr<sql::ast::Source> src;
    if (fv1.count(var)) {
      src = lhs_source;
    } else {
      for (const auto& [cte_src, vars] : cte_source_var_pairs) {
        if (vars.count(var)) {
          src = cte_src;
          break;
        }
      }
    }
    if (src) {
      auto col = std::make_shared<sql::ast::Column>(var, src);
      select1.push_back(std::make_shared<sql::ast::TermSelectable>(col));
    }
  }

  // Build SELECT for branch 2: use CTEs for vars only in FV(F1), T2 for vars in FV(F2)
  std::vector<std::shared_ptr<sql::ast::Selectable>> select2;
  for (const auto& var : ordered_vars) {
    std::shared_ptr<sql::ast::Source> src;
    if (fv2.count(var)) {
      src = rhs_source;
    } else {
      for (const auto& [cte_src, vars] : cte_source_var_pairs) {
        if (vars.count(var)) {
          src = cte_src;
          break;
        }
      }
    }
    if (src) {
      auto col = std::make_shared<sql::ast::Column>(var, src);
      select2.push_back(std::make_shared<sql::ast::TermSelectable>(col));
    }
  }

  std::vector<std::shared_ptr<sql::ast::Source>> from1 = {lhs_source};
  from1.insert(from1.end(), cte_sources.begin(), cte_sources.end());
  std::vector<std::shared_ptr<sql::ast::Source>> from2 = {rhs_source};
  from2.insert(from2.end(), cte_sources.begin(), cte_sources.end());

  auto select_stmt1 = std::make_shared<sql::ast::Select>(select1, std::make_shared<sql::ast::From>(from1, eq1), true);
  auto select_stmt2 = std::make_shared<sql::ast::Select>(select2, std::make_shared<sql::ast::From>(from2, eq2), true);

  node->sql_expression = std::make_shared<sql::ast::Union>(select_stmt1, select_stmt2, cte_sources, false);
  return node;
}

std::shared_ptr<RelFormula> Translator::Visit(const std::shared_ptr<RelNegation>& node) {
  if (!node->formula) return nullptr;

  Visit(node->formula);
  auto formula_sourceable = ExpectSourceable(node->formula->sql_expression);
  auto formula_source = std::make_shared<sql::ast::Source>(formula_sourceable, GenerateTableAlias());
  node->formula->sql_expression = formula_source;

  const std::set<std::string>& fv = node->formula->free_variables;

  // Safety check: FV(F1) ⊆ bound(F). Bounds come from parent via InheritSafetyToChildren.
  for (const auto& var : fv) {
    if (!node->safety.bound_variables.count(var)) {
      throw TranslationException("Negation: variable '" + var + "' is not bound (safety check failed)",
                                 ErrorCode::UNBALANCED_VARIABLE, SourceLocation(0, 0));
    }
  }

  BoundSet cover = node->safety.SmallCover();
  std::vector<std::shared_ptr<sql::ast::Source>> cte_sources;
  std::vector<std::pair<std::shared_ptr<sql::ast::Source>, std::set<std::string>>> cte_source_var_pairs;

  for (const auto& bound : cover.bounds) {
    if (!bound.domain) continue;
    auto domain_sql = DomainToSql(*bound.domain);
    std::set<std::string> bound_vars(bound.variables.begin(), bound.variables.end());
    std::vector<std::string> def_cols(bound.variables.begin(), bound.variables.end());
    auto cte_source = std::make_shared<sql::ast::Source>(domain_sql, GenerateTableAlias("E"), true, def_cols);
    cte_sources.push_back(cte_source);
    cte_source_var_pairs.push_back({cte_source, bound_vars});
  }

  // Build EQ for CTEs
  auto eq = BuildEqualityForSources(cte_source_var_pairs);

  // Build output columns and NOT IN tuple from CTEs (one column per var in FV)
  std::vector<std::string> ordered_vars(fv.begin(), fv.end());
  std::sort(ordered_vars.begin(), ordered_vars.end());

  std::vector<std::shared_ptr<sql::ast::Selectable>> select_cols;
  std::vector<std::shared_ptr<sql::ast::Column>> not_in_columns;
  for (const auto& var : ordered_vars) {
    for (const auto& [cte_src, vars] : cte_source_var_pairs) {
      if (vars.count(var)) {
        auto col = std::make_shared<sql::ast::Column>(var, cte_src);
        select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(col));
        not_in_columns.push_back(col);
        break;
      }
    }
  }

  // Build NOT IN subquery: SELECT * FROM F1° (formula already has correct columns)
  auto not_in_select = std::make_shared<sql::ast::Select>(
      std::vector<std::shared_ptr<sql::ast::Selectable>>{std::make_shared<sql::ast::Wildcard>()},
      std::make_shared<sql::ast::From>(formula_source), true);
  auto inclusion = std::make_shared<sql::ast::Inclusion>(not_in_columns, not_in_select, true);

  std::vector<std::shared_ptr<sql::ast::Condition>> where_conditions;
  if (eq) where_conditions.push_back(eq);
  where_conditions.push_back(inclusion);
  auto where = std::make_shared<sql::ast::LogicalCondition>(where_conditions, sql::ast::LogicalOp::AND);

  std::vector<std::shared_ptr<sql::ast::Source>> from_sources(cte_sources.begin(), cte_sources.end());
  auto from = std::make_shared<sql::ast::From>(from_sources, where);
  auto select = std::make_shared<sql::ast::Select>(select_cols, from, cte_sources, false);
  // Propagate CTEs from the negated formula (e.g. if it's a disjunction with CTEs)
  if (auto* q = dynamic_cast<sql::ast::Query*>(formula_sourceable.get())) {
    q->TransferCTEsTo(*select);
  }
  node->sql_expression = select;
  return node;
}

std::shared_ptr<RelFormula> Translator::Visit(const std::shared_ptr<RelParen>& node) {
  if (node->formula) {
    Visit(node->formula);
    node->sql_expression = node->formula->sql_expression;
  }
  return node;
}

namespace {

RelBuiltinAggregateOp MapSqlAggToRelBuiltin(sql::ast::AggregateFunction f) {
  switch (f) {
    case sql::ast::AggregateFunction::SUM:
      return RelBuiltinAggregateOp::SUM;
    case sql::ast::AggregateFunction::COUNT:
      return RelBuiltinAggregateOp::COUNT;
    case sql::ast::AggregateFunction::AVG:
      return RelBuiltinAggregateOp::AVG;
    case sql::ast::AggregateFunction::MIN:
      return RelBuiltinAggregateOp::MIN;
    case sql::ast::AggregateFunction::MAX:
      return RelBuiltinAggregateOp::MAX;
  }
  return RelBuiltinAggregateOp::SUM;
}

std::shared_ptr<RelExpr> GetApplParamExpr(const std::shared_ptr<RelApplParam>& p) {
  if (!p) return nullptr;
  return p->GetExpr();
}

// BuiltinResolver runs before TermRewriter, so lifted atoms may still hold `sum[body]` as a
// partial application rather than RelBuiltinAggregateExpr.
std::shared_ptr<RelBuiltinAggregateExpr> ExtractAggregateFromExpr(std::shared_ptr<RelExpr> expr) {
  if (!expr) return nullptr;
  if (auto eat = std::dynamic_pointer_cast<RelExprAsTerm>(expr)) {
    return ExtractAggregateFromExpr(eat->inner);
  }
  if (auto agg = std::dynamic_pointer_cast<RelBuiltinAggregateExpr>(expr)) return agg;
  auto partial = std::dynamic_pointer_cast<RelPartialApplication>(expr);
  if (!partial || !partial->base) return nullptr;
  auto* id_base = dynamic_cast<RelIDApplBase*>(partial->base.get());
  if (!id_base) return nullptr;
  auto it = GetAggregateMap().find(id_base->id);
  if (it == GetAggregateMap().end()) return nullptr;
  if (partial->params.size() != 1) return nullptr;
  auto body = GetApplParamExpr(partial->params[0]);
  if (!body) return nullptr;
  return std::make_shared<RelBuiltinAggregateExpr>(MapSqlAggToRelBuiltin(it->second), body);
}

bool ExtractDirectAggregateEquality(const RelComparison& cmp, std::string& value_var,
                                    std::shared_ptr<RelBuiltinAggregateExpr>& agg) {
  if (cmp.op != RelCompOp::EQ || !cmp.lhs || !cmp.rhs) return false;
  auto try_sides = [&](const std::shared_ptr<RelTerm>& id_side, const std::shared_ptr<RelTerm>& expr_side) {
    auto* id = dynamic_cast<RelIDTerm*>(id_side.get());
    auto* eat = dynamic_cast<RelExprAsTerm*>(expr_side.get());
    if (!id || !eat || !eat->inner) return false;
    auto extracted = ExtractAggregateFromExpr(eat->inner);
    if (!extracted) return false;
    value_var = id->id;
    agg = extracted;
    return true;
  };
  return try_sides(cmp.lhs, cmp.rhs) || try_sides(cmp.rhs, cmp.lhs);
}

sql::ast::CompOp MapRelCompToSql(RelCompOp op) {
  switch (op) {
    case RelCompOp::EQ:
      return sql::ast::CompOp::EQ;
    case RelCompOp::NEQ:
      return sql::ast::CompOp::NEQ;
    case RelCompOp::LT:
      return sql::ast::CompOp::LT;
    case RelCompOp::GT:
      return sql::ast::CompOp::GT;
    case RelCompOp::LTE:
      return sql::ast::CompOp::LTE;
    case RelCompOp::GTE:
      return sql::ast::CompOp::GTE;
  }
  return sql::ast::CompOp::EQ;
}

bool ExtractValueVsIdbComparison(const RelComparison& cmp, const RelContext& ctx, const std::string& value_var,
                                 std::string& idb_name, sql::ast::CompOp& sql_op) {
  if (!cmp.lhs || !cmp.rhs) return false;
  if (cmp.op != RelCompOp::GT && cmp.op != RelCompOp::GTE && cmp.op != RelCompOp::LT && cmp.op != RelCompOp::LTE) {
    return false;
  }
  auto idb_from_term = [&](const std::shared_ptr<RelTerm>& term) -> std::string {
    if (auto* id = dynamic_cast<RelIDTerm*>(term.get())) {
      return ctx.IsIDB(id->id) ? id->id : "";
    }
    if (auto* eat = dynamic_cast<RelExprAsTerm*>(term.get())) {
      if (auto partial = std::dynamic_pointer_cast<RelPartialApplication>(eat->inner)) {
        if (auto* id_base = dynamic_cast<RelIDApplBase*>(partial->base.get())) {
          return ctx.IsIDB(id_base->id) ? id_base->id : "";
        }
      }
      if (auto* id = dynamic_cast<RelIDTerm*>(eat->inner.get())) {
        return ctx.IsIDB(id->id) ? id->id : "";
      }
    }
    return "";
  };
  auto var_from_term = [&](const std::shared_ptr<RelTerm>& term) -> std::string {
    if (auto* id = dynamic_cast<RelIDTerm*>(term.get())) return id->id;
    return "";
  };
  auto try_sides = [&](const std::shared_ptr<RelTerm>& a, const std::shared_ptr<RelTerm>& b) {
    const std::string var_name = var_from_term(a);
    const std::string idb = idb_from_term(b);
    if (var_name != value_var || idb.empty()) return false;
    idb_name = idb;
    sql_op = MapRelCompToSql(cmp.op);
    return true;
  };
  return try_sides(cmp.lhs, cmp.rhs) || try_sides(cmp.rhs, cmp.lhs);
}

std::pair<sql::ast::AggregateFunction, bool> MapRelAggregateOp(const RelBuiltinAggregateExpr& agg_expr) {
  sql::ast::AggregateFunction fn = sql::ast::AggregateFunction::SUM;
  bool count_all = false;
  switch (agg_expr.op) {
    case RelBuiltinAggregateOp::SUM:
      fn = sql::ast::AggregateFunction::SUM;
      break;
    case RelBuiltinAggregateOp::COUNT:
      fn = sql::ast::AggregateFunction::COUNT;
      count_all = true;
      break;
    case RelBuiltinAggregateOp::AVG:
      fn = sql::ast::AggregateFunction::AVG;
      break;
    case RelBuiltinAggregateOp::MIN:
      fn = sql::ast::AggregateFunction::MIN;
      break;
    case RelBuiltinAggregateOp::MAX:
      fn = sql::ast::AggregateFunction::MAX;
      break;
  }
  return {fn, count_all};
}

std::shared_ptr<RelBuiltinDecimalCastExpr> ExtractDecimalFromLiftedAtom(const RelFormula* lifted_atom) {
  if (!lifted_atom) return nullptr;
  auto* app = dynamic_cast<const RelFullApplication*>(lifted_atom);
  if (!app || !app->base) return nullptr;
  auto* base = dynamic_cast<const RelExprApplBase*>(app->base.get());
  if (!base || !base->expr) return nullptr;
  std::shared_ptr<RelExpr> domain_expr = base->expr;
  if (auto uni = std::dynamic_pointer_cast<RelUnion>(domain_expr)) {
    if (!uni->exprs.empty()) domain_expr = uni->exprs.front();
  }
  if (auto eat = std::dynamic_pointer_cast<RelExprAsTerm>(domain_expr)) {
    domain_expr = eat->inner;
  }
  return std::dynamic_pointer_cast<RelBuiltinDecimalCastExpr>(domain_expr);
}

struct ScalarDecimalSumLift {
  std::string export_var;
  std::shared_ptr<RelBuiltinDecimalCastExpr> decimal;
  std::shared_ptr<RelBuiltinAggregateExpr> sum;
};

std::optional<ScalarDecimalSumLift> ParseScalarDecimalSumLift(const std::shared_ptr<RelFormula>& formula) {
  if (!formula) return std::nullopt;
  auto flat = FlattenConjunctionChain(formula);
  if (flat.size() != 3) return std::nullopt;

  std::shared_ptr<RelComparison> mul_cmp;
  const RelFullApplication* decimal_app = nullptr;
  const RelFullApplication* sum_app = nullptr;

  for (const auto& conjunct : flat) {
    if (auto cmp = std::dynamic_pointer_cast<RelComparison>(conjunct)) {
      if (mul_cmp) return std::nullopt;
      mul_cmp = cmp;
      continue;
    }
    auto* app = dynamic_cast<const RelFullApplication*>(conjunct.get());
    if (!app) return std::nullopt;
    if (ExtractDecimalFromLiftedAtom(app)) {
      if (decimal_app) return std::nullopt;
      decimal_app = app;
    } else if (ExtractAggregateFromLiftedAtom(app)) {
      if (sum_app) return std::nullopt;
      sum_app = app;
    } else {
      return std::nullopt;
    }
  }
  if (!mul_cmp || !decimal_app || !sum_app || mul_cmp->op != RelCompOp::EQ) return std::nullopt;

  auto parse_mul_ids = [](const std::shared_ptr<RelTerm>& term, std::string& left, std::string& right) {
    auto peeled = PeelRelParenthesisTerm(term);
    auto* op = dynamic_cast<RelOpTerm*>(peeled.get());
    if (!op || op->op != RelTermOp::MUL) return false;
    auto* lhs = dynamic_cast<RelIDTerm*>(op->lhs.get());
    auto* rhs = dynamic_cast<RelIDTerm*>(op->rhs.get());
    if (!lhs || !rhs) return false;
    left = lhs->id;
    right = rhs->id;
    return true;
  };

  std::string mul_lhs;
  std::string mul_rhs;
  const RelIDTerm* export_id = nullptr;
  if (parse_mul_ids(mul_cmp->rhs, mul_lhs, mul_rhs)) {
    export_id = dynamic_cast<RelIDTerm*>(mul_cmp->lhs.get());
  } else if (parse_mul_ids(mul_cmp->lhs, mul_lhs, mul_rhs)) {
    export_id = dynamic_cast<RelIDTerm*>(mul_cmp->rhs.get());
  } else {
    return std::nullopt;
  }
  if (!export_id) return std::nullopt;

  auto* dec_param =
      dynamic_cast<const RelIDTerm*>(decimal_app->params[0] ? decimal_app->params[0]->GetExpr().get() : nullptr);
  auto* sum_param = dynamic_cast<const RelIDTerm*>(sum_app->params[0] ? sum_app->params[0]->GetExpr().get() : nullptr);
  if (!dec_param || !sum_param) return std::nullopt;
  const bool vars_match =
      (dec_param->id == mul_lhs && sum_param->id == mul_rhs) || (dec_param->id == mul_rhs && sum_param->id == mul_lhs);
  if (!vars_match) return std::nullopt;

  ScalarDecimalSumLift out;
  out.export_var = export_id->id;
  out.decimal = ExtractDecimalFromLiftedAtom(decimal_app);
  out.sum = ExtractAggregateFromLiftedAtom(sum_app);
  if (!out.decimal || !out.sum || !out.sum->body) return std::nullopt;
  return out;
}

struct AggregateExportMatch {
  std::string value_var;
  std::shared_ptr<RelBuiltinAggregateExpr> agg;
};

bool IsLiftedBindingConjunction(const RelConjunction& node) {
  std::function<const RelFullApplication*(const std::shared_ptr<RelFormula>&)> find_lift_app;
  find_lift_app = [&](const std::shared_ptr<RelFormula>& f) -> const RelFullApplication* {
    if (!f) return nullptr;
    if (auto* app = dynamic_cast<const RelFullApplication*>(f.get())) return app;
    if (auto* c = dynamic_cast<const RelConjunction*>(f.get())) {
      if (auto* a = find_lift_app(c->lhs)) return a;
      if (auto* a = find_lift_app(c->rhs)) return a;
    }
    return nullptr;
  };

  const RelFullApplication* lhs_app = find_lift_app(node.lhs);
  auto* rhs_cmp = dynamic_cast<const RelComparison*>(node.rhs.get());
  if (!lhs_app || !rhs_cmp || !lhs_app->base || lhs_app->params.size() != 1) return false;

  auto* param = dynamic_cast<RelExprApplParam*>(lhs_app->params[0].get());
  if (!param || !param->expr) return false;
  auto* param_id = dynamic_cast<RelIDTerm*>(param->expr.get());
  if (!param_id) return false;

  auto* cmp_lhs = dynamic_cast<RelIDTerm*>(rhs_cmp->lhs.get());
  auto* cmp_rhs = dynamic_cast<RelIDTerm*>(rhs_cmp->rhs.get());
  const bool lhs_is_param = cmp_lhs && cmp_lhs->id == param_id->id;
  const bool rhs_is_param = cmp_rhs && cmp_rhs->id == param_id->id;
  if (!lhs_is_param && !rhs_is_param) return false;
  if (lhs_is_param && !cmp_rhs) return false;
  if (rhs_is_param && !cmp_lhs) return false;

  auto* expr_base = dynamic_cast<RelExprApplBase*>(lhs_app->base.get());
  if (!expr_base || !expr_base->expr) return false;
  auto* uni = dynamic_cast<const RelUnion*>(expr_base->expr.get());
  return uni && !uni->exprs.empty();
}

std::optional<AggregateExportMatch> FindAggregateExportEquality(const std::shared_ptr<RelNode>& node) {
  if (auto cmp = std::dynamic_pointer_cast<RelComparison>(node)) {
    std::string vv;
    std::shared_ptr<RelBuiltinAggregateExpr> a;
    if (ExtractDirectAggregateEquality(*cmp, vv, a)) return AggregateExportMatch{vv, a};
  }
  auto conj = std::dynamic_pointer_cast<RelConjunction>(node);
  if (!conj || !conj->lhs || !conj->rhs) {
    auto ex = std::dynamic_pointer_cast<RelExistential>(node);
    if (!ex || !ex->formula) return std::nullopt;
    conj = std::dynamic_pointer_cast<RelConjunction>(ex->formula);
  }
  if (!conj || !conj->lhs || !conj->rhs) return std::nullopt;
  if (!IsLiftedBindingConjunction(*conj)) return std::nullopt;
  auto cmp = std::dynamic_pointer_cast<RelComparison>(conj->rhs);
  if (!cmp || cmp->op != RelCompOp::EQ || !cmp->lhs || !cmp->rhs) return std::nullopt;
  auto* lhs_id = dynamic_cast<RelIDTerm*>(cmp->lhs.get());
  auto* rhs_id = dynamic_cast<RelIDTerm*>(cmp->rhs.get());
  if (!lhs_id || !rhs_id) return std::nullopt;

  std::function<const RelFullApplication*(const std::shared_ptr<RelFormula>&)> find_lift_app;
  find_lift_app = [&](const std::shared_ptr<RelFormula>& f) -> const RelFullApplication* {
    if (!f) return nullptr;
    if (auto* app = dynamic_cast<const RelFullApplication*>(f.get())) return app;
    if (auto* c = dynamic_cast<const RelConjunction*>(f.get())) {
      if (auto* a = find_lift_app(c->lhs)) return a;
      if (auto* a = find_lift_app(c->rhs)) return a;
    }
    return nullptr;
  };
  const RelFullApplication* app = find_lift_app(conj->lhs);
  if (!app) return std::nullopt;
  auto agg = ExtractAggregateFromLiftedAtom(app);
  if (!agg || !agg->body) return std::nullopt;
  if (app->params.size() != 1) return std::nullopt;
  auto* param = dynamic_cast<const RelExprApplParam*>(app->params[0].get());
  if (!param || !param->expr) return std::nullopt;
  auto* param_id = dynamic_cast<const RelIDTerm*>(param->expr.get());
  if (!param_id) return std::nullopt;
  std::string export_var;
  if (lhs_id->id == param_id->id) {
    export_var = rhs_id->id;
  } else if (rhs_id->id == param_id->id) {
    export_var = lhs_id->id;
  } else {
    return std::nullopt;
  }
  return AggregateExportMatch{export_var, agg};
}

std::optional<AggregateThresholdPattern> FindAggregateThresholdPattern(const RelContext& ctx,
                                                                       const std::shared_ptr<RelNode>& root) {
  std::optional<AggregateExportMatch> agg_match;
  std::optional<AggregateThresholdPattern> thresh_match;
  std::string value_var;

  std::function<void(const std::shared_ptr<RelNode>&)> walk = [&](const std::shared_ptr<RelNode>& node) {
    if (!node) return;
    if (!agg_match) {
      if (auto found = FindAggregateExportEquality(node)) {
        agg_match = found;
        value_var = found->value_var;
      }
    }
    if (auto cmp = std::dynamic_pointer_cast<RelComparison>(node)) {
      std::string idb;
      sql::ast::CompOp op;
      if (value_var.empty()) {
        auto infer_idb = [&](const std::shared_ptr<RelTerm>& term) -> std::string {
          if (auto* id = dynamic_cast<RelIDTerm*>(term.get())) {
            return ctx.IsIDB(id->id) ? id->id : "";
          }
          if (auto* eat = dynamic_cast<RelExprAsTerm*>(term.get())) {
            if (auto partial = std::dynamic_pointer_cast<RelPartialApplication>(eat->inner)) {
              if (auto* id_base = dynamic_cast<RelIDApplBase*>(partial->base.get())) {
                return ctx.IsIDB(id_base->id) ? id_base->id : "";
              }
            }
          }
          return "";
        };
        auto* lhs = dynamic_cast<RelIDTerm*>(cmp->lhs.get());
        auto* rhs = dynamic_cast<RelIDTerm*>(cmp->rhs.get());
        const std::string idb_rhs = infer_idb(cmp->rhs);
        const std::string idb_lhs = infer_idb(cmp->lhs);
        if (lhs && !idb_rhs.empty() &&
            (cmp->op == RelCompOp::GT || cmp->op == RelCompOp::GTE || cmp->op == RelCompOp::LT ||
             cmp->op == RelCompOp::LTE)) {
          value_var = lhs->id;
        } else if (rhs && !idb_lhs.empty() &&
                   (cmp->op == RelCompOp::GT || cmp->op == RelCompOp::GTE || cmp->op == RelCompOp::LT ||
                    cmp->op == RelCompOp::LTE)) {
          value_var = rhs->id;
        }
      }
      if (!value_var.empty() && !thresh_match && ExtractValueVsIdbComparison(*cmp, ctx, value_var, idb, op)) {
        thresh_match = AggregateThresholdPattern{value_var, nullptr, idb, op};
      }
    }
    for (const auto& ch : node->Children()) {
      walk(ch);
    }
  };

  walk(root);
  if (!agg_match || !thresh_match) return std::nullopt;
  AggregateThresholdPattern out = *thresh_match;
  out.agg = agg_match->agg;
  out.value_var = agg_match->value_var;
  return out;
}

}  // namespace

bool Translator::TryEmitScalarAggregateMul(std::shared_ptr<RelExpr> scalar_side, std::shared_ptr<RelExpr> agg_side,
                                           RelNode& ctx, std::shared_ptr<sql::ast::Select>& out) {
  while (auto eat = std::dynamic_pointer_cast<RelExprAsTerm>(agg_side)) {
    agg_side = eat->inner;
  }
  while (auto eat = std::dynamic_pointer_cast<RelExprAsTerm>(scalar_side)) {
    scalar_side = eat->inner;
  }
  auto agg = ExtractAggregateFromExpr(agg_side);
  if (!agg || !agg->body || ExtractAggregateFromExpr(scalar_side)) return false;

  Visit(scalar_side);
  ScalarSqlTerm scalar;
  try {
    scalar = ExtractScalarSqlTerm(ctx, scalar_side);
  } catch (...) {
    return false;
  }
  if (!scalar.from_sources.empty()) return false;

  const auto [fn, count_all] = MapRelAggregateOp(*agg);
  auto agg_select = VisitAggregateRel(agg->body, fn, count_all);
  agg->sql_expression = agg_select;
  agg_side->sql_expression = agg_select;

  auto agg_src = std::make_shared<sql::ast::Source>(agg_select, GenerateTableAlias());
  const std::string sum_col = GetColumnNameForSourceable(agg_select, agg_select->columns.size());
  auto sum_column = std::make_shared<sql::ast::Column>(sum_col, agg_src);
  auto product = std::make_shared<sql::ast::Operation>(scalar.term, sum_column, "*");
  out = std::make_shared<sql::ast::Select>(
      std::vector<std::shared_ptr<sql::ast::Selectable>>{std::make_shared<sql::ast::TermSelectable>(product, "A1")},
      std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{agg_src}));
  return true;
}

bool Translator::TryEmitScalarAggregateProduct(const std::shared_ptr<RelProduct>& node) {
  if (node->exprs.size() != 2) return false;
  for (int swap = 0; swap < 2; ++swap) {
    std::shared_ptr<sql::ast::Select> out;
    if (TryEmitScalarAggregateMul(node->exprs[swap ? 1 : 0], node->exprs[swap ? 0 : 1], *node, out)) {
      node->sql_expression = out;
      node->arity = 1;
      return true;
    }
  }
  return false;
}

bool Translator::TryEmitScalarAggregateMulTerm(const std::shared_ptr<RelOpTerm>& node) {
  if (node->op != RelTermOp::MUL || !node->lhs || !node->rhs) return false;
  std::shared_ptr<sql::ast::Select> out;
  if (TryEmitScalarAggregateMul(node->lhs, node->rhs, *node, out) ||
      TryEmitScalarAggregateMul(node->rhs, node->lhs, *node, out)) {
    node->sql_expression = out;
    node->arity = 1;
    return true;
  }
  return false;
}

bool Translator::TryEmitScalarAggregateDiv(std::shared_ptr<RelExpr> agg_side, std::shared_ptr<RelExpr> divisor_side,
                                           RelNode& ctx, std::shared_ptr<sql::ast::Select>& out) {
  while (auto eat = std::dynamic_pointer_cast<RelExprAsTerm>(agg_side)) {
    agg_side = eat->inner;
  }
  while (auto eat = std::dynamic_pointer_cast<RelExprAsTerm>(divisor_side)) {
    divisor_side = eat->inner;
  }
  auto agg = ExtractAggregateFromExpr(agg_side);
  if (!agg || !agg->body || ExtractAggregateFromExpr(divisor_side)) return false;

  ScalarSqlTerm divisor;
  if (auto num = std::dynamic_pointer_cast<RelNumTerm>(divisor_side)) {
    if (!num->sql_expression) Visit(num);
    auto constant = std::dynamic_pointer_cast<sql::ast::Constant>(num->sql_expression);
    if (!constant) return false;
    divisor.term = constant;
  } else {
    Visit(divisor_side);
    try {
      divisor = ExtractScalarSqlTerm(ctx, divisor_side);
    } catch (...) {
      return false;
    }
    if (!divisor.from_sources.empty()) return false;
  }

  const auto [fn, count_all] = MapRelAggregateOp(*agg);
  auto agg_select = VisitAggregateRel(agg->body, fn, count_all);
  agg->sql_expression = agg_select;
  agg_side->sql_expression = agg_select;

  auto agg_src = std::make_shared<sql::ast::Source>(agg_select, GenerateTableAlias());
  agg_src->inhibit_subquery_flatten = true;
  const std::string sum_col = GetColumnNameForSourceable(agg_select, agg_select->columns.size());
  auto sum_column = std::make_shared<sql::ast::Column>(sum_col, agg_src);
  auto quotient = std::make_shared<sql::ast::Operation>(sum_column, divisor.term, "/");
  out = std::make_shared<sql::ast::Select>(
      std::vector<std::shared_ptr<sql::ast::Selectable>>{std::make_shared<sql::ast::TermSelectable>(quotient, "A1")},
      std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{agg_src}));
  return true;
}

bool Translator::TryEmitScalarAggregateDivTerm(const std::shared_ptr<RelOpTerm>& node) {
  if (node->op != RelTermOp::DIV || !node->lhs || !node->rhs) return false;
  std::shared_ptr<sql::ast::Select> out;
  if (TryEmitScalarAggregateDiv(node->lhs, node->rhs, *node, out)) {
    node->sql_expression = out;
    node->arity = 1;
    return true;
  }
  return false;
}

bool Translator::TryEmitScalarAggregateDivExistential(const std::shared_ptr<RelExistential>& node) {
  auto parsed = ParseScalarAggregateDivLift(node->formula);
  if (!parsed) return false;

  std::shared_ptr<sql::ast::Select> out;
  if (!TryEmitScalarAggregateDiv(parsed->agg, parsed->divisor, *node, out)) return false;
  if (!out->columns.empty()) {
    if (auto* ts = dynamic_cast<sql::ast::TermSelectable*>(out->columns[0].get())) {
      ts->alias = parsed->export_var;
    }
  }
  node->sql_expression = out;
  return true;
}

bool Translator::TryEmitScalarDecimalSumExistential(const std::shared_ptr<RelExistential>& node) {
  auto parsed = ParseScalarDecimalSumLift(node->formula);
  if (!parsed) return false;

  Visit(parsed->decimal);
  ScalarSqlTerm scalar;
  try {
    scalar = ExtractScalarSqlTerm(*node, parsed->decimal);
  } catch (...) {
    return false;
  }
  if (!scalar.from_sources.empty()) return false;

  const auto [fn, count_all] = MapRelAggregateOp(*parsed->sum);
  auto agg_select = VisitAggregateRel(parsed->sum->body, fn, count_all);
  parsed->sum->sql_expression = agg_select;

  auto agg_src = std::make_shared<sql::ast::Source>(agg_select, GenerateTableAlias());
  const std::string sum_col = GetColumnNameForSourceable(agg_select, agg_select->columns.size());
  auto sum_column = std::make_shared<sql::ast::Column>(sum_col, agg_src);
  auto product = std::make_shared<sql::ast::Operation>(scalar.term, sum_column, "*");
  node->sql_expression = std::make_shared<sql::ast::Select>(
      std::vector<std::shared_ptr<sql::ast::Selectable>>{
          std::make_shared<sql::ast::TermSelectable>(product, parsed->export_var)},
      std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{agg_src}));
  return true;
}

std::shared_ptr<sql::ast::Select> Translator::EmitAggregateExportSelect(
    const std::shared_ptr<RelBuiltinAggregateExpr>& agg_expr, const std::string& export_var) {
  const auto [fn, count_all] = MapRelAggregateOp(*agg_expr);
  auto inner_select = VisitAggregateRel(agg_expr->body, fn, count_all);
  agg_expr->sql_expression = inner_select;

  auto wrapped = std::make_shared<sql::ast::Source>(inner_select, GenerateTableAlias());
  wrapped->inhibit_subquery_flatten = true;
  const std::string result_col = GetColumnNameForSourceable(inner_select, inner_select->columns.size());

  std::vector<std::shared_ptr<sql::ast::Selectable>> select_cols;
  std::set<std::string> group_keys = agg_expr->body->free_variables;
  if (group_keys.empty()) group_keys = ComputeAggregateGroupKeys(agg_expr->body);
  for (const auto& var : group_keys) {
    auto col_name = ResolveOutputColumnNameForVariableOnSource(wrapped, var);
    auto col = std::make_shared<sql::ast::Column>(col_name, wrapped);
    select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(col, var));
  }
  auto result_col_term = std::make_shared<sql::ast::Column>(result_col, wrapped);
  select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(result_col_term, export_var));

  return std::make_shared<sql::ast::Select>(
      select_cols, std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{wrapped}));
}

std::shared_ptr<sql::ast::Expression> Translator::TryEmitAggregateEqualityWithIdbThresholdConjunction(
    const std::vector<std::shared_ptr<RelNode>>& subformulas) {
  if (subformulas.size() < 2) return nullptr;

  std::optional<size_t> eq_idx;
  std::optional<size_t> thresh_idx;
  std::string value_var;
  std::string idb_name;
  sql::ast::CompOp thresh_op = sql::ast::CompOp::GT;
  std::shared_ptr<RelBuiltinAggregateExpr> agg;

  for (size_t i = 0; i < subformulas.size(); ++i) {
    if (auto found = FindAggregateExportEquality(subformulas[i])) {
      if (eq_idx) return nullptr;
      eq_idx = i;
      value_var = found->value_var;
      agg = found->agg;
      continue;
    }
    auto cmp = std::dynamic_pointer_cast<RelComparison>(subformulas[i]);
    if (!cmp) return nullptr;
    std::string idb;
    sql::ast::CompOp op;
    if (value_var.empty()) {
      // threshold conjunct may appear before aggregate equality in the chain
      auto* lhs = dynamic_cast<RelIDTerm*>(cmp->lhs.get());
      auto* rhs = dynamic_cast<RelIDTerm*>(cmp->rhs.get());
      if (lhs && rhs && context_.IsIDB(rhs->id)) {
        value_var = lhs->id;
      } else if (lhs && rhs && context_.IsIDB(lhs->id)) {
        value_var = rhs->id;
      }
    }
    if (value_var.empty() || !ExtractValueVsIdbComparison(*cmp, context_, value_var, idb, op)) return nullptr;
    if (thresh_idx) return nullptr;
    thresh_idx = i;
    idb_name = idb;
    thresh_op = op;
  }
  if (!eq_idx || !thresh_idx || !agg) return nullptr;

  auto export_sel = EmitAggregateExportSelect(agg, value_var);
  auto inner_src = std::make_shared<sql::ast::Source>(export_sel, GenerateTableAlias());
  inner_src->inhibit_subquery_flatten = true;
  auto thresh_src = std::make_shared<sql::ast::Source>(
      std::make_shared<sql::ast::Table>(idb_name, context_.GetArity(idb_name)), GenerateTableAlias());

  auto value_col = std::make_shared<sql::ast::Column>(value_var, inner_src);
  auto thresh_col = std::make_shared<sql::ast::Column>("A1", thresh_src);
  auto cond = std::make_shared<sql::ast::ComparisonCondition>(value_col, thresh_op, thresh_col);

  std::vector<std::shared_ptr<sql::ast::Selectable>> select_cols;
  for (const auto& var : ComputeAggregateGroupKeys(agg->body)) {
    if (var == value_var) continue;
    auto col_name = ResolveOutputColumnNameForVariableOnSource(inner_src, var);
    auto col = std::make_shared<sql::ast::Column>(col_name, inner_src);
    select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(col, var));
  }
  select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(value_col, value_var));

  return std::make_shared<sql::ast::Select>(
      select_cols,
      std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{inner_src, thresh_src}, cond));
}

std::shared_ptr<RelBuiltinAggregateExpr> ExtractAggregateFromLiftedAtom(const RelFormula* lifted_atom) {
  if (!lifted_atom) return nullptr;
  auto* app = dynamic_cast<const RelFullApplication*>(lifted_atom);
  if (!app || !app->base) return nullptr;
  auto* base = dynamic_cast<const RelExprApplBase*>(app->base.get());
  if (!base || !base->expr) return nullptr;
  std::shared_ptr<RelExpr> domain_expr = base->expr;
  if (auto uni = std::dynamic_pointer_cast<RelUnion>(domain_expr)) {
    if (!uni->exprs.empty()) domain_expr = uni->exprs.front();
  }
  return ExtractAggregateFromExpr(domain_expr);
}

bool Translator::TryEmitFilteredAggregateComparison(const std::shared_ptr<RelComparison>& node,
                                                    const std::shared_ptr<RelBuiltinAggregateExpr>& agg_expr) {
  if (!agg_expr || !agg_expr->body) return false;
  if (!dynamic_cast<RelNumTerm*>(node->rhs.get()) && !node->rhs->constant.has_value()) return false;

  sql::ast::AggregateFunction fn = sql::ast::AggregateFunction::SUM;
  bool count_all = false;
  switch (agg_expr->op) {
    case RelBuiltinAggregateOp::SUM:
      fn = sql::ast::AggregateFunction::SUM;
      break;
    case RelBuiltinAggregateOp::COUNT:
      fn = sql::ast::AggregateFunction::COUNT;
      count_all = true;
      break;
    case RelBuiltinAggregateOp::AVG:
      fn = sql::ast::AggregateFunction::AVG;
      break;
    case RelBuiltinAggregateOp::MIN:
      fn = sql::ast::AggregateFunction::MIN;
      break;
    case RelBuiltinAggregateOp::MAX:
      fn = sql::ast::AggregateFunction::MAX;
      break;
  }

  auto inner_select = VisitAggregateRel(agg_expr->body, fn, count_all);
  agg_expr->sql_expression = inner_select;

  Visit(node->rhs);
  auto rhs_sql = BuildSqlTermFromLinearRelTerm(node->rhs, {});
  if (!rhs_sql) return false;

  auto wrapped = std::make_shared<sql::ast::Source>(inner_select, GenerateTableAlias());
  std::string result_col = GetColumnNameForSourceable(inner_select, inner_select->columns.size());
  auto lhs_sql = std::make_shared<sql::ast::Column>(result_col, wrapped);

  sql::ast::CompOp sql_op;
  switch (node->op) {
    case RelCompOp::EQ:
      sql_op = sql::ast::CompOp::EQ;
      break;
    case RelCompOp::NEQ:
      sql_op = sql::ast::CompOp::NEQ;
      break;
    case RelCompOp::LT:
      sql_op = sql::ast::CompOp::LT;
      break;
    case RelCompOp::GT:
      sql_op = sql::ast::CompOp::GT;
      break;
    case RelCompOp::LTE:
      sql_op = sql::ast::CompOp::LTE;
      break;
    case RelCompOp::GTE:
      sql_op = sql::ast::CompOp::GTE;
      break;
  }
  auto comp_cond = std::make_shared<sql::ast::ComparisonCondition>(lhs_sql, sql_op, rhs_sql);
  auto from = std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{wrapped}, comp_cond);

  std::vector<std::shared_ptr<sql::ast::Selectable>> select_cols;
  for (const auto& var : agg_expr->body->free_variables) {
    auto col_name = ResolveOutputColumnNameForVariableOnSource(wrapped, var);
    auto col = std::make_shared<sql::ast::Column>(col_name, wrapped);
    select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(col, var));
  }

  node->sql_expression = std::make_shared<sql::ast::Select>(select_cols, from);
  return true;
}

bool Translator::TryTranslateAggregateConstantComparison(const std::shared_ptr<RelComparison>& node,
                                                         const BoundSet& cover,
                                                         const std::shared_ptr<RelFormula>& lifted_atom) {
  (void)cover;
  if (!lifted_atom) return false;
  if (!dynamic_cast<RelIDTerm*>(node->lhs.get())) return false;
  auto agg_expr = ExtractAggregateFromLiftedAtom(lifted_atom.get());
  return TryEmitFilteredAggregateComparison(node, agg_expr);
}

bool Translator::TryTranslateAggregateVariableEquality(const std::shared_ptr<RelComparison>& node,
                                                       const BoundSet& cover,
                                                       const std::shared_ptr<RelFormula>& lifted_atom) {
  (void)cover;
  if (!lifted_atom || node->op != RelCompOp::EQ) return false;

  auto* lhs_id = dynamic_cast<RelIDTerm*>(node->lhs.get());
  auto* rhs_id = dynamic_cast<RelIDTerm*>(node->rhs.get());
  if (!lhs_id || !rhs_id) return false;

  auto* app = dynamic_cast<const RelFullApplication*>(lifted_atom.get());
  if (!app || app->params.size() != 1) return false;
  auto* param = dynamic_cast<const RelExprApplParam*>(app->params[0].get());
  if (!param || !param->expr) return false;
  auto* param_id = dynamic_cast<const RelIDTerm*>(param->expr.get());
  if (!param_id) return false;

  const RelIDTerm* export_id = nullptr;
  if (lhs_id->id == param_id->id) {
    export_id = rhs_id;
  } else if (rhs_id->id == param_id->id) {
    export_id = lhs_id;
  } else {
    return false;
  }

  std::shared_ptr<RelBuiltinAggregateExpr> agg_expr = ExtractAggregateFromLiftedAtom(lifted_atom.get());
  if (!agg_expr || !agg_expr->body) return false;

  node->sql_expression = EmitAggregateExportSelect(agg_expr, export_id->id);
  return true;
}

bool Translator::TryEmitLiftedPartialAppLiteralEquality(const std::shared_ptr<RelComparison>& node,
                                                        const std::shared_ptr<RelFormula>& lifted_atom) {
  if (!node || node->op != RelCompOp::EQ || !lifted_atom) return false;

  auto* app = dynamic_cast<const RelFullApplication*>(lifted_atom.get());
  if (!app || app->params.size() != 1) return false;
  auto* param = dynamic_cast<const RelExprApplParam*>(app->params[0].get());
  if (!param || !param->expr) return false;
  auto* param_id = dynamic_cast<const RelIDTerm*>(param->expr.get());
  if (!param_id) return false;

  std::shared_ptr<RelTerm> literal_side;
  if (auto* lhs_id = AsPeeledIdTerm(node->lhs)) {
    if (lhs_id->id != param_id->id || !node->rhs || !node->rhs->variables.empty()) return false;
    literal_side = node->rhs;
  } else if (auto* rhs_id = AsPeeledIdTerm(node->rhs)) {
    if (rhs_id->id != param_id->id || !node->lhs || !node->lhs->variables.empty()) return false;
    literal_side = node->lhs;
  } else {
    return false;
  }

  auto* expr_base = dynamic_cast<const RelExprApplBase*>(app->base.get());
  if (!expr_base || !expr_base->expr) return false;
  auto uni = std::dynamic_pointer_cast<RelUnion>(expr_base->expr);
  if (!uni || uni->exprs.size() != 1) return false;
  auto inner_expr = uni->exprs[0];
  inner_expr = PeelRelParenthesisExpr(inner_expr);
  if (auto eat = std::dynamic_pointer_cast<RelExprAsTerm>(inner_expr)) {
    inner_expr = PeelRelParenthesisExpr(eat->inner);
  }
  auto partial = std::dynamic_pointer_cast<RelPartialApplication>(inner_expr);
  if (!partial) return false;

  Visit(partial);
  auto inner = ExpectSourceable(partial->sql_expression);
  auto inner_select = std::dynamic_pointer_cast<sql::ast::Select>(inner);
  if (!inner_select || inner_select->columns.empty() || !inner_select->from.has_value()) return false;

  Visit(literal_side);
  auto literal_sql = BuildSqlTermFromLinearRelTerm(literal_side, {});
  if (!literal_sql) return false;

  const auto& from_ref = *inner_select->from.value();
  if (from_ref.sources.empty()) return false;
  auto ra_source = from_ref.sources.front();

  std::string value_col = GetColumnNameForSourceable(inner, inner_select->columns.size());
  if (auto* id_base = dynamic_cast<RelIDApplBase*>(partial->base.get())) {
    const int rel_arity = context_.GetArity(id_base->id);
    if (rel_arity == 2 && partial->params.size() == 1) {
      value_col = GetColumnNameForSourceable(ra_source->sourceable, 2);
    }
  }
  auto value_column = std::make_shared<sql::ast::Column>(value_col, ra_source);
  auto filter = std::make_shared<sql::ast::ComparisonCondition>(value_column, sql::ast::CompOp::EQ, literal_sql);

  std::shared_ptr<sql::ast::Condition> merged_where = from_ref.where.value_or(nullptr);
  if (!merged_where) {
    merged_where = filter;
  } else {
    merged_where = std::make_shared<sql::ast::LogicalCondition>(
        std::vector<std::shared_ptr<sql::ast::Condition>>{merged_where, filter}, sql::ast::LogicalOp::AND);
  }
  auto new_from = std::make_shared<sql::ast::From>(from_ref.sources, merged_where);

  auto wrapped = std::make_shared<sql::ast::Source>(std::make_shared<sql::ast::Select>(inner_select->columns, new_from),
                                                    GenerateTableAlias());
  std::vector<std::shared_ptr<sql::ast::Selectable>> select_cols;
  for (const auto& var : partial->free_variables) {
    auto col_name = ResolveOutputColumnNameForVariableOnSource(wrapped, var);
    auto col = std::make_shared<sql::ast::Column>(col_name, wrapped);
    select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(col, var));
  }

  node->sql_expression = std::make_shared<sql::ast::Select>(select_cols, std::make_shared<sql::ast::From>(wrapped));
  return true;
}

std::optional<Translator::DateYearPartialAppBinding> Translator::ParseDateYearPartialAppExtract(
    const std::shared_ptr<RelBuiltinDateExpr>& extract) {
  if (!extract || extract->op != RelBuiltinDateOp::ExtractYear || extract->args.size() != 1) return std::nullopt;

  auto date_arg = PeelRelParenthesisExpr(extract->args[0]);
  std::shared_ptr<RelApplBase> appl_base;
  std::vector<std::shared_ptr<RelApplParam>> appl_params;
  if (auto partial = std::dynamic_pointer_cast<RelPartialApplication>(date_arg)) {
    appl_base = partial->base;
    appl_params = partial->params;
  } else if (auto full = std::dynamic_pointer_cast<RelFullApplication>(date_arg)) {
    appl_base = full->base;
    appl_params = full->params;
  } else {
    return std::nullopt;
  }
  if (appl_params.size() != 1) return std::nullopt;
  auto* id_base = dynamic_cast<RelIDApplBase*>(appl_base.get());
  if (!id_base || !context_.IsRelation(id_base->id)) return std::nullopt;

  auto param_expr = appl_params[0] ? PeelRelParenthesisExpr(appl_params[0]->GetExpr()) : nullptr;
  std::string key_var;
  if (auto* id_param = dynamic_cast<RelIDTerm*>(param_expr.get())) {
    key_var = id_param->id;
  } else if (auto* key_term = dynamic_cast<RelTerm*>(param_expr.get())) {
    if (key_term->variables.size() != 1) return std::nullopt;
    key_var = *key_term->variables.begin();
  } else {
    return std::nullopt;
  }
  return DateYearPartialAppBinding{"", key_var, appl_base};
}

std::shared_ptr<sql::ast::Select> Translator::BuildDateYearPartialAppSelect(RelNode& ctx,
                                                                            const DateYearPartialAppBinding& binding) {
  auto table_sql = GetBaseSourceableFromApplBase(ctx, binding.appl_base);
  auto ra_source = std::make_shared<sql::ast::Source>(table_sql, GenerateTableAlias());
  const size_t arity = GetArityForSourceable(table_sql);
  if (arity < 2) {
    throw TranslationException("date_year partial app requires relation arity >= 2", ErrorCode::UNKNOWN_BINARY_OPERATOR,
                               SourceLocation(0, 0));
  }
  const std::string key_col = GetColumnNameForSourceable(table_sql, 1);
  const std::string date_col = GetColumnNameForSourceable(table_sql, 2);
  auto date_term = std::make_shared<sql::ast::Column>(date_col, ra_source);
  auto year_extract =
      std::make_shared<sql::ast::DateExtractTerm>(sql::ast::DateExtractTerm::Part::Year, std::move(date_term));

  std::vector<std::shared_ptr<sql::ast::Selectable>> select_cols;
  select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(
      std::make_shared<sql::ast::Column>(key_col, ra_source), binding.key_var));
  select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(year_extract, binding.year_var));
  return std::make_shared<sql::ast::Select>(
      select_cols, std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{ra_source}));
}

bool Translator::TryEmitDateYearPartialAppComparison(const std::shared_ptr<RelComparison>& node) {
  if (node->op != RelCompOp::EQ || !node->lhs || !node->rhs) return false;

  RelIDTerm* year_var = nullptr;
  std::shared_ptr<RelBuiltinDateExpr> extract;
  auto lhs_u = PeelRelParenthesisExpr(node->lhs);
  auto rhs_u = PeelRelParenthesisExpr(node->rhs);
  if (auto* id = dynamic_cast<RelIDTerm*>(lhs_u.get())) {
    year_var = id;
    extract = std::dynamic_pointer_cast<RelBuiltinDateExpr>(rhs_u);
  } else if (auto* id = dynamic_cast<RelIDTerm*>(rhs_u.get())) {
    year_var = id;
    extract = std::dynamic_pointer_cast<RelBuiltinDateExpr>(lhs_u);
  }
  if (!year_var || !extract) return false;

  auto binding = ParseDateYearPartialAppExtract(extract);
  if (!binding || !node->free_variables.count(binding->key_var)) return false;
  binding->year_var = year_var->id;
  node->sql_expression = BuildDateYearPartialAppSelect(*node, *binding);
  return true;
}

bool Translator::TryEmitDateYearExistential(const std::shared_ptr<RelExistential>& node) {
  if (!node->formula) return false;
  auto conj = std::dynamic_pointer_cast<RelConjunction>(node->formula);
  if (!conj) return false;
  if (!TryEmitDateYearLiftPairConjunction(conj)) return false;

  auto inner_srcable = ExpectSourceable(conj->sql_expression);
  auto subquery = std::make_shared<sql::ast::Source>(inner_srcable, GenerateTableAlias());
  auto inner_select = std::dynamic_pointer_cast<sql::ast::Select>(inner_srcable);
  if (!inner_select) return false;

  bool exported = false;
  for (const auto& col : inner_select->columns) {
    auto ts = std::dynamic_pointer_cast<sql::ast::TermSelectable>(col);
    if (!ts || !ts->alias.has_value()) continue;
    node->free_variables.insert(*ts->alias);
    exported = true;
  }
  if (!exported) return false;

  node->sql_expression = subquery;
  return true;
}

bool Translator::TryEmitDateYearLiftPairConjunction(const std::shared_ptr<RelConjunction>& node) {
  if (!node->lhs || !node->rhs) return false;
  auto lift = std::dynamic_pointer_cast<RelFullApplication>(node->lhs);
  auto cmp = std::dynamic_pointer_cast<RelComparison>(node->rhs);
  if (!lift || !cmp || cmp->op != RelCompOp::EQ || !cmp->lhs || !cmp->rhs) return false;

  auto* lhs_id = AsPeeledIdTerm(cmp->lhs);
  auto* rhs_id = AsPeeledIdTerm(cmp->rhs);
  if (!lhs_id || !rhs_id) return false;
  std::string year_var;
  std::string z_var;
  if (lhs_id->id.starts_with("_") && !rhs_id->id.starts_with("_")) {
    z_var = lhs_id->id;
    year_var = rhs_id->id;
  } else if (rhs_id->id.starts_with("_") && !lhs_id->id.starts_with("_")) {
    z_var = rhs_id->id;
    year_var = lhs_id->id;
  } else {
    return false;
  }

  auto unwrap_union = [](const std::shared_ptr<RelExpr>& e) -> std::shared_ptr<RelExpr> {
    auto cur = e;
    if (auto u = std::dynamic_pointer_cast<RelUnion>(cur)) {
      if (u->exprs.size() == 1) cur = u->exprs[0];
    }
    return PeelRelParenthesisExpr(cur);
  };
  auto* expr_base = dynamic_cast<RelExprApplBase*>(lift->base.get());
  if (!expr_base || !expr_base->expr) return false;
  auto extract = std::dynamic_pointer_cast<RelBuiltinDateExpr>(unwrap_union(expr_base->expr));
  if (!extract) return false;
  auto* z_param = dynamic_cast<RelIDTerm*>(lift->params[0] ? lift->params[0]->GetExpr().get() : nullptr);
  if (!z_param || z_param->id != z_var) return false;

  auto binding = ParseDateYearPartialAppExtract(extract);
  if (!binding) return false;
  binding->year_var = year_var;
  node->sql_expression = BuildDateYearPartialAppSelect(*node, *binding);
  return true;
}

std::shared_ptr<sql::ast::Expression> Translator::TryEmitDateYearLiftedConjunction(
    const std::vector<std::shared_ptr<RelNode>>& subformulas) {
  auto unwrap_union = [](const std::shared_ptr<RelExpr>& e) -> std::shared_ptr<RelExpr> {
    auto cur = e;
    if (auto u = std::dynamic_pointer_cast<RelUnion>(cur)) {
      if (u->exprs.size() == 1) cur = u->exprs[0];
    }
    return PeelRelParenthesisExpr(cur);
  };

  std::optional<size_t> lift_idx;
  std::optional<size_t> cmp_idx;
  std::string year_var;
  std::string z_var;
  std::optional<DateYearPartialAppBinding> binding;

  for (size_t i = 0; i < subformulas.size(); ++i) {
    const auto& f = subformulas[i];
    if (!f) continue;
    if (auto ex = std::dynamic_pointer_cast<RelExistential>(f)) {
      if (!ex->formula) continue;
      auto conj = std::dynamic_pointer_cast<RelConjunction>(ex->formula);
      if (!conj || !conj->lhs || !conj->rhs) continue;
      auto lift = std::dynamic_pointer_cast<RelFullApplication>(conj->lhs);
      auto cmp = std::dynamic_pointer_cast<RelComparison>(conj->rhs);
      if (!lift || !cmp || cmp->op != RelCompOp::EQ || !cmp->lhs || !cmp->rhs) continue;
      auto* lhs_id = AsPeeledIdTerm(cmp->lhs);
      auto* rhs_id = AsPeeledIdTerm(cmp->rhs);
      if (!lhs_id || !rhs_id) continue;
      std::string yv;
      std::string zv;
      if (lhs_id->id.starts_with("_") && !rhs_id->id.starts_with("_")) {
        zv = lhs_id->id;
        yv = rhs_id->id;
      } else if (rhs_id->id.starts_with("_") && !lhs_id->id.starts_with("_")) {
        zv = rhs_id->id;
        yv = lhs_id->id;
      } else {
        continue;
      }
      auto* expr_base = dynamic_cast<RelExprApplBase*>(lift->base.get());
      if (!expr_base || !expr_base->expr) continue;
      auto inner = unwrap_union(expr_base->expr);
      auto extract = std::dynamic_pointer_cast<RelBuiltinDateExpr>(inner);
      if (!extract) continue;
      auto* z_param = dynamic_cast<RelIDTerm*>(lift->params[0] ? lift->params[0]->GetExpr().get() : nullptr);
      if (!z_param || z_param->id != zv) continue;
      auto parsed = ParseDateYearPartialAppExtract(extract);
      if (!parsed) continue;
      parsed->year_var = yv;
      binding = *parsed;
      year_var = yv;
      z_var = zv;
      lift_idx = i;
      cmp_idx = i;
      break;
    }
    if (auto cmp = std::dynamic_pointer_cast<RelComparison>(f)) {
      if (cmp->op != RelCompOp::EQ || !cmp->lhs || !cmp->rhs) continue;
      auto* lhs_id = AsPeeledIdTerm(cmp->lhs);
      auto* rhs_id = AsPeeledIdTerm(cmp->rhs);
      if (!lhs_id || !rhs_id) continue;
      if (lhs_id->id.starts_with("_") && !rhs_id->id.starts_with("_")) {
        z_var = lhs_id->id;
        year_var = rhs_id->id;
        cmp_idx = i;
      } else if (rhs_id->id.starts_with("_") && !lhs_id->id.starts_with("_")) {
        z_var = rhs_id->id;
        year_var = lhs_id->id;
        cmp_idx = i;
      } else if (!lift_idx) {
        auto lhs_u = PeelRelParenthesisExpr(cmp->lhs);
        auto rhs_u = PeelRelParenthesisExpr(cmp->rhs);
        RelIDTerm* yv = nullptr;
        std::shared_ptr<RelBuiltinDateExpr> extract;
        if (auto* id = dynamic_cast<RelIDTerm*>(lhs_u.get())) {
          yv = id;
          extract = std::dynamic_pointer_cast<RelBuiltinDateExpr>(rhs_u);
        } else if (auto* id = dynamic_cast<RelIDTerm*>(rhs_u.get())) {
          yv = id;
          extract = std::dynamic_pointer_cast<RelBuiltinDateExpr>(lhs_u);
        }
        if (yv && extract) {
          if (auto parsed = ParseDateYearPartialAppExtract(extract)) {
            parsed->year_var = yv->id;
            binding = *parsed;
            year_var = yv->id;
            lift_idx = i;
            cmp_idx = i;
            z_var.clear();
          }
        }
      }
      continue;
    }
    auto app = std::dynamic_pointer_cast<RelFullApplication>(f);
    if (!app || app->params.size() != 1) continue;
    auto* expr_base = dynamic_cast<RelExprApplBase*>(app->base.get());
    if (!expr_base || !expr_base->expr) continue;
    auto inner = unwrap_union(expr_base->expr);
    auto extract = std::dynamic_pointer_cast<RelBuiltinDateExpr>(inner);
    if (!extract) continue;
    auto* z_param = dynamic_cast<RelIDTerm*>(app->params[0] ? app->params[0]->GetExpr().get() : nullptr);
    if (!z_param) continue;
    auto parsed = ParseDateYearPartialAppExtract(extract);
    if (!parsed) continue;
    if (!z_var.empty() && z_param->id != z_var) continue;
    z_var = z_param->id;
    binding = *parsed;
    lift_idx = i;
  }

  const bool combined = lift_idx && cmp_idx && *lift_idx == *cmp_idx;
  if (!lift_idx || !cmp_idx || !binding || year_var.empty()) return nullptr;
  if (!combined && z_var.empty()) return nullptr;
  binding->year_var = year_var;

  const bool existential_lift =
      combined && std::dynamic_pointer_cast<RelExistential>(subformulas[*lift_idx]) != nullptr;
  RelNode* date_ctx_node = subformulas[*lift_idx].get();
  if (existential_lift) {
    auto ex = std::dynamic_pointer_cast<RelExistential>(subformulas[*lift_idx]);
    if (ex && ex->formula) date_ctx_node = ex->formula.get();
  }
  auto date_select = BuildDateYearPartialAppSelect(*date_ctx_node, *binding);
  auto date_src = std::make_shared<sql::ast::Source>(date_select, GenerateTableAlias());
  if (existential_lift) {
    auto ex = std::dynamic_pointer_cast<RelExistential>(subformulas[*lift_idx]);
    if (ex) {
      ex->free_variables.insert(binding->key_var);
      ex->free_variables.insert(year_var);
    }
  } else if (combined) {
    subformulas[*lift_idx]->free_variables.insert(binding->key_var);
    subformulas[*lift_idx]->free_variables.insert(year_var);
  }
  subformulas[*lift_idx]->sql_expression = date_src;

  std::vector<std::shared_ptr<sql::ast::Source>> subqueries;
  std::vector<RelNode*> input_ctxs;
  subqueries.push_back(date_src);
  input_ctxs.push_back(subformulas[*lift_idx].get());

  for (size_t i = 0; i < subformulas.size(); ++i) {
    if (i == *lift_idx) continue;
    if (cmp_idx && *cmp_idx != *lift_idx && i == *cmp_idx) continue;
    const auto& f = subformulas[i];
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
  for (size_t i = 0; i < input_ctxs.size() && i < subqueries.size(); ++i) {
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

std::shared_ptr<RelFormula> Translator::Visit(const std::shared_ptr<RelComparison>& node) {
  if (!node->lhs || !node->rhs) return node;

  if (auto* eat = dynamic_cast<RelExprAsTerm*>(node->lhs.get())) {
    if (auto agg = ExtractAggregateFromExpr(eat->inner)) {
      if (TryEmitFilteredAggregateComparison(node, agg)) return node;
    }
  }

  if (TryEmitDateYearPartialAppComparison(node)) return node;

  auto try_scalar_agg_mul = [&](const std::shared_ptr<RelTerm>& mul_term) {
    auto peeled = PeelRelParenthesisTerm(mul_term);
    auto* op = dynamic_cast<RelOpTerm*>(peeled.get());
    if (!op || op->op != RelTermOp::MUL || !op->lhs || !op->rhs) return false;
    std::shared_ptr<sql::ast::Select> out;
    if (!TryEmitScalarAggregateMul(op->lhs, op->rhs, *node, out) &&
        !TryEmitScalarAggregateMul(op->rhs, op->lhs, *node, out)) {
      return false;
    }
    node->sql_expression = out;
    return true;
  };
  auto try_scalar_agg_div = [&](const std::shared_ptr<RelTerm>& div_term) {
    auto peeled = PeelRelParenthesisTerm(div_term);
    auto* op = dynamic_cast<RelOpTerm*>(peeled.get());
    if (!op || op->op != RelTermOp::DIV || !op->lhs || !op->rhs) return false;
    std::shared_ptr<sql::ast::Select> out;
    if (!TryEmitScalarAggregateDiv(op->lhs, op->rhs, *node, out)) return false;
    node->sql_expression = out;
    return true;
  };
  if (node->op == RelCompOp::EQ && (try_scalar_agg_mul(node->rhs) || try_scalar_agg_mul(node->lhs) ||
                                    try_scalar_agg_div(node->rhs) || try_scalar_agg_div(node->lhs))) {
    return node;
  }

  // Safety check: FV(t1 ⋄ t2) ⊆ bound(F). Bounds come from parent.
  for (const auto& var : node->free_variables) {
    if (!node->safety.bound_variables.count(var)) {
      throw TranslationException("Comparison: variable '" + var + "' is not bound (safety check failed)",
                                 ErrorCode::UNBALANCED_VARIABLE, SourceLocation(0, 0));
    }
  }

  BoundSet cover = node->safety.SmallCover();
  std::vector<std::shared_ptr<sql::ast::Source>> cte_sources;
  std::vector<std::pair<std::shared_ptr<sql::ast::Source>, std::set<std::string>>> cte_source_var_pairs;
  std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>> free_var_sources;

  for (const auto& bound : cover.bounds) {
    if (!bound.domain) continue;
    auto domain_sql = DomainToSql(*bound.domain);
    std::set<std::string> bound_vars(bound.variables.begin(), bound.variables.end());
    std::vector<std::string> def_cols(bound.variables.begin(), bound.variables.end());
    auto cte_source = std::make_shared<sql::ast::Source>(domain_sql, GenerateTableAlias("E"), true, def_cols);
    cte_source->bound_hash = bound.Hash();
    cte_sources.push_back(cte_source);
    cte_source_var_pairs.push_back({cte_source, bound_vars});
    for (const auto& var : bound_vars) {
      if (node->free_variables.count(var)) free_var_sources[var] = cte_source;
    }
  }

  auto eq = BuildEqualityForSources(cte_source_var_pairs);

  std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>> term_sources = free_var_sources;
  auto gen_alias = [this]() { return GenerateTableAlias(); };
  CollectIdbTermSources(node->lhs, context_, gen_alias, term_sources);
  CollectIdbTermSources(node->rhs, context_, gen_alias, term_sources);

  Visit(node->lhs);
  Visit(node->rhs);
  auto lhs_sql = BuildSqlTermFromLinearRelTerm(node->lhs, term_sources);
  auto rhs_sql = BuildSqlTermFromLinearRelTerm(node->rhs, term_sources);
  if (!lhs_sql || !rhs_sql) {
    throw TranslationException("Comparison: could not translate terms to SQL", ErrorCode::UNKNOWN_BINARY_OPERATOR,
                               SourceLocation(0, 0));
  }

  sql::ast::CompOp sql_op;
  switch (node->op) {
    case RelCompOp::EQ:
      sql_op = sql::ast::CompOp::EQ;
      break;
    case RelCompOp::NEQ:
      sql_op = sql::ast::CompOp::NEQ;
      break;
    case RelCompOp::LT:
      sql_op = sql::ast::CompOp::LT;
      break;
    case RelCompOp::GT:
      sql_op = sql::ast::CompOp::GT;
      break;
    case RelCompOp::LTE:
      sql_op = sql::ast::CompOp::LTE;
      break;
    case RelCompOp::GTE:
      sql_op = sql::ast::CompOp::GTE;
      break;
  }
  auto comp_cond = std::make_shared<sql::ast::ComparisonCondition>(lhs_sql, sql_op, rhs_sql);

  std::vector<std::string> ordered_vars(node->free_variables.begin(), node->free_variables.end());
  std::sort(ordered_vars.begin(), ordered_vars.end());
  std::vector<std::shared_ptr<sql::ast::Selectable>> select_cols;
  for (const auto& var : ordered_vars) {
    auto it = free_var_sources.find(var);
    if (it != free_var_sources.end()) {
      auto col = std::make_shared<sql::ast::Column>(var, it->second);
      select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(col));
    }
  }

  std::vector<std::shared_ptr<sql::ast::Condition>> where_conditions;
  if (eq) where_conditions.push_back(eq);
  where_conditions.push_back(comp_cond);
  auto where = std::make_shared<sql::ast::LogicalCondition>(where_conditions, sql::ast::LogicalOp::AND);

  std::vector<std::shared_ptr<sql::ast::Source>> from_sources(cte_sources.begin(), cte_sources.end());
  for (const auto& [idb_id, src] : term_sources) {
    if (context_.IsIDB(idb_id)) {
      from_sources.push_back(src);
    }
  }
  auto from = std::make_shared<sql::ast::From>(from_sources, where);
  auto select = std::make_shared<sql::ast::Select>(select_cols, from, cte_sources, false);
  node->sql_expression = select;
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

  if (TryEmitScalarAggregateDivTerm(node)) return node;
  if (TryEmitScalarAggregateMulTerm(node)) return node;

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

std::shared_ptr<RelTerm> Translator::Visit(const std::shared_ptr<RelStringTerm>& node) {
  node->sql_expression = std::make_shared<sql::ast::Constant>(sql::ast::constant_t(node->value));
  return node;
}

std::shared_ptr<RelTerm> Translator::Visit(const std::shared_ptr<RelExprAsTerm>&) {
  throw std::logic_error("Translator: RelExprAsTerm leaked past TermRewriter");
}

std::vector<std::shared_ptr<sql::ast::Selectable>> Translator::VarListShorthandRel(
    const std::vector<RelNode*>& nodes, const std::shared_ptr<sql::ast::Source>& source) {
  std::unordered_set<std::string> seen_vars;
  std::vector<std::shared_ptr<sql::ast::Selectable>> columns;
  for (RelNode* node : nodes) {
    if (!node) continue;
    for (const auto& var : node->free_variables) {
      if (seen_vars.count(var)) continue;
      auto col_name = ResolveOutputColumnNameForVariableOnSource(source, var);
      auto column = std::make_shared<sql::ast::Column>(col_name, source);
      columns.push_back(std::make_shared<sql::ast::TermSelectable>(column, var));
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
      auto col_name = ResolveOutputColumnNameForVariableOnSource(source, var);
      auto column = std::make_shared<sql::ast::Column>(col_name, source);
      columns.push_back(std::make_shared<sql::ast::TermSelectable>(column, var));
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
    for (const auto& var : node->free_variables) {
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
        auto lhs_col = ResolveOutputColumnNameForVariableOnSource(src_i, var);
        auto rhs_col = ResolveOutputColumnNameForVariableOnSource(src_j, var);
        auto lhs = std::make_shared<sql::ast::Column>(lhs_col, src_i);
        auto rhs = std::make_shared<sql::ast::Column>(rhs_col, src_j);
        conditions.push_back(std::make_shared<sql::ast::ComparisonCondition>(lhs, sql::ast::CompOp::EQ, rhs));
      }
    }
  }
  if (conditions.empty()) return nullptr;
  if (conditions.size() == 1) return conditions[0];
  return std::make_shared<sql::ast::LogicalCondition>(conditions, sql::ast::LogicalOp::AND);
}

std::shared_ptr<sql::ast::Condition> Translator::BuildEqualityForSources(
    const std::vector<std::pair<std::shared_ptr<sql::ast::Source>, std::set<std::string>>>& source_var_pairs) {
  std::unordered_map<std::string, std::vector<std::shared_ptr<sql::ast::Source>>> var_to_sources;
  for (const auto& [source, vars] : source_var_pairs) {
    for (const auto& var : vars) {
      var_to_sources[var].push_back(source);
    }
  }
  std::vector<std::shared_ptr<sql::ast::Condition>> conditions;
  for (const auto& [var, sources] : var_to_sources) {
    if (sources.size() < 2) continue;
    for (size_t i = 0; i < sources.size(); i++) {
      for (size_t j = i + 1; j < sources.size(); j++) {
        auto lhs_col = ResolveOutputColumnNameForVariableOnSource(sources[i], var);
        auto rhs_col = ResolveOutputColumnNameForVariableOnSource(sources[j], var);
        auto lhs = std::make_shared<sql::ast::Column>(lhs_col, sources[i]);
        auto rhs = std::make_shared<sql::ast::Column>(rhs_col, sources[j]);
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

  if (TryEmitDateYearExistential(node)) return node;

  if (TryEmitScalarDecimalSumExistential(node)) return node;
  if (TryEmitScalarAggregateDivExistential(node)) return node;

  // exists(z | {sum[...]}(z) and z > k) — emit one filtered aggregate; skip CTE/cross-join materialization.
  if (auto conj = std::dynamic_pointer_cast<RelConjunction>(node->formula)) {
    std::shared_ptr<RelComparison> cmp;
    const RelFullApplication* app = nullptr;
    if (auto c = std::dynamic_pointer_cast<RelComparison>(conj->rhs)) {
      cmp = c;
      app = dynamic_cast<const RelFullApplication*>(conj->lhs.get());
    } else if (auto c = std::dynamic_pointer_cast<RelComparison>(conj->lhs)) {
      cmp = c;
      app = dynamic_cast<const RelFullApplication*>(conj->rhs.get());
    }
    if (cmp && app) {
      auto lifted = std::dynamic_pointer_cast<RelFormula>(conj->lhs);
      if (!lifted) lifted = std::dynamic_pointer_cast<RelFormula>(conj->rhs);
      if (lifted && TryEmitLiftedPartialAppLiteralEquality(cmp, lifted)) {
        auto inner_srcable = ExpectSourceable(cmp->sql_expression);
        auto subquery = std::make_shared<sql::ast::Source>(inner_srcable, GenerateTableAlias());
        std::vector<std::shared_ptr<sql::ast::Selectable>> select_columns;
        for (const auto& var : node->free_variables) {
          auto col_name = ResolveOutputColumnNameForVariableOnSource(subquery, var);
          auto col = std::make_shared<sql::ast::Column>(col_name, subquery);
          select_columns.push_back(std::make_shared<sql::ast::TermSelectable>(col, var));
        }
        node->sql_expression =
            std::make_shared<sql::ast::Select>(select_columns, std::make_shared<sql::ast::From>(subquery));
        return node;
      }
      auto agg = ExtractAggregateFromLiftedAtom(app);
      if (agg && TryEmitFilteredAggregateComparison(cmp, agg)) {
        auto inner_srcable = ExpectSourceable(cmp->sql_expression);
        auto subquery = std::make_shared<sql::ast::Source>(inner_srcable, GenerateTableAlias());
        std::vector<std::shared_ptr<sql::ast::Selectable>> select_columns;
        for (const auto& var : node->free_variables) {
          auto col_name = ResolveOutputColumnNameForVariableOnSource(subquery, var);
          auto col = std::make_shared<sql::ast::Column>(col_name, subquery);
          select_columns.push_back(std::make_shared<sql::ast::TermSelectable>(col, var));
        }
        node->sql_expression =
            std::make_shared<sql::ast::Select>(select_columns, std::make_shared<sql::ast::From>(subquery));
        return node;
      }
    }
  }

  // Translate inner formula to a Sourceable subquery.
  Visit(node->formula);

  auto inner_expr = node->formula->sql_expression;
  auto inner_srcable = ExpectSourceable(inner_expr);

  auto subquery = std::make_shared<sql::ast::Source>(inner_srcable, GenerateTableAlias());

  // SELECT free variables from the subquery.
  std::vector<std::shared_ptr<sql::ast::Selectable>> select_columns;
  for (const auto& var : node->free_variables) {
    auto col_name = ResolveOutputColumnNameForVariableOnSource(subquery, var);
    auto col = std::make_shared<sql::ast::Column>(col_name, subquery);
    select_columns.push_back(std::make_shared<sql::ast::TermSelectable>(col, var));
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
    auto var_col_name = ResolveOutputColumnNameForVariableOnSource(subquery, vb->id);
    auto var_col = std::make_shared<sql::ast::Column>(var_col_name, subquery);

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

  auto select = std::make_shared<sql::ast::Select>(select_columns, from);
  node->sql_expression = select;
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

  // Translate inner formula to a Sourceable subquery (paper Section 5.2: two aliases T1, T2 over F◦).
  Visit(node->formula);
  auto inner_expr = node->formula->sql_expression;
  auto inner_srcable = ExpectSourceable(inner_expr);

  auto subquery_outer = std::make_shared<sql::ast::Source>(inner_srcable, GenerateTableAlias());
  auto subquery_inner = std::make_shared<sql::ast::Source>(inner_srcable, GenerateTableAlias());
  // Keep the inner F◦ copy as its own subquery so the flattener cannot merge it with the outer
  // copy (same underlying Select); merging produced a single alias and bogus T1.A1 = T1.A1.
  subquery_inner->inhibit_subquery_flatten = true;

  std::vector<std::shared_ptr<sql::ast::Selectable>> select_columns;
  for (const auto& var : node->free_variables) {
    auto col = std::make_shared<sql::ast::Column>(var, subquery_outer);
    select_columns.push_back(std::make_shared<sql::ast::TermSelectable>(col));
  }

  auto make_select_one = [] {
    return std::make_shared<sql::ast::TermSelectable>(std::make_shared<sql::ast::Constant>(1));
  };

  std::vector<std::shared_ptr<sql::ast::Condition>> inner_eq_conditions;
  for (const auto& var : node->free_variables) {
    auto outer_c = std::make_shared<sql::ast::Column>(var, subquery_outer);
    auto inner_c = std::make_shared<sql::ast::Column>(var, subquery_inner);
    inner_eq_conditions.push_back(
        std::make_shared<sql::ast::ComparisonCondition>(outer_c, sql::ast::CompOp::EQ, inner_c));
  }

  for (size_t i = 0; i < bound_var_names.size(); i++) {
    auto domain_source = bound_domain_sources[i];
    auto table = std::dynamic_pointer_cast<sql::ast::Table>(domain_source->sourceable);
    if (!table) {
      throw TranslationException("SQLVisitorRel: domain of universal quantification must be a base table",
                                 ErrorCode::UNKNOWN_BINARY_OPERATOR, SourceLocation(0, 0));
    }
    auto domain_col_name = table->GetAttributeName(0);
    auto domain_col = std::make_shared<sql::ast::Column>(domain_col_name, domain_source);
    auto inner_bound_col = std::make_shared<sql::ast::Column>(bound_var_names[i], subquery_inner);
    inner_eq_conditions.push_back(
        std::make_shared<sql::ast::ComparisonCondition>(domain_col, sql::ast::CompOp::EQ, inner_bound_col));
  }

  std::shared_ptr<sql::ast::Condition> inner_where;
  if (inner_eq_conditions.empty()) {
    inner_where = nullptr;
  } else if (inner_eq_conditions.size() == 1) {
    inner_where = inner_eq_conditions[0];
  } else {
    inner_where = std::make_shared<sql::ast::LogicalCondition>(inner_eq_conditions, sql::ast::LogicalOp::AND);
  }

  auto inner_from =
      std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{subquery_inner}, inner_where);
  auto inner_select = std::make_shared<sql::ast::Select>(
      std::vector<std::shared_ptr<sql::ast::Selectable>>{make_select_one()}, inner_from);

  auto inner_not_exists = std::make_shared<sql::ast::LogicalCondition>(
      std::vector<std::shared_ptr<sql::ast::Condition>>{std::make_shared<sql::ast::Exists>(inner_select)},
      sql::ast::LogicalOp::NOT);

  auto middle_from = std::make_shared<sql::ast::From>(bound_domain_sources, inner_not_exists);
  auto middle_select = std::make_shared<sql::ast::Select>(
      std::vector<std::shared_ptr<sql::ast::Selectable>>{make_select_one()}, middle_from);

  auto outer_not_exists = std::make_shared<sql::ast::LogicalCondition>(
      std::vector<std::shared_ptr<sql::ast::Condition>>{std::make_shared<sql::ast::Exists>(middle_select)},
      sql::ast::LogicalOp::NOT);

  auto outer_from = std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{subquery_outer},
                                                     outer_not_exists);
  auto select = std::make_shared<sql::ast::Select>(select_columns, outer_from);
  node->sql_expression = select;
  return node;
}

std::shared_ptr<sql::ast::Expression> Translator::VisitGeneralizedConjunctionRel(
    const std::vector<std::shared_ptr<RelNode>>& subformulas) {
  if (auto lifted = TryEmitDateYearLiftedConjunction(subformulas)) return lifted;
  if (auto agg_thresh = TryEmitAggregateEqualityWithIdbThresholdConjunction(subformulas)) return agg_thresh;

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
  auto select = std::make_shared<sql::ast::Select>(select_cols, from);
  return select;
}

std::shared_ptr<sql::ast::Term> Translator::BuildSqlTermFromLinearRelTerm(
    const std::shared_ptr<RelTerm>& rel_term,
    const std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>>& term_sources) {
  if (!rel_term) return nullptr;

  if (auto* num = dynamic_cast<RelNumTerm*>(rel_term.get())) {
    return std::make_shared<sql::ast::Constant>(num->value);
  }

  if (auto* str = dynamic_cast<RelStringTerm*>(rel_term.get())) {
    return std::make_shared<sql::ast::Constant>(sql::ast::constant_t(str->value));
  }

  if (auto* id = dynamic_cast<RelIDTerm*>(rel_term.get())) {
    auto it = term_sources.find(id->id);
    if (it == term_sources.end()) return nullptr;
    if (context_.IsIDB(id->id)) {
      return std::make_shared<sql::ast::Column>("A1", it->second);
    }
    return std::make_shared<sql::ast::Column>(ResolveOutputColumnNameForVariableOnSource(it->second, id->id),
                                              it->second);
  }

  if (auto* paren = dynamic_cast<RelParenthesisTerm*>(rel_term.get())) {
    return BuildSqlTermFromLinearRelTerm(paren->term, term_sources);
  }

  if (auto* op_term = dynamic_cast<RelOpTerm*>(rel_term.get())) {
    if (!op_term->lhs || !op_term->rhs) return nullptr;
    auto lhs_sql = BuildSqlTermFromLinearRelTerm(op_term->lhs, term_sources);
    auto rhs_sql = BuildSqlTermFromLinearRelTerm(op_term->rhs, term_sources);
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
    // Wrap operands in parentheses when needed for precedence: * and / bind tighter than + and -.
    // e.g. 3 * (2*x - 1 + 5*x) must not become 3 * 2 * x - 1 + 5 * x
    const bool parent_is_mul_div = (op_str[0] == '*' || op_str[0] == '/');
    if (parent_is_mul_div) {
      if (auto* lhs_op = dynamic_cast<sql::ast::Operation*>(lhs_sql.get());
          lhs_op && (lhs_op->op == "+" || lhs_op->op == "-")) {
        lhs_sql = std::make_shared<sql::ast::ParenthesisTerm>(lhs_sql);
      }
      if (auto* rhs_op = dynamic_cast<sql::ast::Operation*>(rhs_sql.get());
          rhs_op && (rhs_op->op == "+" || rhs_op->op == "-")) {
        rhs_sql = std::make_shared<sql::ast::ParenthesisTerm>(rhs_sql);
      }
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
      ApplyDistinctToDefinitionSelects(member);
    }
  }
}

std::string Translator::GenerateTableAlias(const std::string& prefix) {
  if (table_alias_prefix_counter_.find(prefix) == table_alias_prefix_counter_.end()) {
    table_alias_prefix_counter_[prefix] = 0;
  }
  return fmt::format("{}{}", prefix, table_alias_prefix_counter_[prefix]++);
}

std::shared_ptr<sql::ast::Column> Translator::MakeColumnForBindingOnExprSource(
    const std::shared_ptr<sql::ast::Sourceable>& expr_sql, const std::string& var) const {
  const std::string bare = BindingBareName(var);

  if (auto select = std::dynamic_pointer_cast<sql::ast::Select>(expr_sql)) {
    for (const auto& col : select->columns) {
      const auto* ts = dynamic_cast<const sql::ast::TermSelectable*>(col.get());
      if (!ts) continue;
      if (!ts->alias.has_value() || (*ts->alias != bare && *ts->alias != var)) continue;
      if (const auto* c = dynamic_cast<const sql::ast::Column*>(ts->term.get())) {
        if (c->source.has_value()) {
          return std::make_shared<sql::ast::Column>(c->name, c->source.value());
        }
        return std::make_shared<sql::ast::Column>(c->name);
      }
    }
  }

  std::function<std::shared_ptr<sql::ast::Column>(const std::shared_ptr<sql::ast::Source>&)> find_in_source;
  find_in_source = [&](const std::shared_ptr<sql::ast::Source>& src) -> std::shared_ptr<sql::ast::Column> {
    if (!src) return nullptr;

    const std::string col_name = ResolveOutputColumnNameForVariableOnSource(src, var);
    if (SourceExposesColumn(src, col_name)) {
      return std::make_shared<sql::ast::Column>(col_name, src);
    }

    if (auto table = std::dynamic_pointer_cast<sql::ast::Table>(src->sourceable)) {
      if ((table->name == "ps_supplycost" || table->name == "ps_availqty") && table->arity >= 2) {
        if (bare == "part") {
          return std::make_shared<sql::ast::Column>(table->GetAttributeName(0), src);
        }
        if (bare == "supplier") {
          return std::make_shared<sql::ast::Column>(table->GetAttributeName(1), src);
        }
      }
      if (table->name == bare) {
        return std::make_shared<sql::ast::Column>(table->GetAttributeName(0), src);
      }
    }

    if (auto inner_sel = std::dynamic_pointer_cast<sql::ast::Select>(src->sourceable)) {
      for (const auto& col : inner_sel->columns) {
        const auto* ts = dynamic_cast<const sql::ast::TermSelectable*>(col.get());
        if (!ts || !ts->alias.has_value() || (*ts->alias != bare && *ts->alias != var)) continue;
        if (const auto* c = dynamic_cast<const sql::ast::Column*>(ts->term.get())) {
          return std::make_shared<sql::ast::Column>(c->name, src);
        }
      }
      if (inner_sel->from.has_value()) {
        for (const auto& inner_src : inner_sel->from.value()->sources) {
          if (auto found = find_in_source(inner_src)) return found;
        }
      }
    }
    return nullptr;
  };

  if (auto select = std::dynamic_pointer_cast<sql::ast::Select>(expr_sql)) {
    if (select->from.has_value()) {
      for (const auto& src : select->from.value()->sources) {
        if (auto found = find_in_source(src)) return found;
      }
    }
  }

  return std::make_shared<sql::ast::Column>(bare);
}

std::string Translator::ResolveOutputColumnNameForVariableOnSource(const std::shared_ptr<sql::ast::Source>& source,
                                                                   const std::string& var) const {
  if (!source) return var;

  std::string bare = var;
  if (auto dot = bare.rfind('.'); dot != std::string::npos) {
    bare = bare.substr(dot + 1);
  }

  auto select = std::dynamic_pointer_cast<sql::ast::Select>(source->sourceable);
  if (!select) return bare;

  for (const auto& col : select->columns) {
    const auto* ts = dynamic_cast<const sql::ast::TermSelectable*>(col.get());
    if (!ts) continue;
    if (ts->alias.has_value() && (*ts->alias == bare || *ts->alias == var)) return *ts->alias;
  }
  for (const auto& col : select->columns) {
    const auto* ts = dynamic_cast<const sql::ast::TermSelectable*>(col.get());
    if (!ts) continue;
    const auto* c = dynamic_cast<const sql::ast::Column*>(ts->term.get());
    if (!c || (c->name != bare && c->name != var)) continue;
    if (ts->alias.has_value()) return *ts->alias;
    if (c->name.find('.') == std::string::npos) return c->name;
    return bare;
  }
  return bare;
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
      const auto& col = select->columns[idx - 1];
      if (auto* ts = dynamic_cast<const sql::ast::TermSelectable*>(col.get())) {
        if (ts->alias.has_value()) return *ts->alias;
        if (auto* c = dynamic_cast<const sql::ast::Column*>(ts->term.get())) return c->name;
      }
      return col->Alias();
    }
    return fmt::format("A{}", idx);
  }

  if (auto uni = std::dynamic_pointer_cast<sql::ast::Union>(src)) {
    if (!uni->members.empty()) return GetColumnNameForSourceable(uni->members.front(), idx);
    return fmt::format("A{}", idx);
  }

  if (auto uni_all = std::dynamic_pointer_cast<sql::ast::UnionAll>(src)) {
    if (!uni_all->members.empty()) {
      return GetColumnNameForSourceable(uni_all->members.front(), idx);
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
    return uni_all->members.empty() ? 0 : GetArityForSourceable(uni_all->members.front());
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

void Translator::MaterializeRelationExprIfNeeded(RelNode& ctx_node, const std::shared_ptr<RelExpr>& expr) {
  if (!expr) return;
  auto* id = dynamic_cast<RelIDTerm*>(expr.get());
  if (id && context_.IsRelation(id->id)) {
    expr->sql_expression = GetExpressionFromID(ctx_node, id->id, true);
  }
}

std::shared_ptr<sql::ast::Term> Translator::RelExprToSqlTerm(RelNode& node, const std::shared_ptr<RelExpr>& expr) {
  std::vector<std::shared_ptr<sql::ast::Source>> discard;
  return RelExprToSqlTerm(node, expr, discard);
}

Translator::ScalarSqlTerm Translator::ExtractScalarSqlTerm(RelNode& node, const std::shared_ptr<RelExpr>& expr) {
  MaterializeRelationExprIfNeeded(node, expr);
  if (!expr->sql_expression) {
    Visit(expr);
  }
  auto srcable = ExpectSourceable(expr->sql_expression);
  if (auto sel = std::dynamic_pointer_cast<sql::ast::Select>(srcable)) {
    if (!sel->columns.empty()) {
      // Partial apps like o_orderdate[ok] project bound vars then attribute columns; use the value column.
      const size_t col_idx = sel->columns.size() > 1 ? sel->columns.size() - 1 : 0;
      auto ts = std::dynamic_pointer_cast<sql::ast::TermSelectable>(sel->columns[col_idx]);
      if (!ts || !ts->term) {
        throw TranslationException("ExtractScalarSqlTerm: expected TermSelectable", ErrorCode::UNKNOWN_BINARY_OPERATOR,
                                   SourceLocation(0, 0));
      }
      ScalarSqlTerm out{ts->term, {}, {}};
      if (sel->from.has_value()) {
        for (auto& s : sel->from.value()->sources) {
          out.from_sources.push_back(s);
        }
        if (sel->from.value()->where.has_value()) {
          out.where = sel->from.value()->where.value();
        }
      }
      return out;
    }
  }
  ScalarSqlTerm out;
  out.term = RelExprToSqlTerm(node, expr, out.from_sources);
  return out;
}

std::shared_ptr<sql::ast::Term> Translator::RelExprToSqlTerm(RelNode& node, const std::shared_ptr<RelExpr>& expr,
                                                             std::vector<std::shared_ptr<sql::ast::Source>>& from_out) {
  MaterializeRelationExprIfNeeded(node, expr);
  if (!expr->sql_expression) {
    Visit(expr);
  }
  auto srcable = ExpectSourceable(expr->sql_expression);
  auto sel = std::dynamic_pointer_cast<sql::ast::Select>(srcable);
  if (!sel || sel->columns.empty()) {
    throw TranslationException("RelExprToSqlTerm: expected a Select expression", ErrorCode::UNKNOWN_BINARY_OPERATOR,
                               SourceLocation(0, 0));
  }
  auto ts = std::dynamic_pointer_cast<sql::ast::TermSelectable>(sel->columns[0]);
  if (!ts || !ts->term) {
    throw TranslationException("RelExprToSqlTerm: expected TermSelectable column", ErrorCode::UNKNOWN_BINARY_OPERATOR,
                               SourceLocation(0, 0));
  }
  if (sel->from.has_value()) {
    for (auto& s : sel->from.value()->sources) {
      from_out.push_back(s);
    }
  }
  (void)node;
  return ts->term;
}

std::shared_ptr<RelExpr> Translator::Visit(const std::shared_ptr<RelBuiltinAggregateExpr>& node) {
  if (!node->body) return node;
  sql::ast::AggregateFunction fn = sql::ast::AggregateFunction::SUM;
  bool count_all = false;
  switch (node->op) {
    case RelBuiltinAggregateOp::SUM:
      fn = sql::ast::AggregateFunction::SUM;
      break;
    case RelBuiltinAggregateOp::COUNT:
      fn = sql::ast::AggregateFunction::COUNT;
      count_all = true;
      break;
    case RelBuiltinAggregateOp::AVG:
      fn = sql::ast::AggregateFunction::AVG;
      break;
    case RelBuiltinAggregateOp::MIN:
      fn = sql::ast::AggregateFunction::MIN;
      break;
    case RelBuiltinAggregateOp::MAX:
      fn = sql::ast::AggregateFunction::MAX;
      break;
  }
  node->sql_expression = VisitAggregateRel(node->body, fn, count_all);
  return node;
}

std::shared_ptr<RelExpr> Translator::Visit(const std::shared_ptr<RelTypedLiteralExpr>& node) {
  std::string interval;
  switch (node->kind) {
    case RelTypedLiteralKind::Day:
      interval = fmt::format("INTERVAL '{}' DAY", node->arg0);
      break;
    case RelTypedLiteralKind::Month:
      interval = fmt::format("INTERVAL '{}' MONTH", node->arg0);
      break;
    case RelTypedLiteralKind::Year:
      interval = fmt::format("INTERVAL '{}' YEAR", node->arg0);
      break;
    case RelTypedLiteralKind::FixedDecimalType:
      interval = fmt::format("CAST(NULL AS DECIMAL({0},{1}))", node->arg0, node->arg1);
      break;
  }
  auto vt = std::make_shared<sql::ast::VerbatimTerm>(interval);
  auto sel = std::make_shared<sql::ast::Select>(
      std::vector<std::shared_ptr<sql::ast::Selectable>>{std::make_shared<sql::ast::TermSelectable>(vt, "A1")}, false);
  node->sql_expression = sel;
  return node;
}

std::shared_ptr<RelExpr> Translator::Visit(const std::shared_ptr<RelBuiltinDateExpr>& node) {
  if (node->op == RelBuiltinDateOp::ParseDate) {
    if (node->args.size() != 2) return node;
    auto ds = dynamic_cast<RelLiteral*>(node->args[0].get());
    auto fs = dynamic_cast<RelLiteral*>(node->args[1].get());
    if (!ds || !fs || !std::holds_alternative<std::string>(ds->value) ||
        !std::holds_alternative<std::string>(fs->value)) {
      throw TranslationException("parse_date: expected string literals", ErrorCode::UNKNOWN_BINARY_OPERATOR,
                                 SourceLocation(0, 0));
    }
    const std::string& d = std::get<std::string>(ds->value);
    (void)std::get<std::string>(fs->value);
    auto vt = std::make_shared<sql::ast::VerbatimTerm>(fmt::format("DATE '{0}'", d));
    node->sql_expression = std::make_shared<sql::ast::Select>(
        std::vector<std::shared_ptr<sql::ast::Selectable>>{std::make_shared<sql::ast::TermSelectable>(vt, "A1")},
        false);
    return node;
  }
  if (node->op == RelBuiltinDateOp::DateAdd || node->op == RelBuiltinDateOp::DateSubtract) {
    if (node->args.size() != 2) return node;
    std::vector<std::shared_ptr<sql::ast::Source>> from_sources;
    auto lhs = RelExprToSqlTerm(*node, node->args[0], from_sources);
    auto rhs = RelExprToSqlTerm(*node, node->args[1], from_sources);
    const char* op = node->op == RelBuiltinDateOp::DateAdd ? "+" : "-";
    auto comb = std::make_shared<sql::ast::Operation>(lhs, rhs, op);
    auto cols =
        std::vector<std::shared_ptr<sql::ast::Selectable>>{std::make_shared<sql::ast::TermSelectable>(comb, "A1")};
    node->sql_expression = from_sources.empty() ? std::make_shared<sql::ast::Select>(cols, false)
                                                : std::make_shared<sql::ast::Select>(
                                                      cols, std::make_shared<sql::ast::From>(from_sources), false);
    return node;
  }
  if (node->op == RelBuiltinDateOp::ExtractYear) {
    if (node->args.size() != 1) return node;
    auto scalar = ExtractScalarSqlTerm(*node, node->args[0]);
    auto extract = std::make_shared<sql::ast::DateExtractTerm>(sql::ast::DateExtractTerm::Part::Year, scalar.term);
    auto cols =
        std::vector<std::shared_ptr<sql::ast::Selectable>>{std::make_shared<sql::ast::TermSelectable>(extract, "A1")};
    if (scalar.from_sources.empty()) {
      node->sql_expression = std::make_shared<sql::ast::Select>(cols, false);
    } else {
      node->sql_expression = std::make_shared<sql::ast::Select>(
          cols, std::make_shared<sql::ast::From>(scalar.from_sources, scalar.where), false);
    }
    return node;
  }
  return node;
}

std::shared_ptr<RelExpr> Translator::Visit(const std::shared_ptr<RelBuiltinDecimalCastExpr>& node) {
  if (!node->value) {
    auto vt = std::make_shared<sql::ast::VerbatimTerm>(
        fmt::format("CAST(NULL AS DECIMAL({0},{1}))", node->precision, node->scale));
    node->sql_expression = std::make_shared<sql::ast::Select>(
        std::vector<std::shared_ptr<sql::ast::Selectable>>{std::make_shared<sql::ast::TermSelectable>(vt, "A1")},
        false);
    return node;
  }
  std::vector<std::shared_ptr<sql::ast::Source>> from_sources;
  auto inner = RelExprToSqlTerm(*node, node->value, from_sources);
  auto vt = std::make_shared<sql::ast::VerbatimTerm>(
      fmt::format("CAST(({0}) AS DECIMAL({1},{2}))", inner->ToString(), node->precision, node->scale));
  auto cols = std::vector<std::shared_ptr<sql::ast::Selectable>>{std::make_shared<sql::ast::TermSelectable>(vt, "A1")};
  node->sql_expression = from_sources.empty() ? std::make_shared<sql::ast::Select>(cols, false)
                                              : std::make_shared<sql::ast::Select>(
                                                    cols, std::make_shared<sql::ast::From>(from_sources), false);
  return node;
}

std::shared_ptr<RelExpr> Translator::Visit(const std::shared_ptr<RelBuiltinCoalesceExpr>& node) {
  if (!node->primary || !node->fallback) return node;
  std::vector<std::shared_ptr<sql::ast::Source>> from_sources;
  auto a = RelExprToSqlTerm(*node, node->primary, from_sources);
  auto b = RelExprToSqlTerm(*node, node->fallback, from_sources);
  auto vt =
      std::make_shared<sql::ast::VerbatimTerm>(fmt::format("COALESCE(({0}), ({1}))", a->ToString(), b->ToString()));
  auto cols = std::vector<std::shared_ptr<sql::ast::Selectable>>{std::make_shared<sql::ast::TermSelectable>(vt, "A1")};
  node->sql_expression = from_sources.empty() ? std::make_shared<sql::ast::Select>(cols, false)
                                              : std::make_shared<sql::ast::Select>(
                                                    cols, std::make_shared<sql::ast::From>(from_sources), false);
  return node;
}

std::shared_ptr<RelExpr> Translator::Visit(const std::shared_ptr<RelBuiltinSubstringExpr>& node) {
  if (!node->str || !node->start || !node->len) return node;
  std::vector<std::shared_ptr<sql::ast::Source>> from_sources;
  auto s = RelExprToSqlTerm(*node, node->str, from_sources);
  auto st = RelExprToSqlTerm(*node, node->start, from_sources);
  auto ln = RelExprToSqlTerm(*node, node->len, from_sources);
  auto vt = std::make_shared<sql::ast::VerbatimTerm>(
      fmt::format("SUBSTRING(({0}) FROM ({1}) FOR ({2}))", s->ToString(), st->ToString(), ln->ToString()));
  auto cols = std::vector<std::shared_ptr<sql::ast::Selectable>>{std::make_shared<sql::ast::TermSelectable>(vt, "A1")};
  node->sql_expression = from_sources.empty() ? std::make_shared<sql::ast::Select>(cols, false)
                                              : std::make_shared<sql::ast::Select>(
                                                    cols, std::make_shared<sql::ast::From>(from_sources), false);
  return node;
}

std::shared_ptr<RelFormula> Translator::Visit(const std::shared_ptr<RelBuiltinLikeMatchFormula>& node) {
  if (!node->value) return node;

  // Variable value: emit `SELECT free vars FROM <safety CTEs> WHERE col LIKE 'pat'` (same shape as RelComparison).
  if (auto* id = dynamic_cast<RelIDTerm*>(node->value.get())) {
    if (context_.IsVar(id->id)) {
      BoundSet cover = node->safety.SmallCover();
      std::vector<std::shared_ptr<sql::ast::Source>> cte_sources;
      std::vector<std::pair<std::shared_ptr<sql::ast::Source>, std::set<std::string>>> cte_source_var_pairs;
      std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>> free_var_sources;
      for (const auto& bound : cover.bounds) {
        if (!bound.domain) continue;
        auto domain_sql = DomainToSql(*bound.domain);
        std::set<std::string> bound_vars(bound.variables.begin(), bound.variables.end());
        std::vector<std::string> def_cols(bound.variables.begin(), bound.variables.end());
        auto cte_source = std::make_shared<sql::ast::Source>(domain_sql, GenerateTableAlias("E"), true, def_cols);
        cte_source->bound_hash = bound.Hash();
        cte_sources.push_back(cte_source);
        cte_source_var_pairs.push_back({cte_source, bound_vars});
        for (const auto& var : bound_vars) {
          if (node->free_variables.count(var)) free_var_sources[var] = cte_source;
        }
      }
      auto eq = BuildEqualityForSources(cte_source_var_pairs);

      auto var_src = free_var_sources.find(id->id);
      if (var_src == free_var_sources.end()) {
        throw TranslationException("like_match: variable '" + id->id + "' is not bound", ErrorCode::UNBALANCED_VARIABLE,
                                   SourceLocation(0, 0));
      }
      auto col = std::make_shared<sql::ast::Column>(id->id, var_src->second);
      auto pat = std::make_shared<sql::ast::Constant>(node->like_pattern);
      auto like_cond = std::make_shared<sql::ast::ComparisonCondition>(col, sql::ast::CompOp::LIKE, pat);

      std::vector<std::string> ordered_vars(node->free_variables.begin(), node->free_variables.end());
      std::sort(ordered_vars.begin(), ordered_vars.end());
      std::vector<std::shared_ptr<sql::ast::Selectable>> select_cols;
      for (const auto& var : ordered_vars) {
        auto it = free_var_sources.find(var);
        if (it != free_var_sources.end()) {
          select_cols.push_back(
              std::make_shared<sql::ast::TermSelectable>(std::make_shared<sql::ast::Column>(var, it->second)));
        }
      }
      std::vector<std::shared_ptr<sql::ast::Condition>> where_conditions;
      if (eq) where_conditions.push_back(eq);
      where_conditions.push_back(like_cond);
      auto where = std::make_shared<sql::ast::LogicalCondition>(where_conditions, sql::ast::LogicalOp::AND);
      std::vector<std::shared_ptr<sql::ast::Source>> from_sources(cte_sources.begin(), cte_sources.end());
      auto from = std::make_shared<sql::ast::From>(from_sources, where);
      node->sql_expression = std::make_shared<sql::ast::Select>(select_cols, from, cte_sources, false);
      return node;
    }
  }

  // Subquery / relation value: filter rows of the value relation with `col LIKE 'pat'`.
  MaterializeRelationExprIfNeeded(*node, node->value);
  if (!node->value->sql_expression) {
    Visit(node->value);
  }
  auto vsql = ExpectSourceable(node->value->sql_expression);
  auto vs = std::make_shared<sql::ast::Source>(vsql, GenerateTableAlias());
  // like_match(pat, R[key]): key is the first column; match the attribute value (last column when arity > 1).
  size_t like_col_index = 1;
  size_t rel_arity = GetArityForSourceable(vsql);
  if (rel_arity > 1) {
    like_col_index = rel_arity;
  }
  std::string col_name = GetColumnNameForSourceable(vsql, like_col_index);
  auto lhs = std::make_shared<sql::ast::Column>(col_name, vs);
  auto rhs = std::make_shared<sql::ast::Constant>(node->like_pattern);
  auto where = std::make_shared<sql::ast::ComparisonCondition>(lhs, sql::ast::CompOp::LIKE, rhs);

  std::vector<std::shared_ptr<sql::ast::Selectable>> select_cols;
  if (node->free_variables.empty()) {
    select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(std::make_shared<sql::ast::Constant>(1), "A1"));
  } else {
    std::vector<std::string> ordered_vars(node->free_variables.begin(), node->free_variables.end());
    std::sort(ordered_vars.begin(), ordered_vars.end());
    for (const auto& var : ordered_vars) {
      select_cols.push_back(
          std::make_shared<sql::ast::TermSelectable>(std::make_shared<sql::ast::Column>(var, vs), var));
    }
  }
  node->sql_expression = std::make_shared<sql::ast::Select>(
      select_cols, std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{vs}, where));
  return node;
}

std::shared_ptr<RelFormula> Translator::Visit(const std::shared_ptr<RelBuiltinOrderExpr>& node) {
  if (!node->body) return node;
  MaterializeRelationExprIfNeeded(*node, node->body);
  if (!node->body->sql_expression) {
    Visit(node->body);
  }
  auto body_sql = ExpectSourceable(node->body->sql_expression);
  auto src = std::make_shared<sql::ast::Source>(body_sql, GenerateTableAlias());
  node->body->sql_expression = src;

  size_t arity = GetArityForSourceable(body_sql);
  // TPC-H final_sort: reverse_sort[inside_rev_sort] exposes row index + wildcard slot beyond body columns.
  std::string ranked_idb_id;
  if (auto* id = dynamic_cast<RelIDTerm*>(node->body.get())) {
    ranked_idb_id = id->id;
  } else if (auto* pa = dynamic_cast<RelPartialApplication*>(node->body.get())) {
    if (pa->params.empty()) {
      if (auto* id_base = dynamic_cast<RelIDApplBase*>(pa->base.get())) {
        ranked_idb_id = id_base->id;
      }
    }
  }
  const bool ranked_final_sort = node->kind == RelBuiltinOrderKind::SortDesc && !ranked_idb_id.empty() &&
                                 context_.IsIDB(ranked_idb_id) &&
                                 static_cast<size_t>(context_.GetArity(ranked_idb_id)) + 2 > arity;

  std::vector<sql::ast::OrderByClause> order_by;
  if (node->kind == RelBuiltinOrderKind::BottomDesc && node->bottom_sort_column.has_value()) {
    auto col = std::make_shared<sql::ast::Column>(*node->bottom_sort_column, src);
    order_by.push_back({col, sql::ast::SortDirection::DESC});
  } else {
    sql::ast::SortDirection dir = sql::ast::SortDirection::ASC;
    if (node->kind == RelBuiltinOrderKind::SortDesc || node->kind == RelBuiltinOrderKind::BottomDesc) {
      dir = sql::ast::SortDirection::DESC;
    }
    for (size_t i = 1; i <= arity; ++i) {
      std::string cn = GetColumnNameForSourceable(body_sql, i);
      auto col = std::make_shared<sql::ast::Column>(cn, src);
      order_by.push_back({col, dir});
    }
  }

  std::vector<std::shared_ptr<sql::ast::Selectable>> select_cols;
  if (ranked_final_sort) {
    sql::ast::SortDirection over_dir = sql::ast::SortDirection::DESC;
    if (!order_by.empty()) {
      over_dir = order_by[0].direction;
    }
    const std::string over_clause = BuildOrderBySqlOrdinals(arity, over_dir);
    auto row_num = std::make_shared<sql::ast::VerbatimTerm>("ROW_NUMBER() OVER (" + over_clause + ")");
    select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(row_num, "A1"));
    for (size_t i = 1; i <= arity; ++i) {
      if (i == 2) {
        select_cols.push_back(
            std::make_shared<sql::ast::TermSelectable>(std::make_shared<sql::ast::Constant>(1), "A3"));
      }
      std::string cn = GetColumnNameForSourceable(body_sql, i);
      auto col = std::make_shared<sql::ast::Column>(cn, src);
      const std::string out_alias = fmt::format("A{}", RankedFinalSortOutputPosition(i));
      select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(col, out_alias));
    }
    node->arity = select_cols.size();
  } else {
    for (size_t i = 1; i <= arity; ++i) {
      std::string cn = GetColumnNameForSourceable(body_sql, i);
      auto col = std::make_shared<sql::ast::Column>(cn, src);
      select_cols.push_back(std::make_shared<sql::ast::TermSelectable>(col, cn));
    }
  }

  auto from = std::make_shared<sql::ast::From>(std::vector<std::shared_ptr<sql::ast::Source>>{src});
  auto sel = std::make_shared<sql::ast::Select>(select_cols, from);
  sel->order_by = std::move(order_by);
  if (node->limit.has_value()) {
    sel->limit_value = static_cast<int>(*node->limit);
  }
  node->sql_expression = sel;
  return node;
}

}  // namespace rel2sql

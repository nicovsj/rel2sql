#include "select_column_rebinder.h"

#include <unordered_map>
#include <unordered_set>

#include "sql_ast/expr_visitor.h"

namespace rel2sql {
namespace sql::ast {

namespace {

bool SourceExportsAlias(const std::shared_ptr<Source>& source, const std::string& alias) {
  if (!source || !source->sourceable) return false;
  auto select = std::dynamic_pointer_cast<Select>(source->sourceable);
  if (!select) return false;
  for (const auto& col : select->columns) {
    const auto* ts = dynamic_cast<const TermSelectable*>(col.get());
    if (!ts || !ts->alias.has_value()) continue;
    if (*ts->alias == alias) return true;
  }
  return false;
}

std::string ColumnNameForAliasOnSource(const std::shared_ptr<Source>& source, const std::string& alias) {
  if (!source || !source->sourceable) return alias;
  auto select = std::dynamic_pointer_cast<Select>(source->sourceable);
  if (!select) return alias;
  for (const auto& col : select->columns) {
    const auto* ts = dynamic_cast<const TermSelectable*>(col.get());
    if (!ts || !ts->alias.has_value() || *ts->alias != alias) continue;
    if (const auto* c = dynamic_cast<const Column*>(ts->term.get())) return c->name;
    return alias;
  }
  return alias;
}

std::unordered_set<std::string> CollectFromAliases(const std::vector<std::shared_ptr<Source>>& from_sources) {
  std::unordered_set<std::string> from_aliases;
  for (const auto& src : from_sources) {
    if (src && src->alias.has_value()) from_aliases.insert(src->alias.value()->name);
  }
  return from_aliases;
}

bool IsDanglingColumn(const Column& col, const std::unordered_set<std::string>& from_aliases) {
  return col.source.has_value() && !from_aliases.count(col.source.value()->Alias());
}

std::shared_ptr<Column> AsColumn(const std::shared_ptr<Term>& term) { return std::dynamic_pointer_cast<Column>(term); }

void RebindInSourceable(Sourceable& sourceable);

void CollectEqualityPairs(const std::shared_ptr<Condition>& condition,
                          std::vector<std::pair<std::shared_ptr<Column>, std::shared_ptr<Column>>>& out) {
  if (!condition) return;
  if (auto logical = std::dynamic_pointer_cast<LogicalCondition>(condition)) {
    for (const auto& child : logical->conditions) {
      CollectEqualityPairs(child, out);
    }
    return;
  }
  auto cmp = std::dynamic_pointer_cast<ComparisonCondition>(condition);
  if (!cmp || cmp->op != CompOp::EQ) return;
  auto lhs = AsColumn(cmp->lhs);
  auto rhs = AsColumn(cmp->rhs);
  if (lhs && rhs) out.emplace_back(lhs, rhs);
}

void SubstituteColumn(Column& col, const std::unordered_map<std::string, std::shared_ptr<Column>>& subst) {
  if (!col.source.has_value()) return;
  const std::string key = col.source.value()->Alias() + "." + col.name;
  auto it = subst.find(key);
  if (it == subst.end()) return;
  col.name = it->second->name;
  col.source = it->second->source;
}

class WhereColumnSubstitutor : public ExpressionVisitor {
 public:
  explicit WhereColumnSubstitutor(const std::unordered_map<std::string, std::shared_ptr<Column>>& subst)
      : subst_(subst) {}

  void Visit(Column& column) override { SubstituteColumn(column, subst_); }

  void Visit(ComparisonCondition& comparison_condition) override {
    ExpressionVisitor::Visit(comparison_condition);
    SubstituteTerm(comparison_condition.lhs);
    SubstituteTerm(comparison_condition.rhs);
  }

  void Visit(Operation& operation) override {
    ExpressionVisitor::Visit(operation);
    SubstituteTerm(operation.lhs);
    SubstituteTerm(operation.rhs);
  }

  void Visit(ParenthesisTerm& parenthesis_term) override {
    ExpressionVisitor::Visit(parenthesis_term);
    SubstituteTerm(parenthesis_term.term);
  }

  void Visit(Function& function) override {
    ExpressionVisitor::Visit(function);
    SubstituteTerm(function.arg);
  }

  void Visit(DateExtractTerm& date_extract_term) override {
    ExpressionVisitor::Visit(date_extract_term);
    SubstituteTerm(date_extract_term.arg);
  }

 private:
  void SubstituteTerm(std::shared_ptr<Term>& term) {
    if (auto col = std::dynamic_pointer_cast<Column>(term)) {
      SubstituteColumn(*col, subst_);
    }
  }

  const std::unordered_map<std::string, std::shared_ptr<Column>>& subst_;
};

std::unordered_map<std::string, std::shared_ptr<Column>> BuildDanglingEqualitySubstitutions(
    const std::shared_ptr<Condition>& where, const std::unordered_set<std::string>& from_aliases) {
  std::unordered_map<std::string, std::shared_ptr<Column>> subst;
  if (!where) return subst;

  std::vector<std::pair<std::shared_ptr<Column>, std::shared_ptr<Column>>> pairs;
  CollectEqualityPairs(where, pairs);

  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto& [lhs, rhs] : pairs) {
      const bool lhs_dangling = IsDanglingColumn(*lhs, from_aliases);
      const bool rhs_dangling = IsDanglingColumn(*rhs, from_aliases);
      if (lhs_dangling == rhs_dangling) continue;

      const auto& dangling = lhs_dangling ? lhs : rhs;
      const auto& anchor = lhs_dangling ? rhs : lhs;
      if (!anchor->source.has_value()) continue;

      const std::string key = dangling->source.value()->Alias() + "." + dangling->name;
      if (subst.contains(key)) continue;

      subst[key] = std::make_shared<Column>(anchor->name, anchor->source.value());
      changed = true;
    }

    if (!changed) break;

    WhereColumnSubstitutor applicator(subst);
    where->Accept(applicator);

    pairs.clear();
    CollectEqualityPairs(where, pairs);
  }

  return subst;
}

void RebindDanglingWhereColumns(Select& select, const std::vector<std::shared_ptr<Source>>& from_sources,
                                const std::unordered_set<std::string>& from_aliases) {
  if (!select.from.has_value() || !select.from.value()->where.has_value()) return;

  auto& where = select.from.value()->where.value();
  auto subst = BuildDanglingEqualitySubstitutions(where, from_aliases);
  if (subst.empty()) return;

  WhereColumnSubstitutor substitutor(subst);
  where->Accept(substitutor);
}

void RebindSelect(Select& select) {
  if (!select.from.has_value()) return;
  const auto& from_sources = select.from.value()->sources;
  const std::unordered_set<std::string> from_aliases = CollectFromAliases(from_sources);

  RebindDanglingWhereColumns(select, from_sources, from_aliases);

  for (const auto& col : select.columns) {
    auto* ts = dynamic_cast<TermSelectable*>(col.get());
    if (!ts || !ts->term) continue;
    auto* c = dynamic_cast<Column*>(ts->term.get());
    if (!c || !c->source.has_value()) continue;
    if (from_aliases.count(c->source.value()->Alias())) continue;

    const std::string var = ts->alias.has_value() ? *ts->alias : c->name;
    for (const auto& src : from_sources) {
      if (!src || !SourceExportsAlias(src, var)) continue;
      const std::string col_name = ColumnNameForAliasOnSource(src, var);
      ts->term = std::make_shared<Column>(col_name, src);
      break;
    }
  }

  for (const auto& src : from_sources) {
    if (src && src->sourceable) RebindInSourceable(*src->sourceable);
  }
}

void RebindInSourceable(Sourceable& sourceable) {
  if (auto* select = dynamic_cast<Select*>(&sourceable)) {
    RebindSelect(*select);
    return;
  }
  if (auto* union_expr = dynamic_cast<Union*>(&sourceable)) {
    for (const auto& member : union_expr->members) {
      if (member) RebindInSourceable(*member);
    }
    return;
  }
  if (auto* union_all = dynamic_cast<UnionAll*>(&sourceable)) {
    for (const auto& member : union_all->members) {
      if (member) RebindInSourceable(*member);
    }
  }
}

void RebindInExpression(Expression& expression) {
  if (auto* select = dynamic_cast<Select*>(&expression)) {
    RebindSelect(*select);
    return;
  }
  if (auto* view = dynamic_cast<View*>(&expression)) {
    if (view->source && view->source->sourceable) {
      RebindInSourceable(*view->source->sourceable);
    }
    return;
  }
  if (auto* create_table = dynamic_cast<CreateTable*>(&expression)) {
    if (create_table->source && create_table->source->sourceable) {
      RebindInSourceable(*create_table->source->sourceable);
    }
    return;
  }
  if (auto* multi = dynamic_cast<MultipleStatements*>(&expression)) {
    for (const auto& stmt : multi->statements) {
      if (stmt) RebindInExpression(*stmt);
    }
  }
}

}  // namespace

void RebindDanglingSelectColumns(Expression& root) { RebindInExpression(root); }

}  // namespace sql::ast
}  // namespace rel2sql

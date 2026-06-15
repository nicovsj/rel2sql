#include "select_column_rebinder.h"

#include <unordered_set>

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

void RebindSelect(Select& select) {
  if (!select.from.has_value()) return;
  const auto& from_sources = select.from.value()->sources;
  std::unordered_set<std::string> from_aliases;
  for (const auto& src : from_sources) {
    if (src && src->alias.has_value()) from_aliases.insert(src->alias.value()->name);
  }

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

#include "anti_join_optimizer.h"

#include <functional>
#include <map>
#include <set>
#include <vector>

#include "replacers.h"

namespace rel2sql {
namespace sql::ast {

namespace {

// A SELECT that only joins, filters and projects, so that its FROM and WHERE can be moved into an EXISTS.
bool IsPlainProjection(const Select& select) {
  if (!select.ctes.empty() || select.group_by.has_value() || !select.order_by.empty() ||
      select.limit_value.has_value() || !select.from.has_value() || select.from.value()->sources.empty()) {
    return false;
  }
  for (const auto& column : select.columns) {
    if (!std::dynamic_pointer_cast<TermSelectable>(column)) return false;
  }
  return true;
}

// The names of the columns a source exposes, or nothing if they are not known.
std::optional<std::vector<std::string>> SourceColumnNames(const Source& source) {
  if (!source.def_columns.empty()) return source.def_columns;
  if (auto table = std::dynamic_pointer_cast<Table>(source.sourceable)) {
    std::vector<std::string> names;
    for (int i = 0; i < table->arity; ++i) names.push_back(table->GetAttributeName(i));
    return names;
  }
  if (auto select = std::dynamic_pointer_cast<Select>(source.sourceable)) {
    std::vector<std::string> names;
    for (const auto& column : select->columns) {
      if (!std::dynamic_pointer_cast<TermSelectable>(column)) return std::nullopt;
      names.push_back(column->Alias());
    }
    return names;
  }
  return std::nullopt;
}

using ColumnId = std::pair<std::string, std::string>;  // (source alias, column name)

class ColumnCollector : public ExpressionVisitor {
 public:
  using ExpressionVisitor::Visit;
  void Visit(Column& column) override { columns.push_back(&column); }
  std::vector<Column*> columns;
};

class ColumnRetargeter : public ExpressionVisitor {
 public:
  ColumnRetargeter(std::string old_alias, std::shared_ptr<Source> target)
      : old_alias_(std::move(old_alias)), target_(std::move(target)) {}
  using ExpressionVisitor::Visit;
  void Visit(Column& column) override {
    if (column.source && column.source.value()->Alias() == old_alias_) column.source = target_;
  }

 private:
  std::string old_alias_;
  std::shared_ptr<Source> target_;
};

constexpr std::size_t kMaxSourceGroupSize = 4;

// Removes a group of table sources of the body of an EXISTS that maps onto other sources, of the body or of the
// enclosing FROM, of the same tables: every column of a removed source that is used outside the top-level equalities
// must equal the same column of its image, and every top-level equality must still hold between the images. The
// body, which only tests for existence, then has a row whenever it had one before. Returns true if a group was
// removed. With remove_all, only a group of all the sources of the body is removed, and only if a single comparison
// other than an equality made trivial is left, which then replaces the EXISTS.
bool RemoveSourcesMappingOntoOthers(Select& body, const From& outer, bool remove_all = false) {
  auto& from = *body.from.value();
  if (!from.where.has_value()) return false;
  std::map<std::string, std::shared_ptr<Source>> inner;
  std::map<std::string, std::shared_ptr<Source>> targets;
  for (const auto& source : from.sources) {
    if (std::dynamic_pointer_cast<Table>(source->sourceable) && !source->is_cte) inner[source->Alias()] = source;
  }
  if (inner.empty() || (!remove_all && from.sources.size() < 2)) return false;
  targets = inner;
  for (const auto& source : outer.sources) {
    if (std::dynamic_pointer_cast<Table>(source->sourceable) && !source->is_cte) targets[source->Alias()] = source;
  }

  std::vector<std::shared_ptr<Condition>> conjuncts;
  std::function<void(const std::shared_ptr<Condition>&)> collect = [&](const std::shared_ptr<Condition>& c) {
    if (auto logical = std::dynamic_pointer_cast<LogicalCondition>(c); logical && logical->op == LogicalOp::AND) {
      for (const auto& d : logical->conditions) collect(d);
    } else {
      conjuncts.push_back(c);
    }
  };
  collect(from.where.value());

  std::map<ColumnId, ColumnId> parent;
  std::function<ColumnId(const ColumnId&)> find = [&](const ColumnId& c) -> ColumnId {
    auto it = parent.find(c);
    return it == parent.end() || it->second == c ? c : find(it->second);
  };
  std::vector<std::pair<ColumnId, ColumnId>> equalities;
  std::set<const Column*> equality_columns;
  for (const auto& conjunct : conjuncts) {
    auto cmp = std::dynamic_pointer_cast<ComparisonCondition>(conjunct);
    if (!cmp || cmp->op != CompOp::EQ) continue;
    auto lhs = std::dynamic_pointer_cast<Column>(cmp->lhs);
    auto rhs = std::dynamic_pointer_cast<Column>(cmp->rhs);
    if (!lhs || !rhs || !lhs->source || !rhs->source) continue;
    ColumnId a{lhs->source.value()->Alias(), lhs->name};
    ColumnId b{rhs->source.value()->Alias(), rhs->name};
    equalities.emplace_back(a, b);
    equality_columns.insert(lhs.get());
    equality_columns.insert(rhs.get());
    parent.emplace(a, a);
    parent.emplace(b, b);
    auto ra = find(a);
    auto rb = find(b);
    if (ra != rb) parent[ra] = rb;
  }
  if (equalities.empty()) return false;
  auto equivalent = [&](const ColumnId& a, const ColumnId& b) { return find(a) == find(b); };

  ColumnCollector collector;
  collector.Visit(body);
  std::map<std::string, std::set<std::string>> kept_columns;
  for (const Column* column : collector.columns) {
    if (!column->source || equality_columns.count(column)) continue;
    const auto alias = column->source.value()->Alias();
    if (inner.count(alias)) kept_columns[alias].insert(column->name);
  }

  auto table_name = [&](const std::string& alias) {
    return std::dynamic_pointer_cast<Table>(targets.at(alias)->sourceable)->name;
  };
  using Mapping = std::map<std::string, std::string>;  // removed alias -> alias of its image
  auto image = [](const Mapping& h, const ColumnId& c) {
    auto it = h.find(c.first);
    return it == h.end() ? c : ColumnId{it->second, c.second};
  };
  // The first equality the mapping breaks, so that the search can extend the group across it; nullopt if the
  // mapping is valid, and a pair of empty columns if it breaks something else.
  auto check = [&](const Mapping& h) -> std::optional<std::pair<ColumnId, ColumnId>> {
    for (const auto& [removed, target] : h) {
      if (h.count(target)) return std::pair<ColumnId, ColumnId>{};
      if (auto it = kept_columns.find(removed); it != kept_columns.end()) {
        for (const auto& col : it->second) {
          if (!equivalent({removed, col}, {target, col})) return std::pair<ColumnId, ColumnId>{};
        }
      }
    }
    for (const auto& eq : equalities) {
      if (!h.count(eq.first.first) && !h.count(eq.second.first)) continue;
      if (!equivalent(image(h, eq.first), image(h, eq.second))) return eq;
    }
    return std::nullopt;
  };
  std::function<std::optional<Mapping>(const Mapping&)> search = [&](const Mapping& h) -> std::optional<Mapping> {
    auto violation = check(h);
    if (!violation) return h;
    if (violation->first.first.empty() || h.size() >= kMaxSourceGroupSize) return std::nullopt;
    const auto& [a, b] = *violation;
    const std::string& outside = h.count(a.first) ? b.first : a.first;
    if (h.count(outside) || !inner.count(outside)) return std::nullopt;
    for (const auto& [alias, _] : targets) {
      if (alias == outside || h.count(alias) || table_name(alias) != table_name(outside)) continue;
      Mapping extended = h;
      extended[outside] = alias;
      if (auto found = search(extended)) return found;
    }
    return std::nullopt;
  };

  for (const auto& [removed, _] : inner) {
    for (const auto& [target, __] : targets) {
      if (removed == target || table_name(removed) != table_name(target)) continue;
      auto found = search({{removed, target}});
      if (!found || (found->size() >= from.sources.size()) != remove_all) continue;
      if (remove_all) {
        size_t left = 0;
        for (const auto& conjunct : conjuncts) {
          auto cmp = std::dynamic_pointer_cast<ComparisonCondition>(conjunct);
          if (!cmp) {
            left = conjuncts.size() + 1;  // only a comparison can replace the EXISTS
            break;
          }
          auto lhs = std::dynamic_pointer_cast<Column>(cmp->lhs);
          auto rhs = std::dynamic_pointer_cast<Column>(cmp->rhs);
          const bool trivial = cmp->op == CompOp::EQ && lhs && rhs && lhs->source && rhs->source &&
                               image(*found, {lhs->source.value()->Alias(), lhs->name}) ==
                                   image(*found, {rhs->source.value()->Alias(), rhs->name});
          if (!trivial) ++left;
        }
        if (left != 1) continue;
      }
      for (const auto& [old_alias, new_alias] : *found) {
        ColumnRetargeter retargeter(old_alias, targets.at(new_alias));
        body.Accept(retargeter);
        std::erase_if(from.sources, [&](const std::shared_ptr<Source>& s) { return s->Alias() == old_alias; });
      }
      RedundancyReplacer redundancy_replacer;
      body.Accept(redundancy_replacer);
      return true;
    }
  }
  return false;
}

}  // namespace

std::shared_ptr<Condition> AntiJoinOptimizer::Rewrite(const std::shared_ptr<Condition>& condition) {
  auto inclusion = std::dynamic_pointer_cast<Inclusion>(condition);
  if (!inclusion || !inclusion->is_not || !inclusion->select) return condition;

  std::vector<std::shared_ptr<Term>> terms;  // the columns of the subquery, as terms over `sources`
  std::vector<std::shared_ptr<Source>> sources;
  std::optional<std::shared_ptr<Condition>> where;

  auto select = inclusion->select;
  if (select->columns.size() == 1 && std::dynamic_pointer_cast<Wildcard>(select->columns.front())) {
    // `SELECT * FROM src`: compare with the columns of src, or with the projection src is.
    if (!select->ctes.empty() || !select->from.has_value() || select->from.value()->sources.size() != 1 ||
        select->from.value()->where.has_value()) {
      return condition;
    }
    auto source = select->from.value()->sources.front();
    auto sub = std::dynamic_pointer_cast<Select>(source->sourceable);
    if (sub && !source->is_cte && source->def_columns.empty() && IsPlainProjection(*sub)) {
      select = sub;
    } else {
      auto names = SourceColumnNames(*source);
      if (!names) return condition;
      for (const auto& name : *names) terms.push_back(std::make_shared<Column>(name, source));
      sources = {source};
    }
  }
  if (sources.empty()) {
    if (!IsPlainProjection(*select)) return condition;
    for (const auto& column : select->columns) terms.push_back(std::dynamic_pointer_cast<TermSelectable>(column)->term);
    sources = select->from.value()->sources;
    where = select->from.value()->where;
  }
  if (terms.size() != inclusion->columns.size()) return condition;

  std::vector<std::shared_ptr<Condition>> conditions;
  if (where) conditions.push_back(*where);
  for (size_t i = 0; i < terms.size(); ++i) {
    conditions.push_back(std::make_shared<ComparisonCondition>(terms[i], CompOp::EQ, inclusion->columns[i]));
  }
  auto exists_select = std::make_shared<Select>(
      std::vector<std::shared_ptr<Selectable>>{std::make_shared<TermSelectable>(std::make_shared<Constant>(1))},
      std::make_shared<From>(sources, std::make_shared<LogicalCondition>(conditions, LogicalOp::AND)));
  if (!scopes_.empty()) {
    while (RemoveSourcesMappingOntoOthers(*exists_select, *scopes_.back())) {
    }
    // The body only tests a condition on the rows of the enclosing query: a filter.
    if (RemoveSourcesMappingOntoOthers(*exists_select, *scopes_.back(), /*remove_all=*/true)) {
      auto left = exists_select->from.value()->where.value();
      while (auto logical = std::dynamic_pointer_cast<LogicalCondition>(left)) {
        if (logical->op != LogicalOp::AND || logical->conditions.size() != 1) break;
        left = logical->conditions.front();
      }
      return std::make_shared<LogicalCondition>(std::vector<std::shared_ptr<Condition>>{left}, LogicalOp::NOT);
    }
  }
  return std::make_shared<LogicalCondition>(
      std::vector<std::shared_ptr<Condition>>{std::make_shared<Exists>(exists_select)}, LogicalOp::NOT);
}

void AntiJoinOptimizer::Visit(Source& source) {
  // DuckDB cannot bind some correlated subqueries inside a recursive CTE, so its definition keeps NOT IN.
  if (source.is_recursive_cte) return;
  ExpressionVisitor::Visit(source);
}

void AntiJoinOptimizer::Visit(From& from) {
  scopes_.push_back(&from);
  ExpressionVisitor::Visit(from);
  if (from.where.has_value()) from.where = Rewrite(from.where.value());
  scopes_.pop_back();
}

void AntiJoinOptimizer::Visit(LogicalCondition& logical_condition) {
  ExpressionVisitor::Visit(logical_condition);
  for (auto& condition : logical_condition.conditions) condition = Rewrite(condition);
}

}  // namespace sql::ast
}  // namespace rel2sql

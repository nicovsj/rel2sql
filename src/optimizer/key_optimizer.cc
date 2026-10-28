#include "key_optimizer.h"

#include <map>
#include <set>

namespace rel2sql {
namespace sql::ast {

namespace {

using ColumnId = std::pair<std::string, std::string>;  // (source alias, column name)

void CollectConjuncts(const std::shared_ptr<Condition>& condition, std::vector<std::shared_ptr<Condition>>& out) {
  if (auto logical = std::dynamic_pointer_cast<LogicalCondition>(condition); logical && logical->op == LogicalOp::AND) {
    for (const auto& c : logical->conditions) CollectConjuncts(c, out);
    return;
  }
  out.push_back(condition);
}

// The equivalence classes of columns of a FROM that its top-level equalities make equal, and the columns they make
// constant: those equated with a constant or with a column of an outer query.
class Equalities {
 public:
  Equalities(const From& from, const std::set<std::string>& aliases) {
    if (!from.where.has_value()) return;
    std::vector<std::shared_ptr<Condition>> conjuncts;
    CollectConjuncts(from.where.value(), conjuncts);
    for (const auto& conjunct : conjuncts) {
      auto cmp = std::dynamic_pointer_cast<ComparisonCondition>(conjunct);
      if (!cmp || cmp->op != CompOp::EQ) continue;
      auto lhs = Local(cmp->lhs, aliases);
      auto rhs = Local(cmp->rhs, aliases);
      if (lhs && rhs) {
        Union(*lhs, *rhs);
      } else if (lhs && IsFixed(cmp->rhs, aliases)) {
        fixed_.push_back(*lhs);
      } else if (rhs && IsFixed(cmp->lhs, aliases)) {
        fixed_.push_back(*rhs);
      }
    }
  }

  ColumnId Find(const ColumnId& c) const {
    auto it = parent_.find(c);
    if (it == parent_.end() || it->second == c) return c;
    return Find(it->second);
  }

  bool Equivalent(const ColumnId& a, const ColumnId& b) const { return Find(a) == Find(b); }

  const std::vector<ColumnId>& Fixed() const { return fixed_; }

  // Every column that occurs in an equality.
  std::vector<ColumnId> Members() const {
    std::vector<ColumnId> out;
    for (const auto& [c, _] : parent_) out.push_back(c);
    return out;
  }

  // The column a term is, if it is a column of a source of this FROM.
  static std::optional<ColumnId> Local(const std::shared_ptr<Term>& term, const std::set<std::string>& aliases) {
    auto column = std::dynamic_pointer_cast<Column>(term);
    if (!column || !column->source) return std::nullopt;
    auto alias = column->source.value()->Alias();
    if (!aliases.count(alias)) return std::nullopt;
    return ColumnId{alias, column->name};
  }

 private:
  static bool IsFixed(const std::shared_ptr<Term>& term, const std::set<std::string>& aliases) {
    if (std::dynamic_pointer_cast<Constant>(term) || std::dynamic_pointer_cast<VerbatimTerm>(term)) return true;
    auto column = std::dynamic_pointer_cast<Column>(term);
    return column && column->source && !aliases.count(column->source.value()->Alias());
  }

  void Union(const ColumnId& a, const ColumnId& b) {
    parent_.emplace(a, a);
    parent_.emplace(b, b);
    auto ra = Find(a);
    auto rb = Find(b);
    if (ra != rb) parent_[ra] = rb;
  }

  std::map<ColumnId, ColumnId> parent_;
  std::vector<ColumnId> fixed_;
};

// Whether the given columns, together with the constant ones, determine a tuple of every source: they contain a key
// of a source, possibly through equalities with columns of sources already determined.
bool DeterminesAllSources(const std::vector<ColumnId>& given,
                          const std::vector<std::pair<std::string, KeyOptimizer::Keys>>& sources,
                          const Equalities& eq) {
  std::set<ColumnId> determined;  // class representatives
  for (const auto& c : given) determined.insert(eq.Find(c));
  for (const auto& c : eq.Fixed()) determined.insert(eq.Find(c));
  std::set<std::string> full;
  auto is_determined = [&](const ColumnId& c) { return full.count(c.first) || determined.count(eq.Find(c)); };
  const auto members = eq.Members();
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto& [alias, keys] : sources) {
      if (full.count(alias)) continue;
      for (const auto& key : keys) {
        bool all = true;
        for (const auto& col : key) all = all && is_determined({alias, col});
        if (!all) continue;
        full.insert(alias);
        for (const auto& m : members) {
          if (m.first == alias) determined.insert(eq.Find(m));
        }
        changed = true;
        break;
      }
    }
  }
  return full.size() == sources.size();
}

class AggregateFinder : public ExpressionVisitor {
 public:
  using ExpressionVisitor::Visit;
  void Visit(Function&) override { found = true; }
  // Aggregates of a subquery belong to it.
  void Visit(Select&) override {}
  bool found = false;
};

}  // namespace

void KeyOptimizer::Visit(View& view) {
  ExpressionVisitor::Visit(view);
  auto select = std::dynamic_pointer_cast<Select>(view.source->sourceable);
  if (!select || !view.source->def_columns.empty() || !view.source->alias.has_value()) return;
  if (auto it = select_keys_.find(select.get()); it != select_keys_.end()) {
    keys_[view.source->alias.value()->name] = it->second;
  }
}

std::optional<KeyOptimizer::Keys> KeyOptimizer::SourceKeys(const Source& source) const {
  if (!source.def_columns.empty() || source.is_recursive_cte) return std::nullopt;
  if (auto table = std::dynamic_pointer_cast<Table>(source.sourceable)) {
    if (auto it = keys_.find(table->name); it != keys_.end()) return it->second;
    return std::nullopt;
  }
  if (auto select = std::dynamic_pointer_cast<Select>(source.sourceable)) {
    if (auto it = select_keys_.find(select.get()); it != select_keys_.end()) return it->second;
  }
  return std::nullopt;
}

void KeyOptimizer::Visit(Select& select) {
  for (auto& cte : select.ctes) ExpressionVisitor::Visit(*cte);
  ExpressionVisitor::Visit(select);

  if (!select.from.has_value() || select.from.value()->sources.empty()) {
    if (!select.group_by.has_value()) select_keys_[&select] = {{}};
    return;
  }

  const auto& from = *select.from.value();
  std::set<std::string> aliases;
  std::vector<std::pair<std::string, Keys>> sources;
  bool all_keyed = true;
  for (const auto& source : from.sources) {
    aliases.insert(source->Alias());
    if (auto keys = SourceKeys(*source)) {
      sources.emplace_back(source->Alias(), *keys);
    } else {
      all_keyed = false;
    }
  }
  Equalities eq(from, aliases);

  std::vector<std::string> names;
  std::vector<std::optional<ColumnId>> output_columns;
  bool has_wildcard = false;
  AggregateFinder aggregates;
  for (const auto& column : select.columns) {
    auto term_selectable = std::dynamic_pointer_cast<TermSelectable>(column);
    if (!term_selectable) {
      has_wildcard = true;
      continue;
    }
    names.push_back(term_selectable->Alias());
    output_columns.push_back(Equalities::Local(term_selectable->term, aliases));
    term_selectable->term->Accept(aggregates);
  }
  if (has_wildcard) return;

  std::optional<Keys> keys;
  if (select.group_by.has_value()) {
    // The grouping columns are a key, if the output has them all.
    std::vector<std::string> key;
    for (const auto& group_column : select.group_by.value()->columns) {
      auto term_selectable = std::dynamic_pointer_cast<TermSelectable>(group_column);
      auto grouped = term_selectable ? Equalities::Local(term_selectable->term, aliases) : std::nullopt;
      std::optional<std::string> name;
      for (size_t i = 0; grouped && i < output_columns.size() && !name; ++i) {
        if (output_columns[i] && eq.Equivalent(*output_columns[i], *grouped)) name = names[i];
      }
      if (!name) {
        key.clear();
        break;
      }
      key.push_back(*name);
    }
    if (!key.empty()) keys = Keys{key};
  } else if (aggregates.found) {
    keys = Keys{{}};
  } else if (all_keyed) {
    std::vector<size_t> kept;
    for (size_t i = 0; i < output_columns.size(); ++i) {
      if (output_columns[i]) kept.push_back(i);
    }
    auto determines = [&](const std::vector<size_t>& indices) {
      std::vector<ColumnId> given;
      for (size_t i : indices) given.push_back(*output_columns[i]);
      return DeterminesAllSources(given, sources, eq);
    };
    if (determines(kept)) {
      // The rows of the join are distinct on the output, which needs no DISTINCT. A minimal subset of the output
      // columns that still determines them is a key.
      select.is_distinct = false;
      for (size_t pos = 0; pos < kept.size();) {
        auto without = kept;
        without.erase(without.begin() + pos);
        if (determines(without)) {
          kept = std::move(without);
        } else {
          ++pos;
        }
      }
      std::vector<std::string> key;
      for (size_t i : kept) key.push_back(names[i]);
      keys = Keys{key};
    }
  }
  if (!keys && select.is_distinct) keys = Keys{names};
  if (keys) select_keys_[&select] = *keys;
}

}  // namespace sql::ast
}  // namespace rel2sql

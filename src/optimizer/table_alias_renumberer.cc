#include "table_alias_renumberer.h"

#include <cctype>
#include <unordered_set>
#include <vector>

namespace rel2sql {
namespace sql::ast {

namespace {

bool IsGeneratedAliasChar(char c) { return c == 'T' || c == 'E' || c == 'I' || c == 'R'; }

void RegisterAliasName(const std::string& name, std::vector<std::string>& order,
                       std::unordered_set<std::string>& seen) {
  if (name.size() < 2 || !IsGeneratedAliasChar(name[0])) return;
  for (size_t i = 1; i < name.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(name[i]))) return;
  }
  if (seen.insert(name).second) {
    order.push_back(name);
  }
}

void RegisterSourceAlias(const Source& source, std::vector<std::string>& order, std::unordered_set<std::string>& seen) {
  if (!source.alias.has_value()) return;
  RegisterAliasName(source.alias.value()->name, order, seen);
}

class AliasCollector {
 public:
  explicit AliasCollector(std::vector<std::string>& order, std::unordered_set<std::string>& seen)
      : order_(order), seen_(seen) {}

  void Collect(Sourceable& sourceable);
  void Collect(Select& select);
  void Collect(From& from);
  void Collect(Condition& condition);
  void Collect(Term& term);
  void Collect(Selectable& selectable);

 private:
  void CollectSources(const std::vector<std::shared_ptr<Source>>& sources);

  std::vector<std::string>& order_;
  std::unordered_set<std::string>& seen_;
};

void AliasCollector::CollectSources(const std::vector<std::shared_ptr<Source>>& sources) {
  for (const auto& source : sources) {
    if (!source) continue;
    Collect(*source->sourceable);
    RegisterSourceAlias(*source, order_, seen_);
  }
}

void AliasCollector::Collect(Sourceable& sourceable) {
  if (auto* select = dynamic_cast<Select*>(&sourceable)) {
    Collect(*select);
    return;
  }
  if (auto* union_expr = dynamic_cast<Union*>(&sourceable)) {
    for (const auto& member : union_expr->members) {
      if (member) Collect(*member);
    }
    return;
  }
  if (auto* union_all = dynamic_cast<UnionAll*>(&sourceable)) {
    for (const auto& member : union_all->members) {
      if (member) Collect(*member);
    }
  }
}

void AliasCollector::Collect(Select& select) {
  for (const auto& cte : select.ctes) {
    if (!cte) continue;
    Collect(*cte->sourceable);
    RegisterSourceAlias(*cte, order_, seen_);
  }

  if (select.from) {
    CollectSources(select.from.value()->sources);
    if (select.from.value()->where) {
      Collect(*select.from.value()->where.value());
    }
  }

  for (const auto& col : select.columns) {
    if (col) Collect(*col);
  }
  if (select.group_by) {
    for (const auto& col : select.group_by.value()->columns) {
      if (col) Collect(*col);
    }
  }
  for (const auto& ob : select.order_by) {
    if (ob.term) Collect(*ob.term);
  }
}

void AliasCollector::Collect(From& from) {
  CollectSources(from.sources);
  if (from.where) {
    Collect(*from.where.value());
  }
}

void AliasCollector::Collect(Condition& condition) {
  if (auto* exists = dynamic_cast<Exists*>(&condition)) {
    if (exists->select) Collect(*exists->select);
    return;
  }
  if (auto* inclusion = dynamic_cast<Inclusion*>(&condition)) {
    for (const auto& col : inclusion->columns) {
      if (col) Collect(*col);
    }
    if (inclusion->select) Collect(*inclusion->select);
    return;
  }
  if (auto* logical = dynamic_cast<LogicalCondition*>(&condition)) {
    for (const auto& sub : logical->conditions) {
      if (sub) Collect(*sub);
    }
    return;
  }
  if (auto* comparison = dynamic_cast<ComparisonCondition*>(&condition)) {
    if (comparison->lhs) Collect(*comparison->lhs);
    if (comparison->rhs) Collect(*comparison->rhs);
  }
}

void AliasCollector::Collect(Term& term) {
  if (auto* operation = dynamic_cast<Operation*>(&term)) {
    if (operation->lhs) Collect(*operation->lhs);
    if (operation->rhs) Collect(*operation->rhs);
    return;
  }
  if (auto* paren = dynamic_cast<ParenthesisTerm*>(&term)) {
    if (paren->term) Collect(*paren->term);
    return;
  }
  if (auto* function = dynamic_cast<Function*>(&term)) {
    if (function->arg) Collect(*function->arg);
    return;
  }
  if (auto* case_when = dynamic_cast<CaseWhen*>(&term)) {
    for (auto& [cond, case_term] : case_when->cases) {
      if (cond) Collect(*cond);
      if (case_term) Collect(*case_term);
    }
  }
}

void AliasCollector::Collect(Selectable& selectable) {
  if (auto* term_sel = dynamic_cast<TermSelectable*>(&selectable)) {
    if (term_sel->term) Collect(*term_sel->term);
  }
}

class AliasApplier {
 public:
  explicit AliasApplier(const std::unordered_map<std::string, std::string>& rename_map) : rename_map_(rename_map) {}

  void Apply(Expression& expression);
  void CommitRenames();

  void Apply(Sourceable& sourceable);
  void Apply(Select& select);
  void Apply(From& from);
  void Apply(Condition& condition);
  void Apply(Term& term);
  void Apply(Selectable& selectable);

 private:
  void ApplySource(Source& source);
  void ApplySources(const std::vector<std::shared_ptr<Source>>& sources);
  void QueueRename(const std::shared_ptr<Alias>& alias);

  const std::unordered_map<std::string, std::string>& rename_map_;
  std::vector<std::pair<std::shared_ptr<Alias>, std::string>> pending_renames_;
};

void AliasApplier::QueueRename(const std::shared_ptr<Alias>& alias) {
  if (!alias) return;
  auto it = rename_map_.find(alias->name);
  if (it == rename_map_.end() || it->second == alias->name) return;
  pending_renames_.emplace_back(alias, it->second);
}

void AliasApplier::CommitRenames() {
  for (auto& [alias_ptr, new_name] : pending_renames_) {
    if (alias_ptr) {
      alias_ptr->name = new_name;
    }
  }
  pending_renames_.clear();
}

void AliasApplier::ApplySource(Source& source) {
  Apply(*source.sourceable);
  if (source.alias.has_value()) {
    QueueRename(source.alias.value());
  }
}

void AliasApplier::ApplySources(const std::vector<std::shared_ptr<Source>>& sources) {
  for (const auto& source : sources) {
    if (source) ApplySource(*source);
  }
}

void AliasApplier::Apply(Sourceable& sourceable) {
  if (auto* select = dynamic_cast<Select*>(&sourceable)) {
    Apply(*select);
    return;
  }
  if (auto* union_expr = dynamic_cast<Union*>(&sourceable)) {
    for (const auto& member : union_expr->members) {
      if (member) Apply(*member);
    }
    return;
  }
  if (auto* union_all = dynamic_cast<UnionAll*>(&sourceable)) {
    for (const auto& member : union_all->members) {
      if (member) Apply(*member);
    }
  }
}

void AliasApplier::Apply(Select& select) {
  for (const auto& cte : select.ctes) {
    if (cte) ApplySource(*cte);
  }
  if (select.from) {
    ApplySources(select.from.value()->sources);
    if (select.from.value()->where) {
      Apply(*select.from.value()->where.value());
    }
  }
  for (const auto& col : select.columns) {
    if (col) Apply(*col);
  }
  if (select.group_by) {
    for (const auto& col : select.group_by.value()->columns) {
      if (col) Apply(*col);
    }
  }
  for (const auto& ob : select.order_by) {
    if (ob.term) Apply(*ob.term);
  }
}

void AliasApplier::Apply(From& from) {
  ApplySources(from.sources);
  if (from.where) {
    Apply(*from.where.value());
  }
}

void AliasApplier::Apply(Condition& condition) {
  if (auto* exists = dynamic_cast<Exists*>(&condition)) {
    if (exists->select) Apply(*exists->select);
    return;
  }
  if (auto* inclusion = dynamic_cast<Inclusion*>(&condition)) {
    for (const auto& col : inclusion->columns) {
      if (col) Apply(*col);
    }
    if (inclusion->select) Apply(*inclusion->select);
    return;
  }
  if (auto* logical = dynamic_cast<LogicalCondition*>(&condition)) {
    for (const auto& sub : logical->conditions) {
      if (sub) Apply(*sub);
    }
    return;
  }
  if (auto* comparison = dynamic_cast<ComparisonCondition*>(&condition)) {
    if (comparison->lhs) Apply(*comparison->lhs);
    if (comparison->rhs) Apply(*comparison->rhs);
  }
}

void AliasApplier::Apply(Term& term) {
  if (auto* operation = dynamic_cast<Operation*>(&term)) {
    if (operation->lhs) Apply(*operation->lhs);
    if (operation->rhs) Apply(*operation->rhs);
    return;
  }
  if (auto* paren = dynamic_cast<ParenthesisTerm*>(&term)) {
    if (paren->term) Apply(*paren->term);
    return;
  }
  if (auto* function = dynamic_cast<Function*>(&term)) {
    if (function->arg) Apply(*function->arg);
    return;
  }
  if (auto* case_when = dynamic_cast<CaseWhen*>(&term)) {
    for (auto& [cond, case_term] : case_when->cases) {
      if (cond) Apply(*cond);
      if (case_term) Apply(*case_term);
    }
  }
}

void AliasApplier::Apply(Selectable& selectable) {
  if (auto* term_sel = dynamic_cast<TermSelectable*>(&selectable)) {
    if (term_sel->term) Apply(*term_sel->term);
  }
}

void AliasApplier::Apply(Expression& expression) {
  if (auto* select = dynamic_cast<Select*>(&expression)) {
    Apply(*select);
    return;
  }
  if (auto* union_expr = dynamic_cast<Union*>(&expression)) {
    for (const auto& member : union_expr->members) {
      if (member) Apply(*member);
    }
    return;
  }
  if (auto* union_all = dynamic_cast<UnionAll*>(&expression)) {
    for (const auto& member : union_all->members) {
      if (member) Apply(*member);
    }
    return;
  }
  if (auto* view = dynamic_cast<View*>(&expression)) {
    if (view->source) ApplySource(*view->source);
    return;
  }
  if (auto* create_table = dynamic_cast<CreateTable*>(&expression)) {
    if (create_table->source) ApplySource(*create_table->source);
  }
}

void CollectStatement(Expression& root, std::vector<std::string>& order, std::unordered_set<std::string>& seen) {
  AliasCollector collector(order, seen);
  if (auto* select = dynamic_cast<Select*>(&root)) {
    collector.Collect(*select);
    return;
  }
  if (auto* union_expr = dynamic_cast<Union*>(&root)) {
    for (const auto& member : union_expr->members) {
      if (member) collector.Collect(*member);
    }
    return;
  }
  if (auto* union_all = dynamic_cast<UnionAll*>(&root)) {
    for (const auto& member : union_all->members) {
      if (member) collector.Collect(*member);
    }
    return;
  }
  if (auto* view = dynamic_cast<View*>(&root)) {
    if (view->source) {
      collector.Collect(*view->source->sourceable);
      RegisterSourceAlias(*view->source, order, seen);
    }
    return;
  }
  if (auto* create_table = dynamic_cast<CreateTable*>(&root)) {
    if (create_table->source) {
      collector.Collect(*create_table->source->sourceable);
      RegisterSourceAlias(*create_table->source, order, seen);
    }
  }
}

void ApplyStatement(Expression& root, const std::unordered_map<std::string, std::string>& rename_map) {
  AliasApplier applier(rename_map);
  applier.Apply(root);
  applier.CommitRenames();
}

}  // namespace

std::unordered_map<std::string, std::string> TableAliasRenumberer::CollectRenameMap(Expression& root) {
  std::vector<std::string> order;
  std::unordered_set<std::string> seen;
  CollectStatement(root, order, seen);

  std::unordered_map<char, int> next_by_prefix;
  std::unordered_map<std::string, std::string> rename_map;
  for (const auto& old_name : order) {
    const char prefix = old_name[0];
    rename_map[old_name] = std::string(1, prefix) + std::to_string(next_by_prefix[prefix]++);
  }
  return rename_map;
}

void TableAliasRenumberer::ApplyRenameMap(Expression& root,
                                          const std::unordered_map<std::string, std::string>& rename_map) {
  if (rename_map.empty()) return;
  ApplyStatement(root, rename_map);
}

void TableAliasRenumberer::Renumber(Expression& root) {
  if (auto* multiple = dynamic_cast<MultipleStatements*>(&root)) {
    for (const auto& statement : multiple->statements) {
      if (statement) {
        Renumber(*statement);
      }
    }
    return;
  }

  auto rename_map = CollectRenameMap(root);
  ApplyRenameMap(root, rename_map);
}

}  // namespace sql::ast
}  // namespace rel2sql

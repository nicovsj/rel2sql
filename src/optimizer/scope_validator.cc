#include "scope_validator.h"

#include <fmt/core.h>

#include <algorithm>

#include "sql_ast/sql_ast.h"
#include "support/exceptions.h"

namespace rel2sql {
namespace sql::ast {

namespace {

std::string JoinNames(const std::vector<std::string>& names) {
  if (names.empty()) return "<none>";
  std::string out;
  for (size_t i = 0; i < names.size(); ++i) {
    if (i) out += ", ";
    out += names[i];
  }
  return out;
}

std::string JoinNames(const std::unordered_set<std::string>& names) {
  return JoinNames(std::vector<std::string>(names.begin(), names.end()));
}

// Mirrors the "explicit alias, else the wrapped Column's bare name, else the term's own
// printed form" convention already used elsewhere for this exact question (e.g.
// CTEInliner::GetColumnNameFromSelectable, Translator::GetColumnNameForSourceable).
std::string ExposedNameForSelectable(const Selectable& selectable) {
  if (selectable.HasAlias()) return selectable.Alias();
  if (const auto* ts = dynamic_cast<const TermSelectable*>(&selectable)) {
    if (const auto* c = dynamic_cast<const Column*>(ts->term.get())) return c->name;
  }
  return selectable.Alias();
}

}  // namespace

void ScopeValidator::Validate(Expression& root) {
  ScopeValidator validator;
  validator.Visit(root);
}

void ScopeValidator::PushCtes(const std::vector<std::shared_ptr<Source>>& ctes) {
  std::unordered_set<std::string> names;
  for (const auto& cte : ctes) {
    if (cte) names.insert(cte->Alias());
  }
  cte_scope_stack_.push_back(std::move(names));
}

void ScopeValidator::PopCtes() { cte_scope_stack_.pop_back(); }

bool ScopeValidator::IsCteVisible(const std::string& alias) const {
  for (const auto& scope : cte_scope_stack_) {
    if (scope.count(alias)) return true;
  }
  return false;
}

void ScopeValidator::Visit(Select& select) {
  PushCtes(select.ctes);

  std::unordered_set<std::string> from_aliases;
  if (select.from.has_value()) {
    for (const auto& source : select.from.value()->sources) {
      if (source) from_aliases.insert(source->Alias());
    }
  }
  auto saved_from_aliases = std::move(current_from_aliases_);
  current_from_aliases_ = std::move(from_aliases);

  ExpressionVisitor::Visit(select);

  current_from_aliases_ = std::move(saved_from_aliases);
  PopCtes();
}

void ScopeValidator::Visit(Union& union_expr) {
  PushCtes(union_expr.ctes);
  ExpressionVisitor::Visit(union_expr);
  PopCtes();
}

void ScopeValidator::Visit(UnionAll& union_all_expr) {
  PushCtes(union_all_expr.ctes);
  ExpressionVisitor::Visit(union_all_expr);
  PopCtes();
}

void ScopeValidator::Visit(Column& column) {
  // An unqualified column (no source) isn't a cross-source reference — nothing to check.
  if (!column.source.has_value()) return;
  const auto& source = column.source.value();
  const std::string alias = source->Alias();

  if (current_from_aliases_.count(alias) || IsCteVisible(alias)) {
    CheckColumnName(column, *source);
    return;
  }

  throw TranslationException(fmt::format("ScopeValidator: column '{}.{}' references an alias not visible here. "
                                         "Visible FROM sources: [{}].",
                                         alias, column.name, JoinNames(current_from_aliases_)),
                             ErrorCode::DANGLING_COLUMN_REFERENCE);
}

void ScopeValidator::CheckColumnName(const Column& column, const Source& source) const {
  // A source with explicit declared columns (a CTE's "WITH name(col1, col2)", or any other
  // Source built with def_columns) is authoritative about its own names regardless of what
  // its underlying SELECT happens to call them.
  if (!source.def_columns.empty()) {
    if (std::find(source.def_columns.begin(), source.def_columns.end(), column.name) != source.def_columns.end()) {
      return;
    }
    throw TranslationException(fmt::format("ScopeValidator: column '{}.{}' is not among {}'s declared columns [{}]",
                                           source.Alias(), column.name, source.Alias(), JoinNames(source.def_columns)),
                               ErrorCode::DANGLING_COLUMN_REFERENCE);
  }

  // Otherwise, only check sources transparent enough to introspect precisely: a plain SELECT
  // with no wildcard column. Anything else (a Table whose attribute names we may not have, a
  // Union, a Values list, ...) is left unchecked here — the alias-in-scope check in Visit
  // (Column&) is this validator's main guarantee; this is a bonus check that only fires when
  // it can be done without risking a false positive.
  auto select = std::dynamic_pointer_cast<Select>(source.sourceable);
  if (!select) return;

  std::vector<std::string> exposed;
  for (const auto& col : select->columns) {
    if (dynamic_cast<const Wildcard*>(col.get())) return;
    exposed.push_back(ExposedNameForSelectable(*col));
  }

  if (std::find(exposed.begin(), exposed.end(), column.name) != exposed.end()) return;

  throw TranslationException(fmt::format("ScopeValidator: column '{}.{}' is not among {}'s exposed columns [{}]",
                                         source.Alias(), column.name, source.Alias(), JoinNames(exposed)),
                             ErrorCode::DANGLING_COLUMN_REFERENCE);
}

}  // namespace sql::ast
}  // namespace rel2sql

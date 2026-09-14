#ifndef SCOPE_VALIDATOR_H
#define SCOPE_VALIDATOR_H

#include <unordered_set>
#include <vector>

#include "sql_ast/expr_visitor.h"

namespace rel2sql {
namespace sql::ast {

// Walks a translated (and optimized) SQL AST and verifies every Column reference actually
// resolves within its enclosing scope: the alias it names must be a FROM source or visible
// CTE at that point in the tree, and — where the referenced source is transparent enough to
// introspect — the column name itself must be one of that source's own exposed columns.
//
// This exists because several optimizer passes (CTEInliner, FlattenerOptimizer, ...) rewrite
// Column nodes by string-matching against a source's alias, and a bug in one of those rewrites
// produces a "dangling" reference: syntactically well-formed AST that references something
// no longer (or never) in scope. Left unchecked, that surfaces much later as a confusing
// DuckDB binder error — or not at all, if the malformed SQL happens to still parse — several
// passes and sometimes several queries away from whichever rewrite actually broke it. Running
// this immediately after the optimizer pipeline turns that into an immediate, precise
// TranslationException naming the exact column and alias involved.
//
// Deliberately conservative: when a source can't be fully introspected (a Table with unknown
// attribute names, a Union, a Values list, ...) this only checks that the alias itself is in
// scope and skips the column-name check, rather than risking a false positive.
class ScopeValidator : public ExpressionVisitor {
 public:
  using ExpressionVisitor::Visit;

  // Throws TranslationException (ErrorCode::DANGLING_COLUMN_REFERENCE) on the first dangling
  // reference found.
  static void Validate(Expression& root);

  void Visit(Select& select) override;
  void Visit(Union& union_expr) override;
  void Visit(UnionAll& union_all_expr) override;
  void Visit(Column& column) override;

 private:
  void PushCtes(const std::vector<std::shared_ptr<Source>>& ctes);
  void PopCtes();
  bool IsCteVisible(const std::string& alias) const;
  void CheckColumnName(const Column& column, const Source& source) const;

  // The FROM-source aliases visible to whichever Select's own columns/WHERE/GROUP
  // BY/ORDER BY are currently being visited. Table aliases are not visible to nested
  // subqueries, so this is simply saved and restored around each Select visit rather than
  // accumulated.
  std::unordered_set<std::string> current_from_aliases_;

  // CTE names are visible to the whole subtree under the Select/Union/UnionAll that
  // introduced them, so this accumulates as a stack of per-level name sets rather than a
  // single set that gets replaced.
  std::vector<std::unordered_set<std::string>> cte_scope_stack_;
};

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // SCOPE_VALIDATOR_H

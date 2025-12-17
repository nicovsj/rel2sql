#include "cte_inliner.h"

#include "replacers.h"

namespace rel2sql {
namespace sql::ast {

namespace {

class CTEFromRefCounter final : public ExpressionVisitor {
 public:
  explicit CTEFromRefCounter(std::string cte_name) : cte_name_(std::move(cte_name)) {}

  void Visit(FromStatement& from_statement) override {
    // Count references in this FROM's sources
    for (auto& source : from_statement.sources) {
      if (source && source->Alias() == cte_name_) {
        ++count_;
      }
      ExpressionVisitor::Visit(*source);
    }

    if (from_statement.where) {
      ExpressionVisitor::Visit(*from_statement.where.value());
    }
  }

  std::size_t count() const { return count_; }

 private:
  std::string cte_name_;
  std::size_t count_ = 0;
};

}  // namespace

void CTEInliner::Visit(SelectStatement& select_statement) {
  // Visit children first
  ExpressionVisitor::Visit(select_statement);

  // Do not attempt to inline / move recursive CTEs into the FROM clause.
  // Recursive CTEs rely on the SQL engine's recursive WITH support, and
  // rewriting them as plain subqueries would break semantics.
  if (select_statement.ctes_are_recursive) {
    return;
  }

  std::vector<std::shared_ptr<Source>> new_ctes;

  // Try to inline remaining CTEs into FROM clause
  for (auto& cte : select_statement.ctes) {
    if (!TryReplaceRedundantCTE(cte, select_statement)) {
      new_ctes.push_back(cte);
    }
  }

  select_statement.ctes = new_ctes;

  if (select_statement.ctes.empty()) {
    select_statement.ctes_are_recursive = false;
  }
}

std::size_t CTEInliner::CountCTEReferencesInFromClauses(const SelectStatement& root, const std::string& cte_name) {
  // ExpressionVisitor API is non-const; we only read, so const_cast is safe here.
  auto& mutable_root = const_cast<SelectStatement&>(root);
  CTEFromRefCounter counter(cte_name);
  mutable_root.Accept(counter);
  return counter.count();
}

bool CTEInliner::TryReplaceRedundantCTE(const std::shared_ptr<Source>& cte, const SelectStatement& owning_select) {
  auto cte_select = std::dynamic_pointer_cast<SelectStatement>(cte->sourceable);
  // CTE must be a SELECT statement
  if (!cte_select) return false;

  // Only inline if the CTE is referenced exactly once across all FROM clauses
  // within the owning SELECT statement.
  //
  // Reason: the inlining is implemented via a global replacer keyed by source alias,
  // so multiple FROM references would all be rewritten to the same inlined source,
  // potentially collapsing self-joins or otherwise changing semantics.
  auto ref_count = CountCTEReferencesInFromClauses(owning_select, cte->Alias());
  if (ref_count != 1) {
    return false;
  }

  // Try the simple case first: wildcard CTE with single table source
  if (TryReplaceSimpleWildcardCTE(cte, cte_select)) {
    return true;
  }

  // General case: move CTE as subquery into FROM clause
  return TryReplaceGeneralCTE(cte, cte_select);
}

bool CTEInliner::TryReplaceSimpleWildcardCTE(const std::shared_ptr<Source>& cte,
                                               const std::shared_ptr<SelectStatement>& cte_select) {
  // CTE must have a single wildcard column
  if (cte_select->columns.size() != 1 || !std::dynamic_pointer_cast<Wildcard>(cte_select->columns[0])) return false;

  // CTE must have a single source
  if (!cte_select->from.has_value() || cte_select->from.value()->sources.size() != 1) return false;

  auto original_source = cte_select->from.value()->sources[0];
  auto table = std::dynamic_pointer_cast<Table>(original_source->sourceable);

  auto new_source = std::make_shared<Source>(original_source->sourceable, cte->Alias());
  // Create a map of CTE column aliases to their new names
  // Use the table's actual attribute names if available, otherwise fall back to A1, A2, etc.
  std::unordered_map<std::string, std::shared_ptr<Term>> term_map;
  for (size_t i = 0; i < cte->def_columns.size(); ++i) {
    if (table) {
      std::string column_name = table->GetAttributeName(i);
      term_map[cte->def_columns[i]] = std::make_shared<Column>(column_name, new_source);
    } else {
      std::string column_name = fmt::format("A{}", i + 1);
      term_map[cte->def_columns[i]] = std::make_shared<Column>(column_name, new_source);
    }
  }

  // Create a replacer that handles both source name and column name replacements
  SourceAndColumnReplacer replacer(cte->Alias(), new_source, term_map, false);
  base_expr_->Accept(replacer);

  return true;
}

bool CTEInliner::TryReplaceGeneralCTE(const std::shared_ptr<Source>& cte,
                                        const std::shared_ptr<SelectStatement>& cte_select) {
  // Create a subquery source from the CTE's SELECT statement
  auto subquery_sourceable = std::static_pointer_cast<Sourceable>(cte_select);
  auto new_source = std::make_shared<Source>(subquery_sourceable, cte->Alias(), false);

  // Build column mapping
  // If def_columns is provided, use those as the column names
  // Otherwise, derive column names from the SELECT columns
  std::unordered_map<std::string, std::shared_ptr<Term>> column_map;

  if (!cte->def_columns.empty()) {
    // def_columns override the SELECT column names
    if (cte->def_columns.size() != cte_select->columns.size()) {
      // Mismatch in column count - cannot optimize
      return false;
    }
    for (size_t i = 0; i < cte->def_columns.size(); ++i) {
      std::string def_column_name = cte->def_columns[i];
      std::string select_column_name = GetColumnNameFromSelectable(cte_select->columns[i], i);
      column_map[def_column_name] = std::make_shared<Column>(select_column_name, new_source);
    }
  } else {
    // No def_columns - use SELECT column aliases or derive names
    for (size_t i = 0; i < cte_select->columns.size(); ++i) {
      std::string column_name = GetColumnNameFromSelectable(cte_select->columns[i], i);
      column_map[column_name] = std::make_shared<Column>(column_name, new_source);
    }
  }

  // Create a replacer that handles both source name and column name replacements
  SourceAndColumnReplacer replacer(cte->Alias(), new_source, column_map, false);
  base_expr_->Accept(replacer);

  return true;
}

std::string CTEInliner::GetColumnNameFromSelectable(const std::shared_ptr<Selectable>& selectable, size_t index) {
  // If the selectable has an alias, use it
  if (selectable->HasAlias()) {
    return selectable->Alias();
  }

  // If it's a TermSelectable with a Column term, use the column name
  auto term_selectable = std::dynamic_pointer_cast<TermSelectable>(selectable);
  if (term_selectable) {
    auto column = std::dynamic_pointer_cast<Column>(term_selectable->term);
    if (column) {
      return column->name;
    }
    // For other terms (constants, operations, etc.), use the term's string representation
    return term_selectable->term->ToString();
  }

  // For wildcards or other cases, derive a name
  return "A" + std::to_string(index + 1);
}

}  // namespace sql::ast
}  // namespace rel2sql

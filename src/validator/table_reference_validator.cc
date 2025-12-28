#include "validator/table_reference_validator.h"

#include <unordered_set>

namespace rel2sql {

namespace sql::ast {

void TableReferenceValidator::Visit(SelectStatement& select_statement) {
  // Collect aliases from CTEs first (they're available in the query)
  for (auto& cte : select_statement.ctes) {
    CollectAliasFromSource(*cte);
  }

  // Collect aliases from FROM clause
  if (select_statement.from) {
    for (auto& source : select_statement.from.value()->sources) {
      CollectAliasFromSource(*source);
    }
  }

  // Now validate all expressions in the SELECT statement
  // Visit columns in SELECT list
  for (auto& column : select_statement.columns) {
    column->Accept(*this);
  }

  // Visit FROM clause (which includes WHERE)
  if (select_statement.from) {
    select_statement.from.value()->Accept(*this);
  }

  // Visit GROUP BY
  if (select_statement.group_by) {
    select_statement.group_by.value()->Accept(*this);
  }
}

void TableReferenceValidator::Visit(FromStatement& from_statement) {
  // Visit sources (already collected aliases, but need to visit for subqueries)
  for (auto& source : from_statement.sources) {
    source->Accept(*this);
  }
  // Visit WHERE clause
  if (from_statement.where) {
    from_statement.where.value()->Accept(*this);
  }
}

void TableReferenceValidator::Visit(Column& column) {
  // If column has a source reference, validate it exists
  if (column.source.has_value()) {
    std::string alias = column.source.value()->Alias();
    if (available_aliases_.find(alias) == available_aliases_.end()) {
      RecordMissingAlias(alias);
    }
  }
}

void TableReferenceValidator::Visit(Wildcard& wildcard) {
  // If wildcard has a source reference, validate it exists
  if (wildcard.source.has_value()) {
    std::string alias = wildcard.source.value()->Alias();
    if (available_aliases_.find(alias) == available_aliases_.end()) {
      RecordMissingAlias(alias);
    }
  }
}

void TableReferenceValidator::Visit(Source& source) {
  // If this is a subquery, we need to validate it separately with its own scope
  if (source.is_subquery && source.sourceable) {
    // Create a new validator for the subquery (it has its own alias scope)
    TableReferenceValidator subquery_validator;
    source.sourceable->Accept(subquery_validator);
    // Merge errors from subquery (though they're in a different scope, we still report them)
    for (const auto& alias : subquery_validator.GetMissingTableAliases()) {
      RecordMissingAlias(alias);
    }
  }
  // For non-subquery sources, we don't need to visit further (already collected alias)
}

void TableReferenceValidator::Visit(Inclusion& inclusion) {
  // Visit columns
  for (auto& column : inclusion.columns) {
    column->Accept(*this);
  }
  // Visit subquery (it has its own scope)
  TableReferenceValidator subquery_validator;
  inclusion.select->Accept(subquery_validator);
  // Merge errors from subquery
  for (const auto& alias : subquery_validator.GetMissingTableAliases()) {
    RecordMissingAlias(alias);
  }
}

void TableReferenceValidator::Visit(Exists& exists) {
  // Visit subquery (it has its own scope)
  TableReferenceValidator subquery_validator;
  exists.select->Accept(subquery_validator);
  // Merge errors from subquery
  for (const auto& alias : subquery_validator.GetMissingTableAliases()) {
    RecordMissingAlias(alias);
  }
}

void TableReferenceValidator::Visit(Union& union_expr) {
  // Visit all members (each has its own scope)
  for (auto& member : union_expr.members) {
    TableReferenceValidator member_validator;
    member->Accept(member_validator);
    // Merge errors from each member
    for (const auto& alias : member_validator.GetMissingTableAliases()) {
      RecordMissingAlias(alias);
    }
  }
}

void TableReferenceValidator::Visit(UnionAll& union_all_expr) {
  // Visit all members (each has its own scope)
  for (auto& member : union_all_expr.members) {
    TableReferenceValidator member_validator;
    member->Accept(member_validator);
    // Merge errors from each member
    for (const auto& alias : member_validator.GetMissingTableAliases()) {
      RecordMissingAlias(alias);
    }
  }
}

void TableReferenceValidator::Visit(MultipleStatements& multiple_statements) {
  // Visit each statement (each has its own scope)
  for (auto& statement : multiple_statements.statements) {
    TableReferenceValidator statement_validator;
    statement->Accept(statement_validator);
    // Merge errors from each statement
    for (const auto& alias : statement_validator.GetMissingTableAliases()) {
      RecordMissingAlias(alias);
    }
  }
}

void TableReferenceValidator::CollectAliasFromSource(const Source& source) {
  if (source.alias.has_value()) {
    available_aliases_.insert(source.alias.value()->Access());
  } else if (auto table = std::dynamic_pointer_cast<Table>(source.sourceable)) {
    // If no alias, use table name
    available_aliases_.insert(table->name);
  }
}

void TableReferenceValidator::RecordMissingAlias(const std::string& alias) {
  missing_table_aliases_.insert(alias);
}

}  // namespace sql::ast
}  // namespace rel2sql

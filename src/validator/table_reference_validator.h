#ifndef TABLE_REFERENCE_VALIDATOR_H
#define TABLE_REFERENCE_VALIDATOR_H

#include <string>
#include <unordered_set>

#include "base_validator.h"

namespace rel2sql {

namespace sql::ast {

/**
 * Validator that checks if all table references in an SQL expression are consistent.
 *
 * Returns true if all table aliases referenced in expressions (WHERE, SELECT, etc.)
 * exist in the FROM clause (or CTEs), false otherwise.
 *
 * Example of invalid query:
 *   SELECT T0.A1 FROM B AS T0 WHERE T10.x = T11.x
 *   (T10 and T11 are referenced but don't exist in FROM)
 */
class TableReferenceValidator : public BaseValidator {
 public:
  using BaseValidator::Visit;

  // Default constructor - starts with empty scope
  TableReferenceValidator() = default;

  // Constructor that accepts parent scope aliases (for nested subqueries)
  explicit TableReferenceValidator(const std::unordered_set<std::string>& parent_aliases)
      : available_aliases_(parent_aliases) {}

  bool IsValid() const override { return missing_table_aliases_.empty(); }

  std::vector<ValidationError> GetErrors() const override {
    std::vector<ValidationError> errors;
    for (const auto& alias : missing_table_aliases_) {
      errors.push_back({"Table alias '" + alias + "' is referenced but does not exist in FROM clause or CTEs"});
    }
    return errors;
  }

  // Returns the set of table aliases that were referenced but don't exist
  const std::unordered_set<std::string>& GetMissingTableAliases() const { return missing_table_aliases_; }

  void Visit(Select& select) override;
  void Visit(From& from) override;
  void Visit(Column& column) override;
  void Visit(Wildcard& wildcard) override;
  void Visit(Source& source) override;
  void Visit(Inclusion& inclusion) override;
  void Visit(Exists& exists) override;
  void Visit(Union& union_expr) override;
  void Visit(UnionAll& union_all_expr) override;
  void Visit(MultipleStatements& multiple_statements) override;

 private:
  void CollectAliasFromSource(const Source& source);
  void RecordMissingAlias(const std::string& alias);

  std::unordered_set<std::string> available_aliases_;
  std::unordered_set<std::string> missing_table_aliases_;
};

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // TABLE_REFERENCE_VALIDATOR_H

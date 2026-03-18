#ifndef SQL_AST_CONST_REPLACER_H
#define SQL_AST_CONST_REPLACER_H

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>

#include "canonical_form.h"
#include "sql_ast/expr_visitor.h"
#include "sql_ast/sql_ast.h"

namespace rel2sql {

namespace sql::ast {

class ConstantReplacer : public ExpressionVisitor {
 public:
  ConstantReplacer(const std::string& table_name, const std::string& column_name, std::shared_ptr<Constant> constant)
      : table_name_(table_name), column_name_(column_name), constant_(constant) {}

  void Visit(ComparisonCondition& comparison_condition) override {
    if (auto column = std::dynamic_pointer_cast<Column>(comparison_condition.lhs)) {
      if (column->source && column->source.value()->Alias() == table_name_ && column->name == column_name_) {
        comparison_condition.lhs = constant_;
      }
    }

    if (auto column = std::dynamic_pointer_cast<Column>(comparison_condition.rhs)) {
      if (column->source && column->source.value()->Alias() == table_name_ && column->name == column_name_) {
        comparison_condition.rhs = constant_;
      }
    }
  }

 private:
  std::string table_name_;
  std::string column_name_;
  std::shared_ptr<Constant> constant_;
};

/**
 * Visitor that replaces references to a source and its columns with new references.
 * Used during query optimization to eliminate redundant sources and update column references.
 */
class SourceAndColumnReplacer : public ExpressionVisitor {
 public:
  SourceAndColumnReplacer(const std::string& old_source_name,
                          const std::unordered_map<std::string, std::shared_ptr<Term>>& term_map,
                          bool replace_alias = true)
      : old_source_name_(old_source_name), term_map_(term_map), replace_alias_(replace_alias) {}

  SourceAndColumnReplacer(const std::string& old_source_name, std::shared_ptr<Source> new_source,
                          const std::unordered_map<std::string, std::shared_ptr<Term>>& term_map,
                          bool replace_alias = true)
      : old_source_name_(old_source_name),
        new_source_(new_source),
        term_map_(term_map),
        replace_alias_(replace_alias) {}

  // Core helper: if a term slot currently holds a Column from the old source and
  // there is an entry in term_map_ for that column name, replace the entire slot
  // with the mapped term (which may be an arbitrary expression).
  void ReplaceTermSlot(std::shared_ptr<Term>& slot) {
    auto column = std::dynamic_pointer_cast<Column>(slot);
    if (!column) return;
    if (!column->source || column->source.value()->Alias() != old_source_name_) return;

    auto it = term_map_.find(column->name);
    if (it == term_map_.end()) return;

    // Replace the whole term (e.g. T1.x -> (T0.A1 - 1)/3)
    slot = it->second;
  }

  void Visit(TermSelectable& term_selectable) override {
    // When visiting a TermSelectable and the term is a column, it is a special case
    auto column = std::dynamic_pointer_cast<Column>(term_selectable.term);

    // If the term is not a column, visit the term
    if (!column) return ExpressionVisitor::Visit(term_selectable);

    // Column must have a source
    if (!column->source) return;

    // Column source must match old source name
    if (column->source.value()->Alias() != old_source_name_) return;

    auto it = term_map_.find(column->name);
    if (it != term_map_.end()) {
      if (replace_alias_ && !term_selectable.alias.has_value()) {
        term_selectable.alias = it->first;  // Replace TermSelectable's alias
      }
      term_selectable.term = it->second;
    }
  }

  // For generic Term references (e.g. in operations, parentheses, functions...),
  // dispatch to the concrete node type via Accept. Column occurrences are then
  // handled in the specific overrides below.
  void Visit(Term& term) override {
    term.Accept(*this);
  }

  void Visit(Operation& operation) override {
    // First recurse into children so nested expressions are rewritten.
    ExpressionVisitor::Visit(operation);
    // Then replace any direct Column children that should become full terms.
    ReplaceTermSlot(operation.lhs);
    ReplaceTermSlot(operation.rhs);
  }

  void Visit(ParenthesisTerm& parenthesis_term) override {
    // Recurse first.
    ExpressionVisitor::Visit(parenthesis_term);
    ReplaceTermSlot(parenthesis_term.term);
  }

  void Visit(Function& function) override {
    // Recurse first.
    ExpressionVisitor::Visit(function);
    ReplaceTermSlot(function.arg);
  }

  void Visit(CaseWhen& case_when) override {
    // Manually recurse so we can rewrite each term slot.
    for (auto& [condition, term] : case_when.cases) {
      ExpressionVisitor::Visit(*condition);
      ReplaceTermSlot(term);
    }
  }

  void Visit(ComparisonCondition& comparison_condition) override {
    // Recurse into child terms first so nested expressions get rewritten.
    ExpressionVisitor::Visit(*comparison_condition.lhs);
    ExpressionVisitor::Visit(*comparison_condition.rhs);

    // Now, if lhs/rhs are simple columns from the old source and there is a
    // mapping in term_map_, replace the entire term slot with the mapped term.
    ReplaceTermSlot(comparison_condition.lhs);
    ReplaceTermSlot(comparison_condition.rhs);
  }

  void Visit(From& from) override {
    if (!new_source_) {
      ExpressionVisitor::Visit(from);
      return;
    }

    for (auto& source : from.sources) {
      if (source->Alias() == old_source_name_) {
        source = new_source_;
        continue;
      }
      ExpressionVisitor::Visit(*source);
    }

    if (from.where) {
      ExpressionVisitor::Visit(*from.where.value());
    }
  }

  void Visit(Column& column) override {
    if (!column.source || column.source.value()->Alias() != old_source_name_) {
      return;
    }
    auto found = term_map_.find(column.name);
    if (found != term_map_.end()) {
      auto found_column = std::dynamic_pointer_cast<Column>(found->second);

      if (!found_column) return;

      column = *found_column;
    }
  }

 private:
  std::string old_source_name_;
  std::shared_ptr<Source> new_source_;
  std::unordered_map<std::string, std::shared_ptr<Term>> term_map_;
  bool replace_alias_;
};

class SourceReplacer : public ExpressionVisitor {
 public:
  SourceReplacer(const std::string& old_source_name, std::shared_ptr<Source> new_source)
      : old_source_name_(old_source_name), new_source_(new_source) {}

  void Visit(From& from) override {
    if (!new_source_) {
      ExpressionVisitor::Visit(from);
      return;
    }

    for (auto& source : from.sources) {
      if (source->Alias() == old_source_name_) {
        source = new_source_;
        continue;
      }
      ExpressionVisitor::Visit(*source);
    }

    if (from.where) {
      ExpressionVisitor::Visit(*from.where.value());
    }
  }

  void Visit(Column& column) override {
    if (!column.source || column.source.value()->Alias() != old_source_name_) {
      return;
    }
    column.source = new_source_;
  }

 private:
  std::string old_source_name_;
  std::shared_ptr<Source> new_source_;
};

class TableNameUpdater : public ExpressionVisitor {
 public:
  using ExpressionVisitor::Visit;
  TableNameUpdater(const std::string& old_table_name, const std::string& new_table_name)
      : old_table_name_(old_table_name), new_table_name_(new_table_name) {}

  void Visit(Table& table) override {
    if (table.name == old_table_name_) {
      table.name = new_table_name_;
    }
  }

 private:
  std::string old_table_name_;
  std::string new_table_name_;
};

/**
 * Visitor that flattens nested LogicalConditions so that AND and OR operators are not nested
 * when they could be at the same level. For example: (A AND B) AND C becomes A AND B AND C.
 */
class LogicalConditionFlattener : public ExpressionVisitor {
  void Visit(LogicalCondition& logical_condition) override {
    // First visit all children recursively (depth-first)
    for (auto& condition : logical_condition.conditions) {
      ExpressionVisitor::Visit(*condition);
    }

    // Only flatten AND and OR conditions (NOT doesn't make sense to flatten)
    if (logical_condition.op != LogicalOp::AND && logical_condition.op != LogicalOp::OR) {
      return;
    }

    // Build flattened list of conditions
    std::vector<std::shared_ptr<Condition>> flattened_conditions;

    for (auto& condition : logical_condition.conditions) {
      // If this condition is also a LogicalCondition with the same operator, extract its conditions
      auto nested_logical = std::dynamic_pointer_cast<LogicalCondition>(condition);
      if (nested_logical && nested_logical->op == logical_condition.op) {
        // Extract all conditions from the nested LogicalCondition
        flattened_conditions.insert(flattened_conditions.end(), nested_logical->conditions.begin(),
                                    nested_logical->conditions.end());
      } else {
        // Keep the condition as-is
        flattened_conditions.push_back(condition);
      }
    }

    // Replace the conditions vector with the flattened version
    logical_condition.conditions = std::move(flattened_conditions);
  }
};

/**
 * Visitor that removes duplicate sources and redundant equalities from the FROM clause and WHERE clause.
 */
class RedundancyReplacer : public ExpressionVisitor {
  void Visit(From& from) override {
    for (auto& source : from.sources) {
      ExpressionVisitor::Visit(*source);
    }

    std::unordered_set<std::string> seen_aliases;

    auto new_end = std::remove_if(from.sources.begin(), from.sources.end(),
                                  [&seen_aliases](const std::shared_ptr<Source>& source) {
                                    return !seen_aliases.insert(source->Alias()).second;  // Returns true if duplicate
                                  });

    from.sources.erase(new_end, from.sources.end());

    if (from.where) {
      ExpressionVisitor::Visit(*from.where.value());

      // Check if the WHERE condition should be removed
      // Case 1: Single ComparisonCondition that's redundant
      auto comp_condition = std::dynamic_pointer_cast<ComparisonCondition>(from.where.value());
      if (comp_condition) {
        std::set<EqualityPair> seen_equalities;
        if (IsRedundantEquality(from.where.value(), seen_equalities)) {
          from.where = std::nullopt;
        }
      }
      // Case 2: LogicalCondition that became empty after removing redundant conditions
      else if (from.where.value()->IsEmpty()) {
        from.where = std::nullopt;
      }
    }
  }

  void Visit(LogicalCondition& logical_condition) override {
    // Visit remaining conditions
    for (auto& condition : logical_condition.conditions) {
      ExpressionVisitor::Visit(*condition);
    }
    // Only process AND conditions for duplicate removal
    if (logical_condition.op != LogicalOp::AND) return;

    std::set<EqualityPair> seen_equalities;

    auto new_end = std::remove_if(logical_condition.conditions.begin(), logical_condition.conditions.end(),
                                  [&seen_equalities](const std::shared_ptr<Condition>& condition) {
                                    return IsRedundantEquality(condition, seen_equalities);
                                  });

    logical_condition.conditions.erase(new_end, logical_condition.conditions.end());
  }

 private:
  using ColumnId = std::pair<std::string, std::string>;  // (source_alias, column_name)
  using EqualityPair = std::pair<ColumnId, ColumnId>;    // (left_column, right_column)

  // Returns true if the condition is a duplicate equality between two columns
  static bool IsRedundantEquality(const std::shared_ptr<Condition>& condition,
                                  std::set<EqualityPair>& seen_equalities) {
    // Extract equality condition between two columns
    auto comp_condition = std::dynamic_pointer_cast<ComparisonCondition>(condition);
    if (!comp_condition || comp_condition->op != CompOp::EQ) {
      return false;  // Keep non-equality conditions
    }

    // If both sides of the equality are structurally identical terms
    // (e.g., T0.A1 - 1 = T0.A1 - 1), the comparison is redundant.
    if (comp_condition->lhs && comp_condition->rhs && *comp_condition->lhs == *comp_condition->rhs) {
      return true;
    }

    // If both sides are algebraically equal and reduce to the same column (e.g., T0.A1 = 2 * T0.A1 / 2),
    // the comparison is a tautology. This handles conditions introduced when the self-join optimizer
    // replaces columns.
    if (IsTautologyByCanonicalForm(comp_condition)) {
      return true;
    }

    auto left_column = std::dynamic_pointer_cast<Column>(comp_condition->lhs);
    auto right_column = std::dynamic_pointer_cast<Column>(comp_condition->rhs);
    if (!left_column || !right_column) {
      return false;  // Keep non-column comparisons
    }

    if (!left_column->source.has_value() || !right_column->source.has_value()) {
      return false;  // Keep columns without sources
    }

    // Create column identifiers
    ColumnId left_id{left_column->source.value()->Alias(), left_column->name};
    ColumnId right_id{right_column->source.value()->Alias(), right_column->name};

    if (left_id == right_id) {
      return true;  // Self-comparisons are redundant
    }

    // Normalize order so (A, B) and (B, A) are treated as the same equality
    EqualityPair equality_pair =
        (left_id < right_id) ? EqualityPair{left_id, right_id} : EqualityPair{right_id, left_id};

    // Return true if this equality was already seen (duplicate)
    return !seen_equalities.insert(equality_pair).second;
  }
};

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // SQL_AST_CONST_REPLACER_H

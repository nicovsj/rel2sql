#ifndef SQL_AST_CONST_REPLACER_H
#define SQL_AST_CONST_REPLACER_H

#include <memory>
#include <string>

#include "sql_ast/expr_visitor.h"
#include "sql_ast/sql_ast.h"

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

class SourceAndColumnReplacer : public ExpressionVisitor {
 public:
  SourceAndColumnReplacer(const std::string& old_source_name,
                          const std::unordered_map<std::string, std::shared_ptr<Column>>& column_map,
                          bool replace_alias = true)
      : old_source_name_(old_source_name), column_map_(column_map), replace_alias_(replace_alias) {}

  SourceAndColumnReplacer(const std::string& old_source_name, std::shared_ptr<Source> new_source,
                          const std::unordered_map<std::string, std::shared_ptr<Column>>& column_map,
                          bool replace_alias = true)
      : old_source_name_(old_source_name),
        new_source_(new_source),
        column_map_(column_map),
        replace_alias_(replace_alias) {}

  void Visit(TermSelectable& term_selectable) override {
    // When visiting a TermSelectable and the term is a column, it is a special case
    if (auto column = std::dynamic_pointer_cast<Column>(term_selectable.term)) {
      if (!column->source || column->source.value()->Alias() != old_source_name_) {
        return;
      }
      auto it = column_map_.find(column->name);
      if (it != column_map_.end()) {
        if (replace_alias_ && !term_selectable.alias.has_value()) {
          term_selectable.alias = it->first;  // Replace TermSelectable's alias
        }
        term_selectable.term = it->second;
      }
      return;
    }
    ExpressionVisitor::Visit(*term_selectable.term);
  }

  void Visit(FromStatement& from_statement) override {
    if (!new_source_) {
      ExpressionVisitor::Visit(from_statement);
      return;
    }

    for (auto& source : from_statement.sources) {
      if (source->Alias() == old_source_name_) {
        source = new_source_;
        continue;
      }
      ExpressionVisitor::Visit(*source);
    }

    if (from_statement.where) {
      ExpressionVisitor::Visit(*from_statement.where.value());
    }
  }

  void Visit(Column& column) override {
    if (!column.source || column.source.value()->Alias() != old_source_name_) {
      return;
    }
    auto it = column_map_.find(column.name);
    if (it != column_map_.end()) {
      column = *it->second;
    }
  }

 private:
  std::string old_source_name_;
  std::shared_ptr<Source> new_source_;
  std::unordered_map<std::string, std::shared_ptr<Column>> column_map_;
  bool replace_alias_;
};

}  // namespace sql::ast

#endif  // SQL_AST_CONST_REPLACER_H

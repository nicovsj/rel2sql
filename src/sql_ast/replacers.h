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
  SourceAndColumnReplacer(const std::string& old_source_name, const std::string& new_source_name,
                          const std::unordered_map<std::string, std::string>& column_map)
      : old_source_name_(old_source_name), new_source_name_(new_source_name), column_map_(column_map) {}

  void Visit(Source& source) override {
    ExpressionVisitor::Visit(*source.sourceable);
    if (source.alias) {
      auto& alias_statement = source.alias.value();
      if (alias_statement->name == old_source_name_) {
        alias_statement->name = new_source_name_;
        old_source_name_ = new_source_name_;
      }
    }
  }

  void Visit(Column& column) override {
    if (column.source && column.source.value()->Alias() == old_source_name_) {
      if (column.source.value()->alias) {
        column.source.value()->alias.value()->name = new_source_name_;
      } else if (auto table = std::dynamic_pointer_cast<Table>(column.source.value()->sourceable)) {
        table->name = new_source_name_;
      }
      old_source_name_ = new_source_name_;
      auto it = column_map_.find(column.name);
      if (it != column_map_.end()) {
        column.name = it->second;
      }
    }
  }

 private:
  std::string old_source_name_;
  std::string new_source_name_;
  std::unordered_map<std::string, std::string> column_map_;
};

}  // namespace sql::ast

#endif  // SQL_AST_CONST_REPLACER_H

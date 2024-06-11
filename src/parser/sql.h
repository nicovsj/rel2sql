#ifndef SQL_H
#define SQL_H

#include <fmt/core.h>

#include <iostream>
#include <optional>
#include <string>
#include <vector>

enum class CompOp {
  EQ,
  NEQ,
  LT,
  GT,
  LTE,
  GTE,
};

enum class LogicalOp {
  AND,
  OR,
};

using value_t = std::variant<int, double, std::string>;

class Expression {
 public:
  virtual ~Expression() = default;
  virtual std::ostream& print(std::ostream& os) const = 0;

  friend std::ostream& operator<<(std::ostream& os, const Expression& expr) { return expr.print(os); }
};

class Table : public Expression {
 public:
  std::string name;

  Table(std::string name) : name(name) {}

  std::ostream& print(std::ostream& os) const override { return os << name; }
};

class Column : public Expression {
 public:
  std::string name;
  std::optional<std::shared_ptr<Table>> table;

  Column(std::string name) : name(name) {}
  Column(std::string name, std::shared_ptr<Table> table) : name(name), table(table) {}

  std::ostream& print(std::ostream& os) const override {
    if (table.has_value()) {
      auto table_ptr = table.value();
      return os << *table_ptr << "." << name;
    }

    return os << name;
  }
};

class Condition : public Expression {
 public:
  virtual ~Condition() = default;
  virtual std::ostream& print(std::ostream& os) const = 0;
};

class ValueCondition : public Condition {
 public:
  std::shared_ptr<Column> lhs;
  CompOp op;
  value_t rhs;

  ValueCondition(std::shared_ptr<Column> column, CompOp op, value_t value) : lhs(column), op(op), rhs(value) {}

  std::string get_operator_string(CompOp op) const {
    switch (op) {
      case CompOp::EQ:
        return "=";
      case CompOp::NEQ:
        return "!=";
      case CompOp::LT:
        return "<";
      case CompOp::GT:
        return ">";
      case CompOp::LTE:
        return "<=";
      case CompOp::GTE:
        return ">=";
    }
  }

  std::ostream& print(std::ostream& os) const override {
    return os << *lhs << " " << get_operator_string(op) << " "
              << std::visit(
                     [](auto&& arg) -> std::string {
                       using T = std::decay_t<decltype(arg)>;
                       if constexpr (std::is_same_v<T, int>) {
                         return std::to_string(arg);
                       } else if constexpr (std::is_same_v<T, double>) {
                         return std::to_string(arg);
                       } else if constexpr (std::is_same_v<T, std::string>) {
                         return fmt::format("'{}'", arg);
                       }
                     },
                     rhs);
  }
};

class ColumnComparisonCondition : public Condition {
 public:
  std::shared_ptr<Column> lhs;
  CompOp op;
  std::shared_ptr<Column> rhs;

  std::string get_operator_string(CompOp op) const {
    switch (op) {
      case CompOp::EQ:
        return "=";
      case CompOp::NEQ:
        return "!=";
      case CompOp::LT:
        return "<";
      case CompOp::GT:
        return ">";
      case CompOp::LTE:
        return "<=";
      case CompOp::GTE:
        return ">=";
    }
  }

  std::ostream& print(std::ostream& os) const override {
    return os << *lhs << " " << get_operator_string(op) << " " << *rhs;
  }

  ColumnComparisonCondition(std::shared_ptr<Column> lhs, CompOp op, std::shared_ptr<Column> rhs)
      : lhs(lhs), op(op), rhs(rhs) {}
};

class LogicalCondition : public Condition {
 public:
  std::vector<std::shared_ptr<Condition>> conditions;
  LogicalOp op;

  std::string get_operator_string(LogicalOp op) const {
    switch (op) {
      case LogicalOp::AND:
        return "AND";
      case LogicalOp::OR:
        return "OR";
    }
  }

  std::ostream& print(std::ostream& os) const override {
    for (size_t i = 0; i < conditions.size(); i++) {
      os << *conditions[i];
      if (i < conditions.size() - 1) {
        os << " " << get_operator_string(op) << " ";
      }
    }

    return os;
  }

  LogicalCondition(std::vector<std::shared_ptr<Condition>> conditions, LogicalOp op) : conditions(conditions), op(op) {}
};

class SelectStatement : public Expression {
 public:
  std::vector<std::shared_ptr<Column>> columns;
  std::vector<std::shared_ptr<Table>> table;
  std::shared_ptr<Condition> where;

  SelectStatement(std::vector<std::shared_ptr<Column>> columns, std::vector<std::shared_ptr<Table>> table,
                  std::shared_ptr<Condition> where)
      : columns(columns), table(table), where(where) {}

  std::ostream& print(std::ostream& os) const override {
    os << "SELECT ";
    for (size_t i = 0; i < columns.size(); i++) {
      os << *columns[i];
      if (i < columns.size() - 1) {
        os << ", ";
      }
    }

    os << " FROM ";
    for (size_t i = 0; i < table.size(); i++) {
      os << *table[i];
      if (i < table.size() - 1) {
        os << ", ";
      }
    }

    if (where != nullptr) {
      os << " WHERE " << *where;
    }

    return os;
  }
};

#endif  // SQL_H

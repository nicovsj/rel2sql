#ifndef SQL_H
#define SQL_H

#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "utils.h"

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

class SelectStatement;

class Expression {
 public:
  virtual ~Expression() = default;
  virtual std::ostream& Print(std::ostream& os) const = 0;

  friend std::ostream& operator<<(std::ostream& os, const Expression& expr) { return expr.Print(os); }
};

class Source : public Expression {
 public:
  virtual ~Source() = default;
  virtual std::ostream& Print(std::ostream& os) const = 0;

  virtual std::string Alias() const = 0;
};

class Subquery : public Source {
 public:
  std::shared_ptr<SelectStatement> select;
  std::string alias;

  Subquery(std::shared_ptr<SelectStatement> select, std::string alias) : select(select), alias(alias) {}

  std::ostream& Print(std::ostream& os) const override;

  std::string Alias() const override { return alias; }
};

class Table : public Source {
 public:
  std::string name;
  std::optional<std::string> alias;

  Table(std::string name) : name(name) {}
  Table(std::string name, std::string alias) : name(name), alias(alias) {}

  std::ostream& Print(std::ostream& os) const override { return os << Alias(); };

  std::string Alias() const override {
    if (alias.has_value()) {
      return alias.value();
    }

    return name;
  }
};

class Column : public Expression {
 public:
  std::string name;
  std::optional<std::shared_ptr<Source>> source;

  Column(std::string name) : name(name) {}
  Column(std::string name, std::shared_ptr<Source> source) : name(name), source(source) {}

  std::ostream& Print(std::ostream& os) const override {
    if (source.has_value()) {
      auto table_ptr = source.value();
      return os << table_ptr->Alias() << "." << name;
    }

    return os << name;
  }
};

class Condition : public Expression {
 public:
  virtual ~Condition() = default;
  virtual std::ostream& Print(std::ostream& os) const = 0;
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

  std::ostream& Print(std::ostream& os) const override {
    std::string value_str = std::visit(
        utl::overloaded{[](int arg) { return std::to_string(arg); }, [](double arg) { return std::to_string(arg); },
                        [](std::string arg) { return fmt::format("'{}'", arg); }},
        rhs);
    return os << *lhs << " " << get_operator_string(op) << " " << value_str;
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

  std::ostream& Print(std::ostream& os) const override {
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

  std::ostream& Print(std::ostream& os) const override {
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
  std::vector<std::shared_ptr<Source>> sources;
  std::shared_ptr<Condition> where;

  SelectStatement(std::vector<std::shared_ptr<Column>> columns, std::vector<std::shared_ptr<Source>> sources,
                  std::shared_ptr<Condition> where)
      : columns(columns), sources(sources), where(where) {}

  std::ostream& Print(std::ostream& os) const override {
    os << "SELECT ";
    for (size_t i = 0; i < columns.size(); i++) {
      os << *columns[i];
      if (i < columns.size() - 1) {
        os << ", ";
      }
    }

    os << " FROM ";
    for (size_t i = 0; i < sources.size(); i++) {
      os << *sources[i];
      if (i < sources.size() - 1) {
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

#ifndef SQL_H
#define SQL_H

#include <antlr4-runtime.h>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "utils.h"

namespace sql::ast {

enum class CompOp {
  EQ,
  NEQ,
  LT,
  GT,
  LTE,
  GTE,
};

enum class LogicalOp { AND, OR, NOT };

using constant_t = std::variant<int, double, std::string>;

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

class Selectable : public Expression {
 public:
  virtual ~Selectable() = default;
  virtual std::ostream& Print(std::ostream& os) const = 0;

  virtual std::string Alias() const = 0;
  virtual bool HasAlias() const = 0;
};

class Column : public Selectable {
 public:
  std::string name;
  std::optional<std::shared_ptr<Source>> source;
  std::optional<std::string> alias;

  Column(std::string name) : name(name) {}
  Column(std::string name, std::shared_ptr<Source> source) : name(name), source(source) {}
  Column(std::string name, std::shared_ptr<Source> source, std::string alias)
      : name(name), source(source), alias(alias) {}

  std::string Alias() const override {
    if (alias.has_value()) {
      return alias.value();
    }

    return name;
  }

  bool HasAlias() const override { return alias.has_value(); }

  std::ostream& Print(std::ostream& os) const override {
    if (source.has_value()) {
      auto table_ptr = source.value();
      return os << table_ptr->Alias() << "." << name;
    }

    return os << name;
  }
};

class Wildcard : public Selectable {
 public:
  std::optional<std::shared_ptr<Source>> source;

  Wildcard() = default;

  Wildcard(std::shared_ptr<Source> source) : source(source) {}

  std::ostream& Print(std::ostream& os) const override {
    if (source.has_value()) {
      return os << source.value()->Alias() << "." << Alias();
    }

    return os << Alias();
  }

  std::string Alias() const override { return "*"; }

  bool HasAlias() const override { return false; }
};

class Constant : public Expression {
 public:
  constant_t value;

  Constant(constant_t value) : value(value) {}

  std::ostream& Print(std::ostream& os) const override { return os << ToString(); }

  std::string ToString() const {
    return std::visit(
        utl::overloaded{[](int arg) { return std::to_string(arg); }, [](double arg) { return std::to_string(arg); },
                        [](std::string arg) { return fmt::format("'{}'", arg); }},
        value);
  }
};

class SelectableConstant : public Selectable {
 public:
  std::shared_ptr<Constant> constant;
  std::optional<std::string> alias;

  SelectableConstant(std::shared_ptr<Constant> constant) : constant(constant) {}

  std::ostream& Print(std::ostream& os) const override { return os << *constant; }

  std::string Alias() const override {
    if (alias.has_value())
      return alias.value();
    else
      return constant->ToString();
  }

  bool HasAlias() const override { return alias.has_value(); }
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
  std::shared_ptr<Constant> rhs;

  ValueCondition(std::shared_ptr<Column> column, CompOp op, std::shared_ptr<Constant> value)
      : lhs(column), op(op), rhs(value) {}

  ValueCondition(std::shared_ptr<Column> column, CompOp op, constant_t value)
      : lhs(column), op(op), rhs(std::make_shared<Constant>(value)) {}

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
      case LogicalOp::NOT:
        return "NOT";
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

class Inclusion : public Condition {
 public:
  std::vector<std::shared_ptr<Column>> columns;
  std::shared_ptr<SelectStatement> select;
  bool is_not;

  Inclusion(std::vector<std::shared_ptr<Column>> columns, std::shared_ptr<SelectStatement> select, bool is_not = false)
      : columns(columns), select(select), is_not(is_not) {}

  std::ostream& Print(std::ostream& os) const override;
};

class Exists : public Condition {
 public:
  std::shared_ptr<SelectStatement> select;

  Exists(std::shared_ptr<SelectStatement> select) : select(select) {}

  std::ostream& Print(std::ostream& os) const override;
};

class FromStatement : public Expression {
 public:
  std::vector<std::shared_ptr<Source>> sources;
  std::optional<std::shared_ptr<Condition>> where;

  FromStatement(std::vector<std::shared_ptr<Source>> sources) : sources(sources) {}

  FromStatement(std::vector<std::shared_ptr<Source>> sources, std::shared_ptr<Condition> where)
      : sources(sources), where(where) {}

  std::ostream& Print(std::ostream& os) const override {
    os << "FROM ";
    for (size_t i = 0; i < sources.size(); i++) {
      os << *sources[i];
      if (i < sources.size() - 1) {
        os << ", ";
      }
    }

    if (where.has_value()) {
      os << " WHERE " << *where.value();
    }

    return os;
  }
};

class SelectStatement : public Expression {
 public:
  std::vector<std::shared_ptr<Selectable>> columns;
  std::optional<std::shared_ptr<FromStatement>> from;

  SelectStatement(const std::vector<std::shared_ptr<Selectable>>& columns) : columns(columns) {}

  SelectStatement(const std::vector<std::shared_ptr<Selectable>>& columns, std::shared_ptr<FromStatement> from)
      : columns(columns), from(from) {}

  std::ostream& Print(std::ostream& os) const override {
    os << "SELECT ";
    for (size_t i = 0; i < columns.size(); i++) {
      if (columns[i]->HasAlias()) {
        os << *columns[i] << " AS " << columns[i]->Alias();
      } else {
        os << *columns[i];
      }
      if (i < columns.size() - 1) {
        os << ", ";
      }
    }

    if (from.has_value()) {
      os << " " << *from.value();
    }

    return os;
  }
};

class Union : public Expression {
 public:
  std::shared_ptr<SelectStatement> lhs;
  std::shared_ptr<SelectStatement> rhs;

  Union(std::shared_ptr<SelectStatement> lhs, std::shared_ptr<SelectStatement> rhs) : lhs(lhs), rhs(rhs) {}

  std::ostream& Print(std::ostream& os) const override {
    os << *lhs << " UNION " << *rhs;
    return os;
  }
};

// UTILITY FUNCTIONS

using ParserRuleContext = antlr4::ParserRuleContext;

}  // namespace sql::ast

#endif  // SQL_H

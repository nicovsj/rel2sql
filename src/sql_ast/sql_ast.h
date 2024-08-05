#ifndef SQL_H
#define SQL_H

#include <antlr4-runtime.h>
#include <fmt/core.h>

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "utils/utils.h"

namespace sql::ast {

enum class CompOp {
  EQ,
  NEQ,
  LT,
  GT,
  LTE,
  GTE,
};

enum class AggregateFunction { COUNT, SUM, AVG, MIN, MAX };

enum class LogicalOp { AND, OR, NOT };

using constant_t = std::variant<int, double, std::string, bool>;

class SelectStatement;
class Values;

class Expression {
 public:
  virtual ~Expression() = default;
  virtual std::ostream& Print(std::ostream& os) const = 0;

  friend std::ostream& operator<<(std::ostream& os, const Expression& expr) { return expr.Print(os); }
};

class Sourceable : public Expression {
 public:
  virtual ~Sourceable() = default;
  virtual std::ostream& Print(std::ostream& os) const = 0;
};

class AliasStatement : public Expression {
 public:
  std::string name;
  std::vector<std::string> columns;

  AliasStatement(std::string name) : name(name) {}

  AliasStatement(std::string name, std::vector<std::string> columns) : name(name), columns(columns) {}

  std::ostream& Print(std::ostream& os) const override { return os << Access(); }

  std::string Declaration() const {
    std::stringstream os;
    os << name;
    if (!columns.empty()) {
      os << "(";
      for (size_t i = 0; i < columns.size(); i++) {
        os << columns[i];
        if (i < columns.size() - 1) {
          os << ", ";
        }
      }
      os << ")";
    }
    return os.str();
  }

  std::string Access() const { return name; }
};

class Source : public Expression {
 public:
  std::shared_ptr<Sourceable> sourceable;
  std::optional<std::shared_ptr<AliasStatement>> alias;
  bool is_subquery;
  bool is_cte;

  static bool CheckIsSubquery(std::shared_ptr<Sourceable> sourceable) {
    return std::dynamic_pointer_cast<SelectStatement>(sourceable) != nullptr ||
           std::dynamic_pointer_cast<Values>(sourceable) != nullptr;
  }

  Source(std::shared_ptr<Sourceable> sourceable)
      : sourceable(sourceable), is_subquery(CheckIsSubquery(sourceable)), is_cte(false) {
    if (is_subquery) {
      throw std::runtime_error("Subquery must have an alias");
    }
  }

  Source(std::shared_ptr<Sourceable> sourceable, std::string alias, bool is_cte = false)
      : sourceable(sourceable),
        alias(std::make_shared<AliasStatement>(alias)),
        is_subquery(CheckIsSubquery(sourceable)),
        is_cte(is_cte) {}

  Source(std::shared_ptr<Sourceable> sourceable, std::shared_ptr<AliasStatement> alias, bool is_cte = false)
      : sourceable(sourceable), alias(alias), is_subquery(CheckIsSubquery(sourceable)), is_cte(is_cte) {}

  virtual std::ostream& Print(std::ostream& os) const {
    if (is_cte) {
      os << alias.value()->Access();
      return os;
    }

    os << Definition();

    if (alias.has_value()) {
      os << " AS " << alias.value()->Declaration();
    }

    return os;
  }

  virtual std::string Alias() const {
    std::stringstream os;

    if (alias.has_value()) {
      os << *(alias.value());
    } else {
      os << *sourceable;
    }
    return os.str();
  }

  virtual std::string Definition() const {
    std::stringstream os;

    if (is_subquery) {
      os << "(" << *sourceable << ")";
    } else {
      os << *sourceable;
    }

    return os.str();
  }

  virtual std::string Declaration() const {
    if (alias.has_value()) {
      return alias.value()->Declaration();
    } else {
      return Definition();
    }
  }
};

class Table : public Sourceable {
 public:
  std::string name;

  Table(std::string name) : name(name) {}

  std::ostream& Print(std::ostream& os) const override { return os << name; };
};

class Selectable : public Expression {
 public:
  virtual ~Selectable() = default;
  virtual std::ostream& Print(std::ostream& os) const = 0;

  virtual std::string Alias() const = 0;
  virtual bool HasAlias() const = 0;
};

class Wildcard : public Selectable {
 public:
  std::optional<std::shared_ptr<Source>> source;

  Wildcard() = default;

  Wildcard(std::shared_ptr<Source> source) : source(source) {}

  std::ostream& Print(std::ostream& os) const override {
    if (source.has_value()) {
      os << source.value()->Alias() << ".";
    }

    return os << Alias();
  }

  std::string Alias() const override { return "*"; }

  bool HasAlias() const override { return false; }
};

class Term : public Expression {
 public:
  virtual ~Term() = default;
  virtual std::ostream& Print(std::ostream& os) const = 0;

  virtual std::string ToString() const = 0;
};

class TermSelectable : public Selectable {
 public:
  std::shared_ptr<Term> term;
  std::optional<std::string> alias;

  TermSelectable(std::shared_ptr<Term> term) : term(term) {}
  TermSelectable(std::shared_ptr<Term> term, std::string alias) : term(term), alias(alias) {}

  std::ostream& Print(std::ostream& os) const override { return os << *term; }

  std::string Alias() const override {
    if (alias.has_value())
      return alias.value();
    else
      return term->ToString();
  }

  bool HasAlias() const override { return alias.has_value(); }
};

class Constant : public Term {
 public:
  constant_t value;

  Constant(constant_t value) : value(value) {}

  std::ostream& Print(std::ostream& os) const override { return os << ToString(); }

  std::string ToString() const override {
    return std::visit(
        utl::overloaded{[](int arg) { return std::to_string(arg); }, [](double arg) { return std::to_string(arg); },
                        [](std::string arg) { return fmt::format("'{}'", arg); },
                        [](bool arg) { return arg ? std::string("TRUE") : std::string("FALSE"); }},
        value);
  }
};

class Function : public Term {
 public:
  AggregateFunction name;
  std::shared_ptr<Term> arg;

  Function(AggregateFunction name, std::shared_ptr<Term> arg) : name(name), arg(arg) {}

  std::ostream& Print(std::ostream& os) const override { return os << ToString(); }

  std::string ToString() const override {
    std::string result = "";
    switch (name) {
      case AggregateFunction::COUNT:
        result += "COUNT";
        break;
      case AggregateFunction::SUM:
        result += "SUM";
        break;
      case AggregateFunction::AVG:
        result += "AVG";
        break;
      case AggregateFunction::MIN:
        result += "MIN";
        break;
      case AggregateFunction::MAX:
        result += "MAX";
        break;
    }

    return result + "(" + arg->ToString() + ")";
  }
};

class Column : public Term {
 public:
  std::string name;
  std::optional<std::shared_ptr<Source>> source;

  Column(std::string name) : name(name) {}
  Column(std::string name, std::shared_ptr<Source> source) : name(name), source(source) {}

  std::string ToString() const override {
    std::string result = "";

    if (source.has_value()) {
      result += source.value()->Alias() + ".";
    }

    return result + name;
  };

  std::ostream& Print(std::ostream& os) const override { return os << ToString(); }
};

class Values : public Sourceable {
 public:
  std::vector<std::vector<Constant>> values;

  Values(std::vector<std::vector<Constant>> values) : values(values) {}

  Values(std::vector<std::vector<constant_t>> values) {
    for (auto& row : values) {
      std::vector<Constant> row_constants;
      for (auto& value : row) {
        row_constants.push_back(Constant(value));
      }
      this->values.push_back(row_constants);
    }
  }

  std::ostream& Print(std::ostream& os) const override {
    os << "VALUES ";
    for (size_t i = 0; i < values.size(); i++) {
      auto row = values[i];
      os << "(";
      for (size_t j = 0; j < row.size(); j++) {
        os << row[j];
        if (j < row.size() - 1) {
          os << ", ";
        }
      }
      os << ")";
      if (i < values.size() - 1) {
        os << ", ";
      }
    }
    return os;
  }
};

class Condition : public Expression {
 public:
  virtual ~Condition() = default;
  virtual std::ostream& Print(std::ostream& os) const = 0;

  virtual bool IsEmpty() const = 0;
};

class ComparisonCondition : public Condition {
 public:
  std::shared_ptr<Term> lhs;
  CompOp op;
  std::shared_ptr<Term> rhs;

  ComparisonCondition(std::shared_ptr<Term> lhs, CompOp op, std::shared_ptr<Term> rhs) : lhs(lhs), op(op), rhs(rhs) {}

  ComparisonCondition(std::shared_ptr<Term> lhs, CompOp op, constant_t value)
      : lhs(lhs), op(op), rhs(std::make_shared<Constant>(value)) {}

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

  bool IsEmpty() const override { return false; }
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

  LogicalCondition(std::vector<std::shared_ptr<Condition>> conditions, LogicalOp op) : conditions(conditions), op(op) {
    this->conditions.erase(std::remove_if(this->conditions.begin(), this->conditions.end(),
                                          [](const std::shared_ptr<Condition>& cond) { return cond->IsEmpty(); }),
                           this->conditions.end());
  }

  bool IsEmpty() const override {
    return std::all_of(conditions.begin(), conditions.end(),
                       [](std::shared_ptr<Condition> cond) { return cond->IsEmpty(); });
  }
};

class Inclusion : public Condition {
 public:
  std::vector<std::shared_ptr<Column>> columns;
  std::shared_ptr<SelectStatement> select;
  bool is_not;

  Inclusion(std::vector<std::shared_ptr<Column>> columns, std::shared_ptr<SelectStatement> select, bool is_not = false)
      : columns(columns), select(select), is_not(is_not) {}

  std::ostream& Print(std::ostream& os) const override;

  bool IsEmpty() const override { return false; }
};

class Exists : public Condition {
 public:
  std::shared_ptr<SelectStatement> select;

  Exists(std::shared_ptr<SelectStatement> select) : select(select) {}

  std::ostream& Print(std::ostream& os) const override;

  bool IsEmpty() const override { return false; }
};

class CaseWhen : public Term {
 public:
  std::vector<std::pair<std::shared_ptr<Condition>, std::shared_ptr<Term>>> cases;

  CaseWhen(std::vector<std::pair<std::shared_ptr<Condition>, std::shared_ptr<Term>>> cases) : cases(cases) {};

  std::ostream& Print(std::ostream& os) const override { return os << ToString(); }

  std::string ToString() const override {
    std::stringstream ss;
    ss << "CASE";
    for (auto& [condition, term] : cases) {
      ss << " WHEN " << *condition;
      ss << " THEN " << *term;
    }
    ss << " END";
    return ss.str();
  }
};

class FromStatement : public Expression {
 public:
  std::vector<std::shared_ptr<Source>> sources;
  std::optional<std::shared_ptr<Condition>> where;

  FromStatement(std::shared_ptr<Source> source) : sources({source}) {}

  FromStatement(std::shared_ptr<Source> source, std::shared_ptr<Condition> where) : sources({source}), where(where) {}

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

    if (where.has_value() && !where.value()->IsEmpty()) {
      os << " WHERE " << *where.value();
    }

    return os;
  }
};

class View : public Expression {
 public:
  std::shared_ptr<Source> source;

  View(std::shared_ptr<Source> source) : source(source) {}

  View(std::shared_ptr<Sourceable> sourceable, std::string alias)
      : source(std::make_shared<Source>(sourceable, alias)) {}

  std::ostream& Print(std::ostream& os) const override {
    return os << "CREATE VIEW " << source->Declaration() << " AS " << source->Definition();
  }
};

class GroupBy : public Expression {
 public:
  std::vector<std::shared_ptr<Column>> columns;

  GroupBy(std::vector<std::shared_ptr<Column>> columns) : columns(columns) {}

  std::ostream& Print(std::ostream& os) const override {
    os << "GROUP BY ";
    for (size_t i = 0; i < columns.size(); i++) {
      os << *columns[i];
      if (i < columns.size() - 1) {
        os << ", ";
      }
    }

    return os;
  }
};

class SelectStatement : public Sourceable {
 public:
  std::vector<std::shared_ptr<Selectable>> columns;
  std::optional<std::shared_ptr<FromStatement>> from;
  std::vector<std::shared_ptr<Source>> ctes;
  std::optional<std::shared_ptr<GroupBy>> group_by;

  SelectStatement(const std::vector<std::shared_ptr<Selectable>>& columns) : columns(columns) {}

  SelectStatement(const std::vector<std::shared_ptr<Selectable>>& columns, std::shared_ptr<FromStatement> from)
      : columns(columns), from(from) {}

  SelectStatement(const std::vector<std::shared_ptr<Selectable>>& columns, std::shared_ptr<FromStatement> from,
                  std::shared_ptr<GroupBy> group_by)
      : columns(columns), from(from), group_by(group_by) {}

  SelectStatement(const std::vector<std::shared_ptr<Selectable>>& columns, std::shared_ptr<FromStatement> from,
                  std::vector<std::shared_ptr<Source>> ctes)
      : columns(columns), from(from), ctes(ctes) {}

  std::ostream& Print(std::ostream& os) const override {
    for (auto& cte : ctes) {
      os << "WITH " << cte->Declaration() << " AS " << cte->Definition() << " ";
    }
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

    if (group_by.has_value()) {
      os << " " << *group_by.value();
    }

    return os;
  }
};

class Union : public Sourceable {
 public:
  std::vector<std::shared_ptr<SelectStatement>> members;

  Union(std::shared_ptr<SelectStatement> lhs, std::shared_ptr<SelectStatement> rhs) : members({lhs, rhs}) {}

  Union(std::vector<std::shared_ptr<SelectStatement>> members) : members(members) {}

  std::ostream& Print(std::ostream& os) const override {
    for (size_t i = 0; i < members.size(); i++) {
      os << *members[i];
      if (i < members.size() - 1) {
        os << " UNION ";
      }
    }
    return os;
  }
};

// UTILITY FUNCTIONS

using ParserRuleContext = antlr4::ParserRuleContext;

}  // namespace sql::ast

#endif  // SQL_H

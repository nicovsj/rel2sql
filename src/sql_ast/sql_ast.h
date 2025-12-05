#ifndef SQL_H
#define SQL_H

#include <antlr4-runtime.h>
#include <fmt/core.h>

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "sql_ast/expr_visitor.h"
#include "support/utils.h"

namespace rel2sql {

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
  virtual void Accept(ExpressionVisitor& visitor) = 0;

  // Convenience method that returns a string representation using the Print method
  std::string ToString() const;

  // Virtual equality method for polymorphic comparison
  virtual bool Equals(const Expression& other) const = 0;

  friend std::ostream& operator<<(std::ostream& os, const Expression& expr) { return expr.Print(os); }
};

// Non-member operator== for Expression
inline bool operator==(const Expression& lhs, const Expression& rhs) { return lhs.Equals(rhs); }

inline bool operator!=(const Expression& lhs, const Expression& rhs) { return !(lhs == rhs); }

class Sourceable : public Expression {
 public:
  virtual ~Sourceable() = default;
  virtual std::ostream& Print(std::ostream& os) const override = 0;
  virtual void Accept(ExpressionVisitor& visitor) override = 0;
};

class AliasStatement : public Expression {
 public:
  std::string name;
  std::vector<std::string> columns;

  AliasStatement(std::string name) : name(name) {}

  AliasStatement(std::string name, std::vector<std::string> columns) : name(name), columns(columns) {}

  std::ostream& Print(std::ostream& os) const override { return os << Access(); }

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_alias = dynamic_cast<const AliasStatement*>(&other);
    if (!other_alias) return false;
    return name == other_alias->name && columns == other_alias->columns;
  }

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
  std::vector<std::string> def_columns;
  bool is_subquery;
  bool is_cte;

  static bool CheckIsSubquery(std::shared_ptr<Sourceable> sourceable) {
    return std::dynamic_pointer_cast<SelectStatement>(sourceable) != nullptr ||
           std::dynamic_pointer_cast<Values>(sourceable) != nullptr ||
           std::dynamic_pointer_cast<Union>(sourceable) != nullptr;
  }

  Source(std::shared_ptr<Sourceable> sourceable)
      : sourceable(sourceable), is_subquery(CheckIsSubquery(sourceable)), is_cte(false) {
    if (is_subquery) {
      throw std::runtime_error("Subquery must have an alias");
    }
  }

  Source(std::shared_ptr<Sourceable> sourceable, std::string alias, bool is_cte = false,
         const std::vector<std::string>& def_columns = {})
      : sourceable(sourceable),
        alias(std::make_shared<AliasStatement>(alias)),
        def_columns(def_columns),
        is_subquery(CheckIsSubquery(sourceable)),
        is_cte(is_cte) {}

  Source(std::shared_ptr<Sourceable> sourceable, std::shared_ptr<AliasStatement> alias, bool is_cte = false,
         const std::vector<std::string>& def_columns = {})
      : sourceable(sourceable),
        alias(alias),
        def_columns(def_columns),
        is_subquery(CheckIsSubquery(sourceable)),
        is_cte(is_cte) {}

  virtual std::ostream& Print(std::ostream& os) const override {
    if (is_cte) {
      os << alias.value()->Access();
      return os;
    }

    os << Definition();

    if (alias.has_value()) {
      os << " AS " << Declaration();
    }

    return os;
  }

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_source = dynamic_cast<const Source*>(&other);
    if (!other_source) return false;
    if (is_subquery != other_source->is_subquery || is_cte != other_source->is_cte) return false;
    if (def_columns != other_source->def_columns) return false;
    if ((alias.has_value() != other_source->alias.has_value())) return false;
    if (alias.has_value() && other_source->alias.has_value()) {
      if (*alias.value() != *other_source->alias.value()) return false;
    }
    return *sourceable == *other_source->sourceable;
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
      std::stringstream os;
      os << alias.value()->Declaration();
      if (!def_columns.empty()) {
        os << "(";
        for (size_t i = 0; i < def_columns.size(); i++) {
          os << def_columns[i];
          if (i < def_columns.size() - 1) {
            os << ", ";
          }
        }
        os << ")";
      }
      return os.str();
    } else {
      return Definition();
    }
  }
};

class Table : public Sourceable {
 public:
  std::string name;
  int arity;
  std::vector<std::string> attribute_names;

  Table(std::string name, int arity) : name(name), arity(arity) {}

  Table(std::string name, int arity, std::vector<std::string> attr_names)
      : name(name), arity(arity), attribute_names(std::move(attr_names)) {}

  std::ostream& Print(std::ostream& os) const override { return os << name; };

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_table = dynamic_cast<const Table*>(&other);
    if (!other_table) return false;
    return name == other_table->name && arity == other_table->arity && attribute_names == other_table->attribute_names;
  }

  bool HasNamedAttributes() const { return !attribute_names.empty(); }

  std::string GetAttributeName(int index) const {
    if (HasNamedAttributes() && index >= 0 && index < static_cast<int>(attribute_names.size())) {
      return attribute_names[index];
    } else {
      return "A" + std::to_string(index + 1);
    }
  }
};

class Selectable : public Expression {
 public:
  virtual ~Selectable() = default;
  virtual std::ostream& Print(std::ostream& os) const override = 0;
  virtual void Accept(ExpressionVisitor& visitor) override = 0;

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

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_wildcard = dynamic_cast<const Wildcard*>(&other);
    if (!other_wildcard) return false;
    if (source.has_value() != other_wildcard->source.has_value()) return false;
    if (source.has_value() && other_wildcard->source.has_value()) {
      return *source.value() == *other_wildcard->source.value();
    }
    return true;
  }

  std::string Alias() const override { return "*"; }

  bool HasAlias() const override { return false; }
};

class Term : public Expression {
 public:
  virtual ~Term() = default;
  virtual std::ostream& Print(std::ostream& os) const override = 0;
  virtual void Accept(ExpressionVisitor& visitor) override = 0;

  virtual std::string ToString() const = 0;
};

class TermSelectable : public Selectable {
 public:
  std::shared_ptr<Term> term;
  std::optional<std::string> alias;

  TermSelectable(std::shared_ptr<Term> term) : term(term) {}
  TermSelectable(std::shared_ptr<Term> term, std::string alias) : term(term), alias(alias) {}

  std::ostream& Print(std::ostream& os) const override { return os << *term; }

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_term_selectable = dynamic_cast<const TermSelectable*>(&other);
    if (!other_term_selectable) return false;
    if (alias != other_term_selectable->alias) return false;
    return *term == *other_term_selectable->term;
  }

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

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_constant = dynamic_cast<const Constant*>(&other);
    if (!other_constant) return false;
    return value == other_constant->value;
  }

  std::string ToString() const override {
    return std::visit(
        utl::overloaded{[](int arg) { return std::to_string(arg); }, [](double arg) { return std::to_string(arg); },
                        [](std::string arg) { return fmt::format("'{}'", arg); },
                        [](bool arg) { return arg ? std::string("TRUE") : std::string("FALSE"); }},
        value);
  }
};

class Operation : public Term {
 public:
  std::shared_ptr<Term> lhs;
  std::shared_ptr<Term> rhs;
  std::string op;

  Operation(std::shared_ptr<Term> lhs, std::shared_ptr<Term> rhs, std::string op) : lhs(lhs), rhs(rhs), op(op) {}

  std::ostream& Print(std::ostream& os) const override { return os << ToString(); }

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_operation = dynamic_cast<const Operation*>(&other);
    if (!other_operation) return false;
    return op == other_operation->op && *lhs == *other_operation->lhs && *rhs == *other_operation->rhs;
  }

  std::string ToString() const override {
    std::stringstream ss;
    ss << *lhs << " " << op << " " << *rhs;
    return ss.str();
  }
};

class ParenthesisTerm : public Term {
 public:
  std::shared_ptr<Term> term;

  ParenthesisTerm(std::shared_ptr<Term> term) : term(term) {}

  std::ostream& Print(std::ostream& os) const override { return os << ToString(); }

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_parenthesis_term = dynamic_cast<const ParenthesisTerm*>(&other);
    if (!other_parenthesis_term) return false;
    return *term == *other_parenthesis_term->term;
  }

  std::string ToString() const override {
    return "(" + term->ToString() + ")";
  }
};

class Function : public Term {
 public:
  AggregateFunction name;
  std::shared_ptr<Term> arg;

  Function(AggregateFunction name, std::shared_ptr<Term> arg) : name(name), arg(arg) {}

  std::ostream& Print(std::ostream& os) const override { return os << ToString(); }

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_function = dynamic_cast<const Function*>(&other);
    if (!other_function) return false;
    return name == other_function->name && *arg == *other_function->arg;
  }

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

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_column = dynamic_cast<const Column*>(&other);
    if (!other_column) return false;
    if (name != other_column->name) return false;
    if (source.has_value() != other_column->source.has_value()) return false;
    if (source.has_value() && other_column->source.has_value()) {
      return *source.value() == *other_column->source.value();
    }
    return true;
  }
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

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_values = dynamic_cast<const Values*>(&other);
    if (!other_values) return false;
    if (values.size() != other_values->values.size()) return false;
    for (size_t i = 0; i < values.size(); i++) {
      if (values[i].size() != other_values->values[i].size()) return false;
      for (size_t j = 0; j < values[i].size(); j++) {
        if (values[i][j] != other_values->values[i][j]) return false;
      }
    }
    return true;
  }
};

class Condition : public Expression {
 public:
  virtual ~Condition() = default;
  virtual std::ostream& Print(std::ostream& os) const override = 0;
  virtual void Accept(ExpressionVisitor& visitor) override = 0;

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

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_comp = dynamic_cast<const ComparisonCondition*>(&other);
    if (!other_comp) return false;
    return op == other_comp->op && *lhs == *other_comp->lhs && *rhs == *other_comp->rhs;
  }

  bool IsEmpty() const override { return false; }
};

/**
 * Represents logical operations (AND, OR, NOT) combining multiple conditions.
 */
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
    if (op == LogicalOp::NOT) {
      if (!conditions.empty()) {
        os << get_operator_string(op) << " " << *conditions.front();
      }
      return os;
    }

    for (size_t i = 0; i < conditions.size(); i++) {
      os << *conditions[i];
      if (i < conditions.size() - 1) {
        os << " " << get_operator_string(op) << " ";
      }
    }

    return os;
  }

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  LogicalCondition(std::vector<std::shared_ptr<Condition>> conditions, LogicalOp op) : conditions(conditions), op(op) {
    this->conditions.erase(std::remove_if(this->conditions.begin(), this->conditions.end(),
                                          [](const std::shared_ptr<Condition>& cond) { return cond->IsEmpty(); }),
                           this->conditions.end());
  }

  bool Equals(const Expression& other) const override {
    const auto* other_logical = dynamic_cast<const LogicalCondition*>(&other);
    if (!other_logical) return false;
    if (op != other_logical->op || conditions.size() != other_logical->conditions.size()) return false;
    for (size_t i = 0; i < conditions.size(); i++) {
      if (*conditions[i] != *other_logical->conditions[i]) return false;
    }
    return true;
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

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override;

  bool IsEmpty() const override { return false; }
};

class Exists : public Condition {
 public:
  std::shared_ptr<SelectStatement> select;

  Exists(std::shared_ptr<SelectStatement> select) : select(select) {}

  std::ostream& Print(std::ostream& os) const override;

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override;

  bool IsEmpty() const override { return false; }
};

class CaseWhen : public Term {
 public:
  std::vector<std::pair<std::shared_ptr<Condition>, std::shared_ptr<Term>>> cases;

  CaseWhen(std::vector<std::pair<std::shared_ptr<Condition>, std::shared_ptr<Term>>> cases) : cases(cases) {};

  std::ostream& Print(std::ostream& os) const override { return os << ToString(); }

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_case = dynamic_cast<const CaseWhen*>(&other);
    if (!other_case) return false;
    if (cases.size() != other_case->cases.size()) return false;
    for (size_t i = 0; i < cases.size(); i++) {
      if (*cases[i].first != *other_case->cases[i].first || *cases[i].second != *other_case->cases[i].second) {
        return false;
      }
    }
    return true;
  }

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

  FromStatement(std::shared_ptr<Source> source, std::shared_ptr<Condition> where) : sources({source}) {
    if (where && !where->IsEmpty()) {
      this->where = where;
    }
  }

  FromStatement(std::vector<std::shared_ptr<Source>> sources) : sources(sources) {}

  FromStatement(std::vector<std::shared_ptr<Source>> sources, std::shared_ptr<Condition> where) : sources(sources) {
    if (where && !where->IsEmpty()) {
      this->where = where;
    }
  }

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

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_from = dynamic_cast<const FromStatement*>(&other);
    if (!other_from) return false;
    if (sources.size() != other_from->sources.size()) return false;
    for (size_t i = 0; i < sources.size(); i++) {
      if (*sources[i] != *other_from->sources[i]) return false;
    }
    if (where.has_value() != other_from->where.has_value()) return false;
    if (where.has_value() && other_from->where.has_value()) {
      if (*where.value() != *other_from->where.value()) return false;
    }
    return true;
  }
};

class View : public Expression {
 public:
  std::shared_ptr<Source> source;

  View(std::shared_ptr<Source> source) : source(source) {}

  View(std::shared_ptr<Sourceable> sourceable, std::string alias)
      : source(std::make_shared<Source>(sourceable, alias)) {}

  std::ostream& Print(std::ostream& os) const override {
    return os << "CREATE OR REPLACE VIEW " << source->Declaration() << " AS " << source->Definition();
  }

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_view = dynamic_cast<const View*>(&other);
    if (!other_view) return false;
    return *source == *other_view->source;
  }
};

class GroupBy : public Expression {
 public:
  std::vector<std::shared_ptr<Selectable>> columns;

  GroupBy(std::vector<std::shared_ptr<Selectable>> columns) : columns(columns) {}

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

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_groupby = dynamic_cast<const GroupBy*>(&other);
    if (!other_groupby) return false;
    if (columns.size() != other_groupby->columns.size()) return false;
    for (size_t i = 0; i < columns.size(); i++) {
      if (*columns[i] != *other_groupby->columns[i]) return false;
    }
    return true;
  }
};

class SelectStatement : public Sourceable {
 public:
  std::vector<std::shared_ptr<Selectable>> columns;
  std::optional<std::shared_ptr<FromStatement>> from;
  std::vector<std::shared_ptr<Source>> ctes;
  std::optional<std::shared_ptr<GroupBy>> group_by;
  bool is_distinct = false;
  bool ctes_are_recursive = false;

  SelectStatement(const std::vector<std::shared_ptr<Selectable>>& columns, bool is_distinct = false)
      : columns(columns), is_distinct(is_distinct) {}

  SelectStatement(const std::vector<std::shared_ptr<Selectable>>& columns, std::shared_ptr<FromStatement> from,
                  bool is_distinct = false)
      : columns(columns), from(from), is_distinct(is_distinct) {}

  SelectStatement(const std::vector<std::shared_ptr<Selectable>>& columns, std::shared_ptr<FromStatement> from,
                  std::shared_ptr<GroupBy> group_by, bool is_distinct = false)
      : columns(columns), from(from), group_by(group_by), is_distinct(is_distinct) {}

  SelectStatement(const std::vector<std::shared_ptr<Selectable>>& columns, std::shared_ptr<FromStatement> from,
                  std::vector<std::shared_ptr<Source>> ctes, bool is_distinct = false,
                  bool ctes_are_recursive = false)
      : columns(columns),
        from(from),
        ctes(ctes),
        group_by(std::nullopt),
        is_distinct(is_distinct),
        ctes_are_recursive(ctes_are_recursive) {}

  std::ostream& Print(std::ostream& os) const override {
    if (!ctes.empty()) {
      os << "WITH ";
      if (ctes_are_recursive) {
        os << "RECURSIVE ";
      }
    }
    for (size_t i = 0; i < ctes.size(); i++) {
      os << ctes[i]->Declaration() << " AS " << ctes[i]->Definition();

      if (i < ctes.size() - 1) {
        os << ", ";
      } else {
        os << " ";
      }
    }
    os << "SELECT ";
    if (is_distinct) {
      os << "DISTINCT ";
    }
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

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_select = dynamic_cast<const SelectStatement*>(&other);
    if (!other_select) return false;
    if (is_distinct != other_select->is_distinct) return false;
    if (columns.size() != other_select->columns.size()) return false;
    for (size_t i = 0; i < columns.size(); i++) {
      if (*columns[i] != *other_select->columns[i]) return false;
    }
    if (from.has_value() != other_select->from.has_value()) return false;
    if (from.has_value() && other_select->from.has_value()) {
      if (*from.value() != *other_select->from.value()) return false;
    }
    if (ctes.size() != other_select->ctes.size()) return false;
    if (ctes_are_recursive != other_select->ctes_are_recursive) return false;
    for (size_t i = 0; i < ctes.size(); i++) {
      if (*ctes[i] != *other_select->ctes[i]) return false;
    }
    if (group_by.has_value() != other_select->group_by.has_value()) return false;
    if (group_by.has_value() && other_select->group_by.has_value()) {
      if (*group_by.value() != *other_select->group_by.value()) return false;
    }
    return true;
  }
};

class Union : public Sourceable {
 public:
  std::vector<std::shared_ptr<Sourceable>> members;

  Union(std::shared_ptr<Sourceable> lhs, std::shared_ptr<Sourceable> rhs) : members({lhs, rhs}) {}

  Union(std::vector<std::shared_ptr<Sourceable>> members) : members(members) {}

  std::ostream& Print(std::ostream& os) const override {
    for (size_t i = 0; i < members.size(); i++) {
      os << *members[i];
      if (i < members.size() - 1) {
        os << " UNION ";
      }
    }
    return os;
  }

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_union = dynamic_cast<const Union*>(&other);
    if (!other_union) return false;
    if (members.size() != other_union->members.size()) return false;
    for (size_t i = 0; i < members.size(); i++) {
      if (*members[i] != *other_union->members[i]) return false;
    }
    return true;
  }
};

class UnionAll : public Sourceable {
 public:
  std::vector<std::shared_ptr<SelectStatement>> members;

  UnionAll(std::shared_ptr<SelectStatement> lhs, std::shared_ptr<SelectStatement> rhs) : members({lhs, rhs}) {}

  UnionAll(std::vector<std::shared_ptr<SelectStatement>> members) : members(members) {}

  std::ostream& Print(std::ostream& os) const override {
    for (size_t i = 0; i < members.size(); i++) {
      os << *members[i];
      if (i < members.size() - 1) {
        os << " UNION ALL ";
      }
    }
    return os;
  }

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_union_all = dynamic_cast<const UnionAll*>(&other);
    if (!other_union_all) return false;
    if (members.size() != other_union_all->members.size()) return false;
    for (size_t i = 0; i < members.size(); i++) {
      if (*members[i] != *other_union_all->members[i]) return false;
    }
    return true;
  }
};

class CreateTable : public Expression {
 public:
  std::shared_ptr<Source> source;

  CreateTable(std::shared_ptr<Source> source) : source(source) {}

  CreateTable(std::shared_ptr<Sourceable> sourceable, std::string alias)
      : source(std::make_shared<Source>(sourceable, alias)) {}

  std::ostream& Print(std::ostream& os) const override {
    os << "CREATE TABLE " << source->Declaration() << " AS " << source->Definition();
    return os;
  }

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_create = dynamic_cast<const CreateTable*>(&other);
    if (!other_create) return false;
    return *source == *other_create->source;
  }
};

class MultipleStatements : public Expression {
 public:
  std::vector<std::shared_ptr<Expression>> statements;

  MultipleStatements(const std::vector<std::shared_ptr<Expression>>& statements) : statements(statements) {}

  std::ostream& Print(std::ostream& os) const override {
    for (size_t i = 0; i < statements.size(); i++) {
      os << *statements[i] << ";";
      if (i < statements.size() - 1) {
        os << std::endl << std::endl;
      }
    }
    return os;
  }

  void Accept(ExpressionVisitor& visitor) override { visitor.Visit(*this); }

  bool Equals(const Expression& other) const override {
    const auto* other_multiple = dynamic_cast<const MultipleStatements*>(&other);
    if (!other_multiple) return false;
    if (statements.size() != other_multiple->statements.size()) return false;
    for (size_t i = 0; i < statements.size(); i++) {
      if (*statements[i] != *other_multiple->statements[i]) return false;
    }
    return true;
  }
};

// UTILITY FUNCTIONS

using ParserRuleContext = antlr4::ParserRuleContext;

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // SQL_H

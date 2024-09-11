#ifndef SQL_AST_EXPR_VISITOR_H
#define SQL_AST_EXPR_VISITOR_H

namespace sql::ast {

// Forward declarations
class Expression;
class Sourceable;
class Selectable;
class Condition;
class Term;

class AliasStatement;
class Source;
class Table;
class Values;
class Wildcard;
class Column;
class Constant;
class Operation;
class Function;
class Aggregate;
class TermSelectable;
class ComparisonCondition;
class LogicalCondition;
class Inclusion;
class Exists;
class CaseWhen;
class FromStatement;
class GroupBy;
class SelectStatement;
class Union;
class UnionAll;
class CreateTable;
class View;
class MultipleStatements;

class ExpressionVisitor {
 public:
  virtual ~ExpressionVisitor() = default;

  // Base classes
  virtual void Visit(const Expression& expression) = 0;
  virtual void Visit(const Sourceable& sourceable) = 0;
  virtual void Visit(const Selectable& selectable) = 0;
  virtual void Visit(const Condition& condition) = 0;
  virtual void Visit(const Term& term) = 0;

  // Derived classes
  virtual void Visit(const AliasStatement& alias_statement) = 0;
  virtual void Visit(const Source& source) = 0;
  virtual void Visit(const Table& table) = 0;
  virtual void Visit(const Values& values) = 0;
  virtual void Visit(const Wildcard& wildcard) = 0;
  virtual void Visit(const Column& column) = 0;
  virtual void Visit(const Constant& constant) = 0;
  virtual void Visit(const Operation& operation) = 0;
  virtual void Visit(const Function& function) = 0;
  virtual void Visit(const Aggregate& aggregate) = 0;
  virtual void Visit(const TermSelectable& term_selectable) = 0;
  virtual void Visit(const ComparisonCondition& comparison_condition) = 0;
  virtual void Visit(const LogicalCondition& logical_condition) = 0;
  virtual void Visit(const Inclusion& inclusion) = 0;
  virtual void Visit(const Exists& exists) = 0;
  virtual void Visit(const CaseWhen& case_when) = 0;
  virtual void Visit(const FromStatement& from_statement) = 0;
  virtual void Visit(const GroupBy& group_by) = 0;
  virtual void Visit(const SelectStatement& select_statement) = 0;
  virtual void Visit(const Union& union_expr) = 0;
  virtual void Visit(const UnionAll& union_all_expr) = 0;
  virtual void Visit(const CreateTable& create_table) = 0;
  virtual void Visit(const View& view) = 0;
  virtual void Visit(const MultipleStatements& multiple_statements) = 0;
};

}  // namespace sql::ast

#endif  // SQL_AST_EXPR_VISITOR_H

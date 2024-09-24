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

  // Base class
  virtual void Visit(Expression& expression);
  virtual void Visit(Sourceable& sourceable);
  virtual void Visit(Selectable& selectable);
  virtual void Visit(Condition& condition);
  virtual void Visit(Term& term);

  // Derived classes
  virtual void Visit(AliasStatement& alias_statement);
  virtual void Visit(Source& source);
  virtual void Visit(Table& table);
  virtual void Visit(Values& values);
  virtual void Visit(Wildcard& wildcard);
  virtual void Visit(Column& column);
  virtual void Visit(Constant& constant);
  virtual void Visit(Operation& operation);
  virtual void Visit(Function& function);
  virtual void Visit(TermSelectable& term_selectable);
  virtual void Visit(ComparisonCondition& comparison_condition);
  virtual void Visit(LogicalCondition& logical_condition);
  virtual void Visit(Inclusion& inclusion);
  virtual void Visit(Exists& exists);
  virtual void Visit(CaseWhen& case_when);
  virtual void Visit(FromStatement& from_statement);
  virtual void Visit(GroupBy& group_by);
  virtual void Visit(SelectStatement& select_statement);
  virtual void Visit(Union& union_expr);
  virtual void Visit(UnionAll& union_all_expr);
  virtual void Visit(CreateTable& create_table);
  virtual void Visit(View& view);
  virtual void Visit(MultipleStatements& multiple_statements);
};

}  // namespace sql::ast

#endif  // SQL_AST_EXPR_VISITOR_H

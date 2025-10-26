#include "structs/expr_visitor.h"

#include "structs/sql_ast.h"

namespace rel2sql {

namespace sql::ast {

void ExpressionVisitor::Visit(Expression& expression) { expression.Accept(*this); }

void ExpressionVisitor::Visit(Sourceable& sourceable) { sourceable.Accept(*this); }

void ExpressionVisitor::Visit(Selectable& selectable) { selectable.Accept(*this); }

void ExpressionVisitor::Visit(Condition& condition) { condition.Accept(*this); }

void ExpressionVisitor::Visit(Term& term) { term.Accept(*this); }

//

void ExpressionVisitor::Visit(AliasStatement& alias_statement) {}

void ExpressionVisitor::Visit(Source& source) {
  Visit(*source.sourceable);
  if (source.alias) {
    Visit(*source.alias.value());
  }
}

void ExpressionVisitor::Visit(Table& table) {}

void ExpressionVisitor::Visit(Values& values) {
  for (auto& row : values.values) {
    for (auto& value : row) {
      Visit(value);
    }
  }
}

void ExpressionVisitor::Visit(Wildcard& wildcard) {}

void ExpressionVisitor::Visit(Column& column) {}

void ExpressionVisitor::Visit(Constant& constant) {}

void ExpressionVisitor::Visit(Operation& operation) {
  Visit(*operation.lhs);
  Visit(*operation.rhs);
}

void ExpressionVisitor::Visit(Function& function) { Visit(*function.arg); }

void ExpressionVisitor::Visit(TermSelectable& term_selectable) { Visit(*term_selectable.term); }

void ExpressionVisitor::Visit(ComparisonCondition& comparison_condition) {
  Visit(*comparison_condition.lhs);
  Visit(*comparison_condition.rhs);
}

void ExpressionVisitor::Visit(LogicalCondition& logical_condition) {
  for (auto& condition : logical_condition.conditions) {
    Visit(*condition);
  }
}

void ExpressionVisitor::Visit(Inclusion& inclusion) {
  for (auto& column : inclusion.columns) {
    Visit(*column);
  }

  Visit(*inclusion.select);
}

void ExpressionVisitor::Visit(Exists& exists) { Visit(*exists.select); }

void ExpressionVisitor::Visit(CaseWhen& case_when) {
  for (auto& [condition, term] : case_when.cases) {
    Visit(*condition);
    Visit(*term);
  }
}

void ExpressionVisitor::Visit(FromStatement& from_statement) {
  for (auto& source : from_statement.sources) {
    Visit(*source);
  }
  if (from_statement.where) {
    Visit(*from_statement.where.value());
  }
}

void ExpressionVisitor::Visit(GroupBy& group_by) {
  for (auto& column : group_by.columns) {
    Visit(*column);
  }
}

void ExpressionVisitor::Visit(SelectStatement& select_statement) {
  for (auto& column : select_statement.columns) {
    Visit(*column);
  }

  if (select_statement.from) {
    Visit(*select_statement.from.value());
  }

  for (auto& cte : select_statement.ctes) {
    Visit(*cte);
  }

  if (select_statement.group_by) {
    Visit(*select_statement.group_by.value());
  }
}

void ExpressionVisitor::Visit(Union& union_expr) {
  for (auto& select : union_expr.members) {
    Visit(*select);
  }
}

void ExpressionVisitor::Visit(UnionAll& union_all_expr) {
  for (auto& select : union_all_expr.members) {
    Visit(*select);
  }
}

void ExpressionVisitor::Visit(CreateTable& create_table) { Visit(*create_table.source); }

void ExpressionVisitor::Visit(View& view) { Visit(*view.source); }

void ExpressionVisitor::Visit(MultipleStatements& multiple_statements) {
  for (auto& statement : multiple_statements.statements) {
    Visit(*statement);
  }
}

}  // namespace sql::ast
}  // namespace rel2sql

#include "sql_ast/expr_visitor.h"

#include "sql_ast/sql_ast.h"

namespace rel2sql {

namespace sql::ast {

void ExpressionVisitor::Visit(Expression& expression) { expression.Accept(*this); }

void ExpressionVisitor::Visit(Sourceable& sourceable) { sourceable.Accept(*this); }

void ExpressionVisitor::Visit(Query& query) {
  for (auto& cte : query.ctes) {
    Visit(*cte);
  }
  query.Accept(*this);
}

void ExpressionVisitor::Visit(Selectable& selectable) { selectable.Accept(*this); }

void ExpressionVisitor::Visit(Condition& condition) { condition.Accept(*this); }

void ExpressionVisitor::Visit(Term& term) { term.Accept(*this); }

//

void ExpressionVisitor::Visit(Alias& _) {}

void ExpressionVisitor::Visit(Source& source) {
  Visit(*source.sourceable);
  if (source.alias) {
    Visit(*source.alias.value());
  }
}

void ExpressionVisitor::Visit(Table& _) {}

void ExpressionVisitor::Visit(Values& values) {
  for (auto& row : values.values) {
    for (auto& value : row) {
      Visit(value);
    }
  }
}

void ExpressionVisitor::Visit(Wildcard& _) {}

void ExpressionVisitor::Visit(Column& _) {}

void ExpressionVisitor::Visit(Constant& _) {}

void ExpressionVisitor::Visit(Operation& operation) {
  Visit(*operation.lhs);
  Visit(*operation.rhs);
}

void ExpressionVisitor::Visit(ParenthesisTerm& parenthesis_term) { Visit(*parenthesis_term.term); }

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

void ExpressionVisitor::Visit(From& from) {
  for (auto& source : from.sources) {
    Visit(*source);
  }
  if (from.where) {
    Visit(*from.where.value());
  }
}

void ExpressionVisitor::Visit(GroupBy& group_by) {
  for (auto& column : group_by.columns) {
    Visit(*column);
  }
}

void ExpressionVisitor::Visit(Select& select) {
  for (auto& column : select.columns) {
    Visit(*column);
  }

  if (select.from) {
    Visit(*select.from.value());
  }

  if (select.group_by) {
    Visit(*select.group_by.value());
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

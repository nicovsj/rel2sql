#include "rel_ast/rel_ast_visitor.h"

#include "rel_ast/rel_ast.h"

namespace rel2sql {

void RelASTVisitor::Visit(RelProgram& node) {
  for (auto& def : node.defs) {
    if (def) def->Accept(*this);
  }
}

void RelASTVisitor::Visit(RelDef& node) {
  if (node.body) node.body->Accept(*this);
}

void RelASTVisitor::Visit(RelAbstraction& node) {
  for (auto& expr : node.exprs) {
    if (expr) expr->Accept(*this);
  }
}

void RelASTVisitor::Visit(RelLiteral&) {}

void RelASTVisitor::Visit(RelLitExpr& node) {
  if (node.literal) node.literal->Accept(*this);
}

void RelASTVisitor::Visit(RelTermExpr& node) {
  if (node.term) node.term->Accept(*this);
}

void RelASTVisitor::Visit(RelProductExpr& node) {
  for (auto& expr : node.exprs) {
    if (expr) expr->Accept(*this);
  }
}

void RelASTVisitor::Visit(RelConditionExpr& node) {
  if (node.lhs) node.lhs->Accept(*this);
  if (node.rhs) node.rhs->Accept(*this);
}

void RelASTVisitor::Visit(RelAbstractionExpr& node) {
  if (node.rel_abs) node.rel_abs->Accept(*this);
}

void RelASTVisitor::Visit(RelFormulaExpr& node) {
  if (node.formula) node.formula->Accept(*this);
}

void RelASTVisitor::Visit(RelBindingsExpr& node) {
  if (node.expr) node.expr->Accept(*this);
}

void RelASTVisitor::Visit(RelBindingsFormula& node) {
  if (node.formula) node.formula->Accept(*this);
}

void RelASTVisitor::Visit(RelPartialAppl& node) {
  if (auto* abs_base = dynamic_cast<RelAbstractionApplBase*>(node.base.get())) {
    if (abs_base->rel_abs) abs_base->rel_abs->Accept(*this);
  }
  for (auto& param : node.params) {
    if (param && param->GetExpr()) param->GetExpr()->Accept(*this);
  }
}

void RelASTVisitor::Visit(RelFormulaBool&) {}

void RelASTVisitor::Visit(RelFullAppl& node) {
  if (auto* abs_base = dynamic_cast<RelAbstractionApplBase*>(node.base.get())) {
    if (abs_base->rel_abs) abs_base->rel_abs->Accept(*this);
  }
  for (auto& param : node.params) {
    if (param && param->GetExpr()) param->GetExpr()->Accept(*this);
  }
}

void RelASTVisitor::Visit(RelQuantification& node) {
  if (node.formula) node.formula->Accept(*this);
}

void RelASTVisitor::Visit(RelParen& node) {
  if (node.formula) node.formula->Accept(*this);
}

void RelASTVisitor::Visit(RelComparison& node) {
  if (node.lhs) node.lhs->Accept(*this);
  if (node.rhs) node.rhs->Accept(*this);
}

void RelASTVisitor::Visit(RelUnOp& node) {
  if (node.formula) node.formula->Accept(*this);
}

void RelASTVisitor::Visit(RelBinOp& node) {
  if (node.lhs) node.lhs->Accept(*this);
  if (node.rhs) node.rhs->Accept(*this);
}

void RelASTVisitor::Visit(RelIDTerm&) {}

void RelASTVisitor::Visit(RelNumTerm&) {}

void RelASTVisitor::Visit(RelOpTerm& node) {
  if (node.lhs) node.lhs->Accept(*this);
  if (node.rhs) node.rhs->Accept(*this);
}

void RelASTVisitor::Visit(RelParenthesisTerm& node) {
  if (node.term) node.term->Accept(*this);
}

}  // namespace rel2sql

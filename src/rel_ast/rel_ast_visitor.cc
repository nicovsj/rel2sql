#include "rel_ast/rel_ast_visitor.h"

#include "rel_ast/rel_ast.h"

namespace rel2sql {

// =============================================================================
// Abstract node visitors -> Call DispatchVisit on the node
// =============================================================================

std::shared_ptr<RelNode> BaseRelVisitor::Visit(const std::shared_ptr<RelNode>& node) { return node->DispatchVisit(*this, node); }

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelExpr>& node) {
  return std::dynamic_pointer_cast<RelExpr>(node->DispatchVisit(*this, node));
}

std::shared_ptr<RelFormula> BaseRelVisitor::Visit(const std::shared_ptr<RelFormula>& node) {
  return std::dynamic_pointer_cast<RelFormula>(node->DispatchVisit(*this, node));
}

std::shared_ptr<RelTerm> BaseRelVisitor::Visit(const std::shared_ptr<RelTerm>& node) {
  return std::dynamic_pointer_cast<RelTerm>(node->DispatchVisit(*this, node));
}

std::shared_ptr<RelApplParam> BaseRelVisitor::Visit(const std::shared_ptr<RelApplParam>& node) {
  return std::dynamic_pointer_cast<RelApplParam>(node->DispatchVisit(*this, node));
}

std::shared_ptr<RelApplBase> BaseRelVisitor::Visit(const std::shared_ptr<RelApplBase>& node) {
  return std::dynamic_pointer_cast<RelApplBase>(node->DispatchVisit(*this, node));
}

// =============================================================================
// Concrete node visitors -> Implement the logic for the node. For identity, return itself.
// =============================================================================

std::shared_ptr<RelApplParam> BaseRelVisitor::Visit(const std::shared_ptr<RelUnderscoreParam>& node) { return node; }

std::shared_ptr<RelApplParam> BaseRelVisitor::Visit(const std::shared_ptr<RelExprApplParam>& node) {
  if (node->expr) Visit(node->expr);
  return node;
}

std::shared_ptr<RelProgram> BaseRelVisitor::Visit(const std::shared_ptr<RelProgram>& node) {
  for (auto& def : node->defs) {
    if (def) def = Visit(def);
  }
  return node;
}

std::shared_ptr<RelDef> BaseRelVisitor::Visit(const std::shared_ptr<RelDef>& node) {
  if (node->body) node->body = Visit(node->body);
  return node;
}

std::shared_ptr<RelAbstraction> BaseRelVisitor::Visit(const std::shared_ptr<RelAbstraction>& node) {
  for (auto& expr : node->exprs) {
    if (expr) expr = Visit(expr);
  }
  return node;
}

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelLiteral>& node) { return node; }

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelTermExpr>& node) {
  if (node->term) node->term = Visit(node->term);
  return node;
}

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelProductExpr>& node) {
  for (auto& expr : node->exprs) {
    if (expr) expr = Visit(expr);
  }
  return node;
}

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelConditionExpr>& node) {
  if (node->lhs) node->lhs = Visit(node->lhs);
  if (node->rhs) node->rhs = Visit(node->rhs);
  return node;
}

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelAbstractionExpr>& node) {
  if (node->rel_abs) node->rel_abs = Visit(node->rel_abs);
  return node;
}

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelFormulaExpr>& node) {
  if (node->formula) node->formula = Visit(node->formula);
  return node;
}

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelBindingsExpr>& node) {
  if (node->expr) node->expr = Visit(node->expr);
  return node;
}

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelBindingsFormula>& node) {
  if (node->formula) node->formula = Visit(node->formula);
  return node;
}

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelPartialAppl>& node) {
  node->base = Visit(node->base);
  for (auto& param : node->params) {
    if (param) param = Visit(param);
  }
  return node;
}

std::shared_ptr<RelFormula> BaseRelVisitor::Visit(const std::shared_ptr<RelFormulaBool>& node) { return node; }

std::shared_ptr<RelFormula> BaseRelVisitor::Visit(const std::shared_ptr<RelFullAppl>& node) {
  node->base = Visit(node->base);
  for (auto& param : node->params) {
    if (param) param = Visit(param);
  }
  return node;
}

std::shared_ptr<RelFormula> BaseRelVisitor::Visit(const std::shared_ptr<RelExistential>& node) {
  if (node->formula) node->formula = Visit(node->formula);
  return node;
}

std::shared_ptr<RelFormula> BaseRelVisitor::Visit(const std::shared_ptr<RelUniversal>& node) {
  if (node->formula) node->formula = Visit(node->formula);
  return node;
}

std::shared_ptr<RelFormula> BaseRelVisitor::Visit(const std::shared_ptr<RelParen>& node) {
  if (node->formula) node->formula = Visit(node->formula);
  return node;
}

std::shared_ptr<RelFormula> BaseRelVisitor::Visit(const std::shared_ptr<RelComparison>& node) {
  if (node->lhs) node->lhs = Visit(node->lhs);
  if (node->rhs) node->rhs = Visit(node->rhs);
  return node;
}

std::shared_ptr<RelFormula> BaseRelVisitor::Visit(const std::shared_ptr<RelNegation>& node) {
  if (node->formula) node->formula = Visit(node->formula);
  return node;
}

std::shared_ptr<RelFormula> BaseRelVisitor::Visit(const std::shared_ptr<RelConjunction>& node) {
  if (node->lhs) node->lhs = Visit(node->lhs);
  if (node->rhs) node->rhs = Visit(node->rhs);
  return node;
}

std::shared_ptr<RelFormula> BaseRelVisitor::Visit(const std::shared_ptr<RelDisjunction>& node) {
  if (node->lhs) node->lhs = Visit(node->lhs);
  if (node->rhs) node->rhs = Visit(node->rhs);
  return node;
}

std::shared_ptr<RelTerm> BaseRelVisitor::Visit(const std::shared_ptr<RelIDTerm>& node) { return node; }

std::shared_ptr<RelTerm> BaseRelVisitor::Visit(const std::shared_ptr<RelNumTerm>& node) { return node; }

std::shared_ptr<RelTerm> BaseRelVisitor::Visit(const std::shared_ptr<RelOpTerm>& node) {
  if (node->lhs) node->lhs = Visit(node->lhs);
  if (node->rhs) node->rhs = Visit(node->rhs);
  return node;
}

std::shared_ptr<RelTerm> BaseRelVisitor::Visit(const std::shared_ptr<RelParenthesisTerm>& node) {
  if (node->term) node->term = Visit(node->term);
  return node;
}

std::shared_ptr<RelApplBase> BaseRelVisitor::Visit(const std::shared_ptr<RelIDApplBase>& node) { return node; }

std::shared_ptr<RelApplBase> BaseRelVisitor::Visit(const std::shared_ptr<RelAbstractionApplBase>& node) {
  if (node->rel_abs) node->rel_abs = Visit(node->rel_abs);
  return node;
}

}  // namespace rel2sql

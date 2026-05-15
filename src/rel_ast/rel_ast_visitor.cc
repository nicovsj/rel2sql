#include "rel_ast/rel_ast_visitor.h"

#include "rel_ast/rel_ast.h"

namespace rel2sql {

// =============================================================================
// Abstract node visitors -> Call DispatchVisit on the node
// =============================================================================

std::shared_ptr<RelNode> BaseRelVisitor::Visit(const std::shared_ptr<RelNode>& node) {
  return node->DispatchVisit(*this, node);
}

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

std::shared_ptr<RelApplParam> BaseRelVisitor::Visit(const std::shared_ptr<RelWildcardParam>& node) { return node; }

std::shared_ptr<RelApplParam> BaseRelVisitor::Visit(const std::shared_ptr<RelExprApplParam>& node) {
  if (node->expr) node->expr = Visit(node->expr);
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

std::shared_ptr<RelUnion> BaseRelVisitor::Visit(const std::shared_ptr<RelUnion>& node) {
  for (auto& expr : node->exprs) {
    if (expr) expr = Visit(expr);
  }
  return node;
}

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelLiteral>& node) { return node; }

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelProduct>& node) {
  for (auto& expr : node->exprs) {
    if (expr) expr = Visit(expr);
  }
  return node;
}

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelCondition>& node) {
  if (node->lhs) node->lhs = Visit(node->lhs);
  if (node->rhs) node->rhs = Visit(node->rhs);
  return node;
}

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelExprAbstraction>& node) {
  if (node->expr) node->expr = Visit(node->expr);
  return node;
}

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelFormulaAbstraction>& node) {
  if (node->formula) node->formula = Visit(node->formula);
  return node;
}

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelPartialApplication>& node) {
  node->base = Visit(node->base);
  for (auto& param : node->params) {
    if (param) param = Visit(param);
  }
  return node;
}

std::shared_ptr<RelFormula> BaseRelVisitor::Visit(const std::shared_ptr<RelBoolean>& node) { return node; }

std::shared_ptr<RelFormula> BaseRelVisitor::Visit(const std::shared_ptr<RelFullApplication>& node) {
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

std::shared_ptr<RelTerm> BaseRelVisitor::Visit(const std::shared_ptr<RelExprAsTerm>& node) {
  if (node->inner) node->inner = Visit(node->inner);
  return node;
}

std::shared_ptr<RelTerm> BaseRelVisitor::Visit(const std::shared_ptr<RelStringTerm>& node) { return node; }

std::shared_ptr<RelApplBase> BaseRelVisitor::Visit(const std::shared_ptr<RelIDApplBase>& node) { return node; }

std::shared_ptr<RelApplBase> BaseRelVisitor::Visit(const std::shared_ptr<RelExprApplBase>& node) {
  if (node->expr) node->expr = Visit(node->expr);
  return node;
}

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelBuiltinAggregateExpr>& node) {
  if (node->body) node->body = Visit(node->body);
  return node;
}

std::shared_ptr<RelFormula> BaseRelVisitor::Visit(const std::shared_ptr<RelBuiltinOrderExpr>& node) {
  if (node->body) node->body = Visit(node->body);
  return node;
}

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelBuiltinDateExpr>& node) {
  for (auto& a : node->args) {
    if (a) a = Visit(a);
  }
  return node;
}

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelTypedLiteralExpr>& node) { return node; }

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelBuiltinDecimalCastExpr>& node) {
  if (node->value) node->value = Visit(node->value);
  return node;
}

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelBuiltinCoalesceExpr>& node) {
  if (node->primary) node->primary = Visit(node->primary);
  if (node->fallback) node->fallback = Visit(node->fallback);
  return node;
}

std::shared_ptr<RelExpr> BaseRelVisitor::Visit(const std::shared_ptr<RelBuiltinSubstringExpr>& node) {
  if (node->str) node->str = Visit(node->str);
  if (node->start) node->start = Visit(node->start);
  if (node->len) node->len = Visit(node->len);
  return node;
}

std::shared_ptr<RelFormula> BaseRelVisitor::Visit(const std::shared_ptr<RelBuiltinLikeMatchFormula>& node) {
  if (node->value) node->value = Visit(node->value);
  return node;
}

}  // namespace rel2sql

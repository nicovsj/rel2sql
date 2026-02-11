#include "rewriter/base_rewriter.h"

namespace rel2sql {

void BaseRelRewriter::SetExprReplacement(std::shared_ptr<RelExpr> replacement) {
  expr_replacement_ = std::move(replacement);
}

std::shared_ptr<RelExpr> BaseRelRewriter::TakeExprReplacement() {
  std::shared_ptr<RelExpr> r = std::move(expr_replacement_);
  expr_replacement_.reset();
  return r;
}

void BaseRelRewriter::SetFormulaReplacement(std::shared_ptr<RelFormula> replacement) {
  formula_replacement_ = std::move(replacement);
}

std::shared_ptr<RelFormula> BaseRelRewriter::TakeFormulaReplacement() {
  std::shared_ptr<RelFormula> r = std::move(formula_replacement_);
  formula_replacement_.reset();
  return r;
}

void BaseRelRewriter::SetTermReplacement(std::shared_ptr<RelTerm> replacement) {
  term_replacement_ = std::move(replacement);
}

std::shared_ptr<RelTerm> BaseRelRewriter::TakeTermReplacement() {
  std::shared_ptr<RelTerm> r = std::move(term_replacement_);
  term_replacement_.reset();
  return r;
}

void BaseRelRewriter::SetAbstractionReplacement(std::shared_ptr<RelAbstraction> replacement) {
  abstraction_replacement_ = std::move(replacement);
}

std::shared_ptr<RelAbstraction> BaseRelRewriter::TakeAbstractionReplacement() {
  std::shared_ptr<RelAbstraction> r = std::move(abstraction_replacement_);
  abstraction_replacement_.reset();
  return r;
}

void BaseRelRewriter::RewriteExpr(std::shared_ptr<RelExpr>& expr) {
  if (!expr) return;
  expr->Accept(*this);
  if (auto r = TakeExprReplacement()) expr = r;
}

void BaseRelRewriter::RewriteFormula(std::shared_ptr<RelFormula>& formula) {
  if (!formula) return;
  formula->Accept(*this);
  if (auto r = TakeFormulaReplacement()) formula = r;
}

void BaseRelRewriter::RewriteTerm(std::shared_ptr<RelTerm>& term) {
  if (!term) return;
  term->Accept(*this);
  if (auto r = TakeTermReplacement()) term = r;
}

void BaseRelRewriter::RewriteAbstraction(std::shared_ptr<RelAbstraction>& abs) {
  if (!abs) return;
  abs->Accept(*this);
  if (auto r = TakeAbstractionReplacement()) abs = r;
}

void BaseRelRewriter::Visit(RelProgram& node) {
  for (auto& def : node.defs) {
    if (def) def->Accept(*this);
  }
}

void BaseRelRewriter::Visit(RelDef& node) {
  if (node.body) RewriteAbstraction(node.body);
}

void BaseRelRewriter::Visit(RelAbstraction& node) {
  for (auto& expr : node.exprs) {
    RewriteExpr(expr);
  }
}

void BaseRelRewriter::Visit(RelLiteral&) {}

void BaseRelRewriter::Visit(RelLitExpr& node) {
  if (node.literal) node.literal->Accept(*this);
}

void BaseRelRewriter::Visit(RelTermExpr& node) {
  RewriteTerm(node.term);
}

void BaseRelRewriter::Visit(RelProductExpr& node) {
  for (auto& expr : node.exprs) {
    RewriteExpr(expr);
  }
}

void BaseRelRewriter::Visit(RelConditionExpr& node) {
  RewriteExpr(node.lhs);
  RewriteFormula(node.rhs);
}

void BaseRelRewriter::Visit(RelAbstractionExpr& node) {
  RewriteAbstraction(node.rel_abs);
}

void BaseRelRewriter::Visit(RelFormulaExpr& node) {
  RewriteFormula(node.formula);
}

void BaseRelRewriter::Visit(RelBindingsExpr& node) {
  RewriteExpr(node.expr);
}

void BaseRelRewriter::Visit(RelBindingsFormula& node) {
  RewriteFormula(node.formula);
}

void BaseRelRewriter::Visit(RelPartialAppl& node) {
  if (auto* abs_base = dynamic_cast<RelAbstractionApplBase*>(node.base.get())) {
    if (abs_base->rel_abs) RewriteAbstraction(abs_base->rel_abs);
  }
  for (auto& param : node.params) {
    if (!param) continue;
    if (auto* ep = dynamic_cast<RelExprApplParam*>(param.get())) {
      RewriteExpr(ep->expr);
    }
  }
}

void BaseRelRewriter::Visit(RelFormulaBool&) {}

void BaseRelRewriter::Visit(RelFullAppl& node) {
  if (auto* abs_base = dynamic_cast<RelAbstractionApplBase*>(node.base.get())) {
    if (abs_base->rel_abs) RewriteAbstraction(abs_base->rel_abs);
  }
  for (auto& param : node.params) {
    if (!param) continue;
    if (auto* ep = dynamic_cast<RelExprApplParam*>(param.get())) {
      RewriteExpr(ep->expr);
    }
  }
}

void BaseRelRewriter::Visit(RelQuantification& node) {
  RewriteFormula(node.formula);
}

void BaseRelRewriter::Visit(RelParen& node) {
  RewriteFormula(node.formula);
}

void BaseRelRewriter::Visit(RelComparison& node) {
  RewriteTerm(node.lhs);
  RewriteTerm(node.rhs);
}

void BaseRelRewriter::Visit(RelUnOp& node) {
  RewriteFormula(node.formula);
}

void BaseRelRewriter::Visit(RelBinOp& node) {
  RewriteFormula(node.lhs);
  RewriteFormula(node.rhs);
}

void BaseRelRewriter::Visit(RelIDTerm&) {}

void BaseRelRewriter::Visit(RelNumTerm&) {}

void BaseRelRewriter::Visit(RelOpTerm& node) {
  RewriteTerm(node.lhs);
  RewriteTerm(node.rhs);
}

void BaseRelRewriter::Visit(RelParenthesisTerm& node) {
  RewriteTerm(node.term);
}

}  // namespace rel2sql

#include "rewriter/expression_as_term_rewriter.h"

#include <memory>
#include <sstream>
#include <vector>

#include "rel_ast/rel_ast.h"

namespace rel2sql {

namespace {

// Returns true if the expression is "simple" (variable, literal, number) and
// does not need to be wrapped in { (z) : z = expr }.
bool IsSimpleExpr(const std::shared_ptr<RelExpr>& expr) {
  if (auto* te = dynamic_cast<RelTermExpr*>(expr.get())) {
    return dynamic_cast<RelIDTerm*>(te->term.get()) != nullptr || dynamic_cast<RelNumTerm*>(te->term.get()) != nullptr;
  }
  if (dynamic_cast<RelLitExpr*>(expr.get())) return true;
  return false;
}

}  // namespace

std::string ExpressionAsTermRewriter::FreshVarName() { return std::format("_x{}", fresh_var_counter_++); }

std::shared_ptr<RelExpr> ExpressionAsTermRewriter::WrapTermExpr(std::shared_ptr<RelTermExpr> expr, bool wrap_in_abs) {
  std::string z = FreshVarName();
  auto bind = std::make_shared<RelVarBinding>(z, std::nullopt);
  auto lhs = std::make_shared<RelIDTerm>(z);
  auto formula = std::make_shared<RelComparison>(lhs, RelCompOp::EQ, expr->term);
  auto bindings_formula =
      std::make_shared<RelBindingsFormula>(std::vector<std::shared_ptr<RelBinding>>{bind}, std::move(formula));
  if (!wrap_in_abs) {
    return bindings_formula;
  }
  auto abs = std::make_shared<RelAbstraction>(std::vector<std::shared_ptr<RelExpr>>{std::move(bindings_formula)});
  return std::make_shared<RelAbstractionExpr>(std::move(abs));
}

std::shared_ptr<RelExpr> ExpressionAsTermRewriter::WrapConditionExpr(std::shared_ptr<RelConditionExpr> expr) {
  auto* te = dynamic_cast<RelTermExpr*>(expr->lhs.get());
  if (!te) return nullptr;
  std::string z = FreshVarName();
  auto bind = std::make_shared<RelVarBinding>(z, std::nullopt);
  auto lhs = std::make_shared<RelIDTerm>(z);
  auto eq_formula = std::make_shared<RelComparison>(lhs, RelCompOp::EQ, te->term);
  auto formula = std::make_shared<RelBinOp>(std::move(eq_formula), RelLogicalOp::AND, expr->rhs);
  auto bindings_formula =
      std::make_shared<RelBindingsFormula>(std::vector<std::shared_ptr<RelBinding>>{bind}, std::move(formula));
  auto abs = std::make_shared<RelAbstraction>(std::vector<std::shared_ptr<RelExpr>>{std::move(bindings_formula)});
  return std::make_shared<RelAbstractionExpr>(std::move(abs));
}

std::shared_ptr<RelExpr> ExpressionAsTermRewriter::WrapExpr(std::shared_ptr<RelExpr> expr, bool wrap_in_abs = false) {
  if (auto te = std::dynamic_pointer_cast<RelTermExpr>(expr)) {
    return WrapTermExpr(std::move(te), wrap_in_abs);
  }
  if (auto ce = std::dynamic_pointer_cast<RelConditionExpr>(expr)) {
    return WrapConditionExpr(std::move(ce));
  }
  return nullptr;
}

void ExpressionAsTermRewriter::Visit(RelConditionExpr& node) {
  BaseRelRewriter::Visit(node);
  // E where F: if E is non-simple, wrap as { (z): z = E and F }
  if (!IsSimpleExpr(node.lhs)) {
    auto ce = std::make_shared<RelConditionExpr>(node.lhs, node.rhs);
    if (auto wrapped = WrapConditionExpr(ce)) {
      SetExprReplacement(std::move(wrapped));
    }
  }
}

void ExpressionAsTermRewriter::Visit(RelBindingsExpr& node) {
  BaseRelRewriter::Visit(node);

  if (!IsSimpleExpr(node.expr)) {
    if (auto wrapped = WrapExpr(node.expr, /*wrap_in_abs=*/true)) {
      auto replacement = std::make_shared<RelBindingsExpr>(node.bindings, std::move(wrapped));
      SetExprReplacement(std::move(replacement));
    }
  }
}

void ExpressionAsTermRewriter::Visit(RelAbstraction& node) {
  BaseRelRewriter::Visit(node);

  std::vector<std::shared_ptr<RelExpr>> new_exprs;
  bool changed = false;
  for (auto& expr : node.exprs) {
    if (!IsSimpleExpr(expr)) {
      if (auto wrapped = WrapExpr(expr)) {
        new_exprs.push_back(std::move(wrapped));
        changed = true;
        continue;
      }
    }
    new_exprs.push_back(expr);
  }
  if (changed) {
    SetAbstractionReplacement(std::make_shared<RelAbstraction>(std::move(new_exprs)));
  }
}

void ExpressionAsTermRewriter::Visit(RelProductExpr& node) {
  BaseRelRewriter::Visit(node);

  std::vector<std::shared_ptr<RelExpr>> new_exprs;
  bool changed = false;
  for (auto& expr : node.exprs) {
    if (!IsSimpleExpr(expr)) {
      if (auto wrapped = WrapExpr(expr, /*wrap_in_abs=*/true)) {
        new_exprs.push_back(std::move(wrapped));
        changed = true;
        continue;
      }
    }
    new_exprs.push_back(expr);
  }
  if (changed) {
    SetExprReplacement(std::make_shared<RelProductExpr>(std::move(new_exprs)));
  }
}

}  // namespace rel2sql

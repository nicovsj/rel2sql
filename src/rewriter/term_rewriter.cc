#include "rewriter/term_rewriter.h"

#include <memory>
#include <vector>

#include "rel_ast/rel_ast.h"

namespace rel2sql {

namespace {

bool IsSimpleExpr(const std::shared_ptr<RelExpr>& expr) {
  if (auto* te = dynamic_cast<RelTermExpr*>(expr.get())) {
    return dynamic_cast<RelIDTerm*>(te->term.get()) != nullptr ||
           dynamic_cast<RelNumTerm*>(te->term.get()) != nullptr;
  }
  if (dynamic_cast<RelLiteral*>(expr.get())) return true;
  return false;
}

}  // namespace

std::string TermRewriter::FreshVarName() {
  return std::format("_x{}", fresh_var_counter_++);
}

std::shared_ptr<RelExpr> TermRewriter::WrapTermExpr(
    std::shared_ptr<RelTermExpr> expr, bool wrap_in_abs) {
  std::string z = FreshVarName();
  auto bind = std::make_shared<RelVarBinding>(z, std::nullopt);
  auto lhs = std::make_shared<RelIDTerm>(z);
  auto formula = std::make_shared<RelComparison>(lhs, RelCompOp::EQ, expr->term);
  auto bindings_formula =
      std::make_shared<RelBindingsFormula>(std::vector<std::shared_ptr<RelBinding>>{bind},
                                           std::move(formula));
  if (!wrap_in_abs) {
    return bindings_formula;
  }
  auto abs =
      std::make_shared<RelAbstraction>(std::vector<std::shared_ptr<RelExpr>>{std::move(bindings_formula)});
  return std::make_shared<RelAbstractionExpr>(std::move(abs));
}

std::shared_ptr<RelExpr> TermRewriter::WrapConditionExpr(
    std::shared_ptr<RelConditionExpr> expr) {
  auto* te = dynamic_cast<RelTermExpr*>(expr->lhs.get());
  if (!te) return nullptr;
  std::string z = FreshVarName();
  auto bind = std::make_shared<RelVarBinding>(z, std::nullopt);
  auto lhs = std::make_shared<RelIDTerm>(z);
  auto eq_formula = std::make_shared<RelComparison>(lhs, RelCompOp::EQ, te->term);
  auto formula =
      std::make_shared<RelConjunction>(std::move(eq_formula), expr->rhs);
  auto bindings_formula =
      std::make_shared<RelBindingsFormula>(std::vector<std::shared_ptr<RelBinding>>{bind},
                                           std::move(formula));
  auto abs =
      std::make_shared<RelAbstraction>(std::vector<std::shared_ptr<RelExpr>>{std::move(bindings_formula)});
  return std::make_shared<RelAbstractionExpr>(std::move(abs));
}

std::shared_ptr<RelExpr> TermRewriter::WrapExpr(std::shared_ptr<RelExpr> expr,
                                                            bool wrap_in_abs) {
  if (auto te = std::dynamic_pointer_cast<RelTermExpr>(expr)) {
    return WrapTermExpr(std::move(te), wrap_in_abs);
  }
  if (auto ce = std::dynamic_pointer_cast<RelConditionExpr>(expr)) {
    return WrapConditionExpr(std::move(ce));
  }
  return nullptr;
}

std::shared_ptr<RelExpr> TermRewriter::Visit(const std::shared_ptr<RelConditionExpr>& node) {
  auto result = std::dynamic_pointer_cast<RelConditionExpr>(BaseRelVisitor::Visit(node));
  if (!result) return result;

  if (!IsSimpleExpr(result->lhs)) {
    if (auto wrapped = WrapConditionExpr(result)) {
      return wrapped;
    }
  }
  return result;
}

std::shared_ptr<RelExpr> TermRewriter::Visit(const std::shared_ptr<RelBindingsExpr>& node) {
  auto result = std::dynamic_pointer_cast<RelBindingsExpr>(BaseRelVisitor::Visit(node));
  if (!result) return result;

  if (!IsSimpleExpr(result->expr)) {
    if (auto wrapped = WrapExpr(result->expr, /*wrap_in_abs=*/true)) {
      return std::make_shared<RelBindingsExpr>(result->bindings, std::move(wrapped));
    }
  }
  return result;
}

std::shared_ptr<RelAbstraction> TermRewriter::Visit(
    const std::shared_ptr<RelAbstraction>& node) {
  auto result = std::dynamic_pointer_cast<RelAbstraction>(BaseRelVisitor::Visit(node));
  if (!result) return result;

  std::vector<std::shared_ptr<RelExpr>> new_exprs;
  bool changed = false;
  for (auto& expr : result->exprs) {
    if (!IsSimpleExpr(expr)) {
      if (auto wrapped = WrapExpr(expr, false)) {
        new_exprs.push_back(std::move(wrapped));
        changed = true;
        continue;
      }
    }
    new_exprs.push_back(expr);
  }
  if (changed) {
    return std::make_shared<RelAbstraction>(std::move(new_exprs));
  }
  return result;
}

std::shared_ptr<RelExpr> TermRewriter::Visit(const std::shared_ptr<RelProductExpr>& node) {
  auto result = std::dynamic_pointer_cast<RelProductExpr>(BaseRelVisitor::Visit(node));
  if (!result) return result;

  std::vector<std::shared_ptr<RelExpr>> new_exprs;
  bool changed = false;
  for (auto& expr : result->exprs) {
    if (!IsSimpleExpr(expr)) {
      if (auto wrapped = WrapExpr(expr, true)) {
        new_exprs.push_back(std::move(wrapped));
        changed = true;
        continue;
      }
    }
    new_exprs.push_back(expr);
  }
  if (changed) {
    return std::make_shared<RelProductExpr>(std::move(new_exprs));
  }
  return result;
}

}  // namespace rel2sql

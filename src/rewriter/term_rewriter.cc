#include "rewriter/term_rewriter.h"

#include <memory>
#include <vector>

#include "rel_ast/rel_ast.h"

namespace rel2sql {

namespace {

bool IsSimpleExpr(const std::shared_ptr<RelExpr>& expr) {
  if (auto* term = dynamic_cast<RelTerm*>(expr.get())) {
    return dynamic_cast<RelIDTerm*>(term) != nullptr || dynamic_cast<RelNumTerm*>(term) != nullptr;
  }
  if (dynamic_cast<RelLiteral*>(expr.get())) return true;
  return false;
}

}  // namespace

std::string TermRewriter::FreshVarName() { return std::format("_x{}", fresh_var_counter_++); }

std::shared_ptr<RelExpr> TermRewriter::WrapTermExpr(std::shared_ptr<RelTerm> term, bool wrap_in_abs) {
  std::string z = FreshVarName();
  auto bind = std::make_shared<RelVarBinding>(z, std::nullopt);
  auto lhs = std::make_shared<RelIDTerm>(z);
  auto formula = std::make_shared<RelComparison>(lhs, RelCompOp::EQ, term);
  auto bindings_formula =
      std::make_shared<RelFormulaAbstraction>(std::vector<std::shared_ptr<RelBinding>>{bind}, std::move(formula));
  if (!wrap_in_abs) {
    return bindings_formula;
  }
  return std::make_shared<RelUnion>(std::vector<std::shared_ptr<RelExpr>>{std::move(bindings_formula)});
}

std::shared_ptr<RelExpr> TermRewriter::WrapConditionExpr(std::shared_ptr<RelCondition> expr) {
  auto term = std::dynamic_pointer_cast<RelTerm>(expr->lhs);
  if (!term) return nullptr;
  std::string z = FreshVarName();
  auto bind = std::make_shared<RelVarBinding>(z, std::nullopt);
  auto lhs = std::make_shared<RelIDTerm>(z);
  auto eq_formula = std::make_shared<RelComparison>(lhs, RelCompOp::EQ, term);
  auto formula = std::make_shared<RelConjunction>(std::move(eq_formula), expr->rhs);
  auto bindings_formula =
      std::make_shared<RelFormulaAbstraction>(std::vector<std::shared_ptr<RelBinding>>{bind}, std::move(formula));
  return std::make_shared<RelUnion>(std::vector<std::shared_ptr<RelExpr>>{std::move(bindings_formula)});
}

std::shared_ptr<RelExpr> TermRewriter::WrapExpr(std::shared_ptr<RelExpr> expr, bool wrap_in_abs) {
  if (auto term = std::dynamic_pointer_cast<RelTerm>(expr)) {
    return WrapTermExpr(std::move(term), wrap_in_abs);
  }
  if (auto ce = std::dynamic_pointer_cast<RelCondition>(expr)) {
    return WrapConditionExpr(std::move(ce));
  }
  return nullptr;
}

std::shared_ptr<RelExpr> TermRewriter::Visit(const std::shared_ptr<RelCondition>& node) {
  auto result = std::dynamic_pointer_cast<RelCondition>(BaseRelVisitor::Visit(node));
  if (!result) return result;

  if (!IsSimpleExpr(result->lhs)) {
    if (auto wrapped = WrapConditionExpr(result)) {
      return wrapped;
    }
  }
  return result;
}

std::shared_ptr<RelExpr> TermRewriter::Visit(const std::shared_ptr<RelExprAbstraction>& node) {
  auto result = std::dynamic_pointer_cast<RelExprAbstraction>(BaseRelVisitor::Visit(node));
  if (!result) return result;

  if (!IsSimpleExpr(result->expr)) {
    if (auto wrapped = WrapExpr(result->expr, /*wrap_in_abs=*/true)) {
      return std::make_shared<RelExprAbstraction>(result->bindings, std::move(wrapped));
    }
  }
  return result;
}

std::shared_ptr<RelUnion> TermRewriter::Visit(const std::shared_ptr<RelUnion>& node) {
  auto result = std::dynamic_pointer_cast<RelUnion>(BaseRelVisitor::Visit(node));
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
    return std::make_shared<RelUnion>(std::move(new_exprs));
  }
  return result;
}

std::shared_ptr<RelExpr> TermRewriter::Visit(const std::shared_ptr<RelProduct>& node) {
  auto result = std::dynamic_pointer_cast<RelProduct>(BaseRelVisitor::Visit(node));
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
    return std::make_shared<RelProduct>(std::move(new_exprs));
  }
  return result;
}

}  // namespace rel2sql

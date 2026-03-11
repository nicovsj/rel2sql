#include "rewriter/term_rewriter.h"

#include <memory>
#include <vector>

#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_context_builder.h"

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

std::shared_ptr<RelApplParam> TermRewriter::MakeVarParam(const std::string& var) {
  auto id_term = std::make_shared<RelIDTerm>(var);
  return std::make_shared<RelExprApplParam>(std::move(id_term));
}

int TermRewriter::GetRelationArity(const std::string& id) const {
  if (!container_) return 0;
  return container_->GetArity(id);
}

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

std::shared_ptr<RelFormula> TermRewriter::Visit(const std::shared_ptr<RelFullApplication>& node) {
  auto result = std::dynamic_pointer_cast<RelFullApplication>(BaseRelVisitor::Visit(node));
  if (!result) return result;

  std::vector<int> expr_positions;
  std::vector<std::shared_ptr<RelTerm>> expr_terms;
  for (size_t i = 0; i < result->params.size(); i++) {
    auto* param = dynamic_cast<RelExprApplParam*>(result->params[i].get());
    if (!param || !param->expr) continue;
    auto term = std::dynamic_pointer_cast<RelTerm>(param->expr);
    if (term && !IsSimpleExpr(term)) {
      expr_positions.push_back(static_cast<int>(i));
      expr_terms.push_back(std::move(term));
    }
  }
  if (expr_positions.empty()) return result;

  std::vector<std::string> fresh_vars;
  for (size_t i = 0; i < expr_positions.size(); i++) {
    fresh_vars.push_back(FreshVarName());
  }

  std::vector<std::shared_ptr<RelApplParam>> new_params;
  int ev_idx = 0;
  for (size_t i = 0; i < result->params.size(); i++) {
    if (ev_idx < static_cast<int>(expr_positions.size()) &&
        static_cast<int>(i) == expr_positions[ev_idx]) {
      new_params.push_back(MakeVarParam(fresh_vars[ev_idx++]));
    } else {
      new_params.push_back(result->params[i]);
    }
  }

  std::shared_ptr<RelFormula> formula =
      std::make_shared<RelFullApplication>(result->base, std::move(new_params));
  for (size_t i = 0; i < fresh_vars.size(); i++) {
    auto lhs = std::make_shared<RelIDTerm>(fresh_vars[i]);
    auto eq = std::make_shared<RelComparison>(lhs, RelCompOp::EQ, expr_terms[i]);
    formula = std::make_shared<RelConjunction>(std::move(formula), std::move(eq));
  }

  std::vector<std::shared_ptr<RelBinding>> bindings;
  for (const auto& v : fresh_vars) {
    bindings.push_back(std::make_shared<RelVarBinding>(v, std::nullopt));
  }
  return std::make_shared<RelExistential>(std::move(bindings), std::move(formula));
}

std::shared_ptr<RelExpr> TermRewriter::Visit(const std::shared_ptr<RelPartialApplication>& node) {
  auto result = std::dynamic_pointer_cast<RelPartialApplication>(BaseRelVisitor::Visit(node));
  if (!result) return result;

  int expr_pos = -1;
  std::shared_ptr<RelTerm> expr_term;
  for (size_t i = 0; i < result->params.size(); i++) {
    auto* param = dynamic_cast<RelExprApplParam*>(result->params[i].get());
    if (!param || !param->expr) continue;
    auto term = std::dynamic_pointer_cast<RelTerm>(param->expr);
    if (term && !IsSimpleExpr(term)) {
      if (expr_pos >= 0) return result;
      expr_pos = static_cast<int>(i);
      expr_term = std::move(term);
    }
  }
  if (expr_pos < 0 || !expr_term) return result;

  auto* id_base = dynamic_cast<RelIDApplBase*>(result->base.get());
  if (!id_base) return result;

  int rel_arity = GetRelationArity(id_base->id);
  if (rel_arity <= 0 || static_cast<size_t>(rel_arity) <= result->params.size()) {
    return result;
  }

  std::string z = FreshVarName();
  std::vector<std::string> rest_vars;
  size_t rest_count = static_cast<size_t>(rel_arity) - result->params.size();
  for (size_t i = 0; i < rest_count; i++) {
    rest_vars.push_back(FreshVarName());
  }

  std::vector<std::shared_ptr<RelApplParam>> full_params;
  for (size_t i = 0; i < result->params.size(); i++) {
    if (static_cast<int>(i) == expr_pos) {
      full_params.push_back(MakeVarParam(z));
    } else {
      full_params.push_back(result->params[i]);
    }
  }
  for (const auto& v : rest_vars) {
    full_params.push_back(MakeVarParam(v));
  }

  auto full_appl = std::make_shared<RelFullApplication>(result->base, std::move(full_params));
  auto lhs = std::make_shared<RelIDTerm>(z);
  auto eq = std::make_shared<RelComparison>(lhs, RelCompOp::EQ, std::move(expr_term));
  auto exists_formula =
      std::make_shared<RelConjunction>(std::move(full_appl), std::move(eq));
  std::vector<std::shared_ptr<RelBinding>> exist_bindings;
  exist_bindings.push_back(std::make_shared<RelVarBinding>(z, std::nullopt));
  auto exists = std::make_shared<RelExistential>(std::move(exist_bindings), std::move(exists_formula));

  std::vector<std::shared_ptr<RelBinding>> output_bindings;
  for (const auto& v : rest_vars) {
    output_bindings.push_back(std::make_shared<RelVarBinding>(v, std::nullopt));
  }
  return std::make_shared<RelFormulaAbstraction>(std::move(output_bindings), std::move(exists));
}

}  // namespace rel2sql

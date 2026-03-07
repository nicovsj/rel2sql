#include "rewriter/binding_rewriter.h"

#include <memory>

#include "rel_ast/rel_ast.h"

namespace rel2sql {

namespace {

std::shared_ptr<RelFullAppl> MakeAtomFormula(const std::string& relation_id, const std::string& var_id) {
  auto base = std::make_shared<RelIDApplBase>(relation_id);
  auto x_term = std::make_shared<RelIDTerm>(var_id);
  auto param = std::make_shared<RelExprApplParam>(x_term);
  return std::make_shared<RelFullAppl>(base, std::vector<std::shared_ptr<RelApplParam>>{param});
}

}  // namespace

std::shared_ptr<RelExpr> BindingRewriter::Visit(const std::shared_ptr<RelBindingsFormula>& node) {
  auto new_formula = Visit(node->formula);

  std::vector<std::pair<std::string, std::string>> domain_bindings;

  for (const auto& b : node->bindings) {
    auto vb = std::dynamic_pointer_cast<RelVarBinding>(b);
    if (vb && vb->domain.has_value()) {
      domain_bindings.emplace_back(vb->id, *vb->domain);
    }
  }

  if (domain_bindings.empty()) return std::make_shared<RelBindingsFormula>(node->bindings, new_formula);

  std::vector<std::shared_ptr<RelBinding>> new_bindings;
  new_bindings.reserve(node->bindings.size());
  for (const auto& b : node->bindings) {
    auto vb = std::dynamic_pointer_cast<RelVarBinding>(b);
    if (vb && vb->domain.has_value()) {
      new_bindings.push_back(std::make_shared<RelVarBinding>(vb->id, std::nullopt));
    } else {
      new_bindings.push_back(b);
    }
  }


  for (const auto& [var_id, rel_id] : domain_bindings) {
    new_formula = std::make_shared<RelConjunction>(new_formula, MakeAtomFormula(rel_id, var_id));
  }

  return std::make_shared<RelBindingsFormula>(std::move(new_bindings), std::move(new_formula));
}

std::shared_ptr<RelExpr> BindingRewriter::Visit(const std::shared_ptr<RelBindingsExpr>& node) {
  auto new_expr = Visit(node->expr);

  std::vector<std::pair<std::string, std::string>> domain_bindings;
  for (const auto& b : node->bindings) {
    auto* vb = dynamic_cast<RelVarBinding*>(b.get());
    if (vb && vb->domain.has_value()) {
      domain_bindings.emplace_back(vb->id, *vb->domain);
    }
  }
  if (domain_bindings.empty()) return std::make_shared<RelBindingsExpr>(node->bindings, new_expr);

  std::vector<std::shared_ptr<RelBinding>> new_bindings;
  new_bindings.reserve(node->bindings.size());
  for (const auto& b : node->bindings) {
    auto* vb = dynamic_cast<RelVarBinding*>(b.get());
    if (vb && vb->domain.has_value()) {
      new_bindings.push_back(std::make_shared<RelVarBinding>(vb->id, std::nullopt));
    } else {
      new_bindings.push_back(b);
    }
  }

  std::shared_ptr<RelFormula> condition_formula = MakeAtomFormula(domain_bindings[0].second, domain_bindings[0].first);
  for (size_t i = 1; i < domain_bindings.size(); ++i) {
    const auto& [var_id, rel_id] = domain_bindings[i];
    condition_formula =
        std::make_shared<RelConjunction>(condition_formula, MakeAtomFormula(rel_id, var_id));
  }
  std::shared_ptr<RelExpr> wrapped_expr = std::make_shared<RelConditionExpr>(new_expr, std::move(condition_formula));

  return std::make_shared<RelBindingsExpr>(std::move(new_bindings), std::move(wrapped_expr));
}

}  // namespace rel2sql

#include "rewriter/binding_domain_rewriter.h"

#include <memory>
#include <vector>

#include "rel_ast/rel_ast.h"

namespace rel2sql {

namespace {

// Build formula A(x): full application of relation A to variable x.
std::shared_ptr<RelFullAppl> MakeAtomFormula(const std::string& relation_id, const std::string& var_id) {
  auto base = std::make_shared<RelIDApplBase>(relation_id);
  auto x_term = std::make_shared<RelIDTerm>(var_id);
  auto x_expr = std::make_shared<RelTermExpr>(x_term);
  auto param = std::make_shared<RelExprApplParam>(x_expr);
  return std::make_shared<RelFullAppl>(base, std::vector<std::shared_ptr<RelApplParam>>{param});
}

}  // namespace

void BindingDomainRewriter::Visit(RelBindingsFormula& node) {
  BaseRelRewriter::Visit(node);

  std::vector<std::pair<std::string, std::string>> domain_bindings;
  for (const auto& b : node.bindings) {
    auto vb = std::dynamic_pointer_cast<RelVarBinding>(b);
    if (vb && vb->domain.has_value()) {
      domain_bindings.emplace_back(vb->id, *vb->domain);
    }
  }

  if (domain_bindings.empty()) return;

  std::vector<std::shared_ptr<RelBinding>> new_bindings;
  new_bindings.reserve(node.bindings.size());
  for (const auto& b : node.bindings) {
    auto vb = std::dynamic_pointer_cast<RelVarBinding>(b);
    if (vb && vb->domain.has_value()) {
      new_bindings.push_back(std::make_shared<RelVarBinding>(vb->id, std::nullopt));
    } else {
      new_bindings.push_back(b);
    }
  }

  std::shared_ptr<RelFormula> new_formula = node.formula;
  for (const auto& [var_id, rel_id] : domain_bindings) {
    new_formula = std::make_shared<RelBinOp>(new_formula, RelLogicalOp::AND, MakeAtomFormula(rel_id, var_id));
  }

  auto replacement = std::make_shared<RelBindingsFormula>(std::move(new_bindings), std::move(new_formula));
  SetExprReplacement(std::move(replacement));
}

void BindingDomainRewriter::Visit(RelBindingsExpr& node) {
  BaseRelRewriter::Visit(node);

  std::vector<std::pair<std::string, std::string>> domain_bindings;
  for (const auto& b : node.bindings) {
    auto* vb = dynamic_cast<RelVarBinding*>(b.get());
    if (vb && vb->domain.has_value()) {
      domain_bindings.emplace_back(vb->id, *vb->domain);
    }
  }
  if (domain_bindings.empty()) return;

  std::vector<std::shared_ptr<RelBinding>> new_bindings;
  new_bindings.reserve(node.bindings.size());
  for (const auto& b : node.bindings) {
    auto* vb = dynamic_cast<RelVarBinding*>(b.get());
    if (vb && vb->domain.has_value()) {
      new_bindings.push_back(std::make_shared<RelVarBinding>(vb->id, std::nullopt));
    } else {
      new_bindings.push_back(b);
    }
  }

  // E where A(x) and B(y): condition expression (E): (A(x) and B(y) and ...)
  std::shared_ptr<RelFormula> condition_formula =
      MakeAtomFormula(domain_bindings[0].second, domain_bindings[0].first);
  for (size_t i = 1; i < domain_bindings.size(); ++i) {
    const auto& [var_id, rel_id] = domain_bindings[i];
    condition_formula = std::make_shared<RelBinOp>(condition_formula, RelLogicalOp::AND,
                                                   MakeAtomFormula(rel_id, var_id));
  }
  std::shared_ptr<RelExpr> new_expr =
      std::make_shared<RelConditionExpr>(node.expr, std::move(condition_formula));

  auto replacement = std::make_shared<RelBindingsExpr>(std::move(new_bindings), std::move(new_expr));
  SetExprReplacement(std::move(replacement));
}

}  // namespace rel2sql

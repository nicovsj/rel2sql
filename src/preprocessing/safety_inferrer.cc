#include "preprocessing/safety_inferrer.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <variant>

#include "rel_ast/bound_set.h"
#include "rel_ast/domain.h"
#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_ast_visitor.h"
#include "sql/aggregate_map.h"
#include "support/exceptions.h"

namespace rel2sql {

namespace {

// Returns LHS_coeffs - RHS_coeffs, or nullopt if either term lacks valid linear coeffs.
std::optional<LinearTermCoeffs> NetLinearCoeffs(RelTerm* lhs, RelTerm* rhs) {
  if (!lhs || !rhs || !lhs->term_linear_coeffs || !rhs->term_linear_coeffs || lhs->IsInvalidTermExpression() ||
      rhs->IsInvalidTermExpression()) {
    return std::nullopt;
  }
  LinearTermCoeffs net;
  net.constant = lhs->term_linear_coeffs->constant - rhs->term_linear_coeffs->constant;
  for (const auto& [var, c] : lhs->term_linear_coeffs->var_coeffs) {
    net.var_coeffs[var] = c;
  }
  for (const auto& [var, c] : rhs->term_linear_coeffs->var_coeffs) {
    net.var_coeffs[var] -= c;
  }
  return net;
}

// Returns the domain for var projected to its single column, or nullopt if no bound contains var.
// Skips bounds with non-trivial affine coefficients (initial implementation).
// For single-variable ConstantDomain bounds, returns domain->Clone() directly.
std::optional<std::unique_ptr<Domain>> GetProjectedDomainForVariable(const BoundSet& safety, const std::string& var) {
  for (const auto& bound : safety.bounds) {
    if (bound.HasNonTrivialAffine()) continue;
    auto it = std::find(bound.variables.begin(), bound.variables.end(), var);
    if (it == bound.variables.end()) continue;
    size_t i = static_cast<size_t>(it - bound.variables.begin());
    if (bound.variables.size() == 1) {
      return bound.domain->Clone();
    }
    return std::make_unique<Projection>(std::vector<size_t>{i}, bound.domain->Clone());
  }
  return std::nullopt;
}

sql::ast::constant_t DoubleToConstant(double v) {
  if (std::floor(v) == v && v >= std::numeric_limits<int>::min() && v <= std::numeric_limits<int>::max()) {
    return static_cast<int>(v);
  }
  return v;
}

// Returns bounds from parent_safety that mention at least one free variable of child.
BoundSet InheritBounds(const BoundSet& parent_safety, const RelNode& child) {
  BoundSet inherited;
  for (const auto& bound : parent_safety.bounds) {
    for (const auto& var : bound.variables) {
      if (child.free_variables.count(var)) {
        inherited.Insert(bound);
        break;
      }
    }
  }
  return inherited;
}

void UnionSafetyIntoSubtree(RelNode* root, const BoundSet& extra) {
  if (!root || extra.IsEmpty()) return;
  root->safety = root->safety.UnionWith(extra);
  for (const auto& child : root->Children()) {
    UnionSafetyIntoSubtree(child.get(), extra);
  }
}

bool ConstantDomainIsZero(const Domain& domain) {
  const auto* cd = dynamic_cast<const ConstantDomain*>(&domain);
  if (!cd) return false;
  return std::visit(
      [](const auto& v) -> bool {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, int>) {
          return v == 0;
        } else if constexpr (std::is_same_v<T, double>) {
          return v == 0.0;
        }
        return false;
      },
      cd->value);
}

bool TermContainsId(const RelTerm* term) {
  if (!term) return false;
  if (dynamic_cast<const RelIDTerm*>(term) != nullptr) return true;
  if (const auto* op = dynamic_cast<const RelOpTerm*>(term)) {
    return TermContainsId(op->lhs.get()) || TermContainsId(op->rhs.get());
  }
  if (const auto* par = dynamic_cast<const RelParenthesisTerm*>(term)) {
    return TermContainsId(par->term.get());
  }
  return false;
}

// Divisor must be a compile-time constant expression (no column variables).
bool DivisorIsSafe(const RelTerm* divisor) { return divisor != nullptr && !TermContainsId(divisor); }

// Build a domain for a term from already-bounded variables (ADD/SUB/MUL/DIV on domains).
// Fails for non-compositional terms or division by a variable / zero.
std::optional<std::unique_ptr<Domain>> TermToDomain(const RelTerm* term, const BoundSet& safety) {
  if (!term) return std::nullopt;

  if (const auto* id = dynamic_cast<const RelIDTerm*>(term)) {
    if (id->variables.size() != 1) return std::nullopt;
    return GetProjectedDomainForVariable(safety, *id->variables.begin());
  }

  if (const auto* num = dynamic_cast<const RelNumTerm*>(term)) {
    auto maybe_value = std::visit(
        [](const auto& v) -> std::optional<double> {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, int>) {
            return static_cast<double>(v);
          } else if constexpr (std::is_same_v<T, double>) {
            return v;
          }
          return std::nullopt;
        },
        num->value);
    if (!maybe_value) return std::nullopt;
    return std::make_unique<ConstantDomain>(DoubleToConstant(*maybe_value));
  }

  if (const auto* par = dynamic_cast<const RelParenthesisTerm*>(term)) {
    return TermToDomain(par->term.get(), safety);
  }

  const auto* op = dynamic_cast<const RelOpTerm*>(term);
  if (!op || !op->lhs || !op->rhs) return std::nullopt;

  auto lhs_dom = TermToDomain(op->lhs.get(), safety);
  if (!lhs_dom) return std::nullopt;

  switch (op->op) {
    case RelTermOp::ADD:
    case RelTermOp::SUB:
    case RelTermOp::MUL: {
      auto rhs_dom = TermToDomain(op->rhs.get(), safety);
      if (!rhs_dom) return std::nullopt;
      return std::make_unique<DomainOperation>(std::move(*lhs_dom), std::move(*rhs_dom), op->op);
    }
    case RelTermOp::DIV: {
      if (!DivisorIsSafe(op->rhs.get())) return std::nullopt;
      auto rhs_dom = TermToDomain(op->rhs.get(), safety);
      if (!rhs_dom || ConstantDomainIsZero(**rhs_dom)) return std::nullopt;
      return std::make_unique<DomainOperation>(std::move(*lhs_dom), std::move(*rhs_dom), RelTermOp::DIV);
    }
  }
  return std::nullopt;
}

std::set<std::string> UnboundVariablesInComparison(const RelComparison& node) {
  std::set<std::string> unbound;
  const auto& bound_vars = node.safety.bound_variables;
  for (const auto& v : node.variables) {
    if (!bound_vars.count(v)) unbound.insert(v);
  }
  return unbound;
}

void InsertInferredBound(RelComparison* node, const std::string& var, std::unique_ptr<Domain> domain) {
  std::unordered_set<Bound> new_bounds;
  new_bounds.insert(Bound({var}, std::move(domain)));
  node->safety = node->safety.UnionWith(BoundSet(std::move(new_bounds)));
}

// Affine equality inference (NetLinearCoeffs). Returns a domain for the single unbound variable.
std::optional<std::unique_ptr<Domain>> InferDomainFromAffineEquality(RelComparison* node) {
  auto net_opt = NetLinearCoeffs(node->lhs.get(), node->rhs.get());
  if (!net_opt) return std::nullopt;

  const auto& net = *net_opt;
  std::set<std::string> unbound_vars = UnboundVariablesInComparison(*node);
  if (unbound_vars.size() != 1) return std::nullopt;

  const std::string xj = *unbound_vars.begin();
  auto coeff_it = net.var_coeffs.find(xj);
  if (coeff_it == net.var_coeffs.end()) return std::nullopt;

  const double coeff_xj = coeff_it->second;
  if (coeff_xj == 0.0) return std::nullopt;

  std::unique_ptr<Domain> result;

  std::vector<std::pair<std::string, double>> other_vars;
  for (const auto& [v, coeff] : net.var_coeffs) {
    if (v == xj || coeff == 0.0) continue;
    other_vars.emplace_back(v, coeff);
  }

  if (other_vars.size() == 1) {
    const auto& [v, coeff_v] = other_vars[0];
    auto D_v = GetProjectedDomainForVariable(node->safety, v);
    if (D_v && std::abs(coeff_v) > 1e-12) {
      const double k = -net.constant / coeff_v;
      const double m = -coeff_xj / coeff_v;
      if (std::abs(m) > 1e-12) {
        std::unique_ptr<Domain> numerator;
        if (std::abs(k) < 1e-12) {
          numerator = std::move(*D_v);
        } else {
          numerator = std::make_unique<DomainOperation>(
              std::move(*D_v), std::make_unique<ConstantDomain>(DoubleToConstant(k)), RelTermOp::SUB);
        }
        if (std::abs(m - 1.0) < 1e-12) {
          result = std::move(numerator);
        } else {
          result = std::make_unique<DomainOperation>(
              std::move(numerator), std::make_unique<ConstantDomain>(DoubleToConstant(m)), RelTermOp::DIV);
        }
      }
    }
  }

  if (!result) {
    const double constant_term = -net.constant / coeff_xj;
    if (constant_term != 0.0) {
      result = std::make_unique<ConstantDomain>(DoubleToConstant(constant_term));
    }
    for (const auto& [v, coeff] : net.var_coeffs) {
      if (v == xj) continue;
      auto D_v = GetProjectedDomainForVariable(node->safety, v);
      if (!D_v) return std::nullopt;
      double scale = -coeff / coeff_xj;
      if (scale == 0.0) continue;
      std::unique_ptr<Domain> term_v =
          (scale == 1.0)
              ? std::move(*D_v)
              : std::make_unique<DomainOperation>(
                    std::move(*D_v), std::make_unique<ConstantDomain>(DoubleToConstant(scale)), RelTermOp::MUL);
      if (!result) {
        result = std::move(term_v);
      } else {
        result = std::make_unique<DomainOperation>(std::move(result), std::move(term_v), RelTermOp::ADD);
      }
    }
  }

  if (!result) {
    result = std::make_unique<ConstantDomain>(DoubleToConstant(0));
  }
  return result;
}

// Compositional equality: xj = T where xj is a bare ID and T uses only bounded variables.
std::optional<std::pair<std::string, std::unique_ptr<Domain>>> InferDomainFromCompositionalEquality(
    RelComparison* node) {
  std::set<std::string> unbound_vars = UnboundVariablesInComparison(*node);
  if (unbound_vars.size() != 1) return std::nullopt;

  const std::string xj = *unbound_vars.begin();
  const RelTerm* expr = nullptr;

  if (auto* lhs_id = dynamic_cast<RelIDTerm*>(node->lhs.get())) {
    if (lhs_id->id == xj) {
      expr = node->rhs.get();
    }
  } else if (auto* rhs_id = dynamic_cast<RelIDTerm*>(node->rhs.get())) {
    if (rhs_id->id == xj) {
      expr = node->lhs.get();
    }
  }
  if (!expr) return std::nullopt;

  for (const auto& v : expr->variables) {
    if (!node->safety.bound_variables.count(v)) return std::nullopt;
  }

  auto domain = TermToDomain(expr, node->safety);
  if (!domain) return std::nullopt;
  return std::make_pair(xj, std::move(*domain));
}

// Visitor that computes safety from children only (no recursion).
// Used when nodes are processed in post-order.
class SafetyComputeVisitor : public BaseRelVisitor {
 public:
  using BaseRelVisitor::Visit;
  explicit SafetyComputeVisitor(RelContextBuilder* container) : container_(container) {}

  std::shared_ptr<RelProgram> Visit(const std::shared_ptr<RelProgram>& node) override {
    // RelProgram does not compute safety from children; it just contains defs.
    return node;
  }

  std::shared_ptr<RelDef> Visit(const std::shared_ptr<RelDef>& node) override {
    // RelDef does not compute safety from children.
    return node;
  }

  std::shared_ptr<RelUnion> Visit(const std::shared_ptr<RelUnion>& node) override {
    if (node->exprs.empty()) return node;
    node->safety = node->exprs[0]->safety;
    for (size_t i = 1; i < node->exprs.size(); ++i) {
      node->safety = node->safety.MergeWith(node->exprs[i]->safety);
    }
    return node;
  }

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelProduct>& node) override {
    node->safety = BoundSet();
    for (auto& expr : node->exprs) {
      if (expr) {
        node->safety = node->safety.UnionWith(expr->safety);
      }
    }
    return node;
  }

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelCondition>& node) override {
    if (node->lhs && node->rhs) {
      if (!node->lhs->safety.IsEmpty()) {
        UnionSafetyIntoSubtree(node->rhs.get(), node->lhs->safety);
      }
      node->safety = node->rhs->safety.UnionWith(node->lhs->safety);
    }
    return node;
  }

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelExprAbstraction>& node) override {
    if (node->expr) ComputeBindingsSafety(*node, *node->expr, node->bindings);
    return node;
  }

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelFormulaAbstraction>& node) override {
    if (!node->formula) return node;
    BoundSet inner_body_for_join;
    if (auto* ex = dynamic_cast<RelExistential*>(node->formula.get())) {
      if (ex->formula) inner_body_for_join = ex->formula->safety;
    }
    ComputeBindingsSafety(*node, *node->formula, node->bindings);
    if (!inner_body_for_join.IsEmpty()) {
      node->safety = node->safety.UnionWith(inner_body_for_join);
    }
    return node;
  }

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelPartialApplication>& node) override {
    if (auto* id_base = dynamic_cast<RelIDApplBase*>(node->base.get())) {
      std::string id = id_base->id;
      if (GetAggregateMap().find(id) != GetAggregateMap().end()) {
        if (!node->params.empty()) {
          auto expr = node->params[0]->GetExpr();
          if (expr) {
            node->safety = expr->safety;
          }
        }
        return node;
      }
      ComputeIDApplicationSafety(*node, node->params, id);
    } else if (auto* abs_base = dynamic_cast<RelExprApplBase*>(node->base.get())) {
      if (abs_base->expr) ComputeRelAbsApplicationSafety(node, abs_base->expr, node->params);
    }
    return node;
  }

  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelFullApplication>& node) override {
    if (auto* id_base = dynamic_cast<RelIDApplBase*>(node->base.get())) {
      ComputeIDApplicationSafety(*node, node->params, id_base->id);
    } else if (auto* abs_base = dynamic_cast<RelExprApplBase*>(node->base.get())) {
      if (abs_base->expr) ComputeRelAbsApplicationSafety(node, abs_base->expr, node->params);
    }
    return node;
  }

  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelConjunction>& node) override {
    if (node->lhs && node->rhs) {
      node->safety = node->lhs->safety.UnionWith(node->rhs->safety);
    }
    return node;
  }

  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelDisjunction>& node) override {
    if (node->lhs && node->rhs) {
      auto has_non_trivial_affine = [](const BoundSet& s) {
        for (const auto& bound : s.bounds) {
          for (const auto& coeff_opt : bound.coeffs) {
            if (!coeff_opt) continue;
            auto [a, b] = *coeff_opt;
            if (a != 1.0 || b != 0.0) return true;
          }
        }
        return false;
      };
      if (has_non_trivial_affine(node->lhs->safety) || has_non_trivial_affine(node->rhs->safety)) {
        throw NotImplementedException(
            "Safety analysis for disjunctions with linear term parameters (a*x + b) "
            "is not supported yet when (a,b) != (1,0).",
            SourceLocation(0, 0));
      }
      if (node->use_union_branch_safety) {
        node->safety = node->lhs->safety.UnionWith(node->rhs->safety);
      } else {
        node->safety = node->lhs->safety.MergeWith(node->rhs->safety);
      }
    }
    return node;
  }

  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelNegation>& node) override {
    // Negation formula is not safe, so we do nothing with the safety set.
    return node;
  }

  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelExistential>& node) override {
    if (node->formula) ComputeBindingsSafety(*node, *node->formula, node->bindings);
    return node;
  }

  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelUniversal>& node) override {
    if (node->formula) ComputeBindingsSafety(*node, *node->formula, node->bindings);
    return node;
  }

  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelParen>& node) override {
    if (node->formula) {
      node->safety = node->formula->safety;
    }
    return node;
  }

  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelComparison>& node) override {
    // Propagate child safety for all comparators. (EQ-only domain inference below still needs
    // `bound_variables` / bounds from lhs ∪ rhs, and non-EQ comparisons must inherit sibling
    // bounds from e.g. `TermRewriter` existentials: `{e}(_x0) ∧ _x0 ⋄ t`.)
    if (node->lhs && node->rhs) {
      node->safety = node->lhs->safety.UnionWith(node->rhs->safety);
    }

    if (node->op != RelCompOp::EQ) return node;

    // 1) Affine inference (NetLinearCoeffs).
    if (auto affine_domain = InferDomainFromAffineEquality(node.get())) {
      std::set<std::string> unbound_vars = UnboundVariablesInComparison(*node);
      InsertInferredBound(node.get(), *unbound_vars.begin(), std::move(*affine_domain));
      return node;
    }

    // 2) Compositional inference: z = f(bounded vars) with f built from +,-,*,/ (constant divisor only).
    if (auto comp = InferDomainFromCompositionalEquality(node.get())) {
      InsertInferredBound(node.get(), comp->first, std::move(comp->second));
    }

    return node;
  }

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelBuiltinAggregateExpr>& node) override {
    if (node->body) {
      node->safety = node->body->safety;
    }
    return node;
  }

  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelBuiltinOrderExpr>& node) override {
    if (node->body) {
      node->safety = node->body->safety;
    }
    return node;
  }

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelBuiltinDateExpr>& node) override {
    node->safety = BoundSet();
    for (auto& a : node->args) {
      if (a) node->safety = node->safety.UnionWith(a->safety);
    }
    return node;
  }

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelTypedLiteralExpr>& node) override {
    node->safety = BoundSet();
    return node;
  }

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelBuiltinDecimalCastExpr>& node) override {
    if (node->value) {
      node->safety = node->value->safety;
    } else {
      node->safety = BoundSet();
    }
    return node;
  }

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelBuiltinCoalesceExpr>& node) override {
    node->safety = BoundSet();
    if (node->primary) node->safety = node->primary->safety;
    if (node->fallback) node->safety = node->safety.UnionWith(node->fallback->safety);
    return node;
  }

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelBuiltinSubstringExpr>& node) override {
    node->safety = BoundSet();
    if (node->str) node->safety = node->str->safety;
    if (node->start) node->safety = node->safety.UnionWith(node->start->safety);
    if (node->len) node->safety = node->safety.UnionWith(node->len->safety);
    return node;
  }

  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelBuiltinLikeMatchFormula>& node) override {
    if (node->value) {
      node->safety = node->value->safety;
    } else {
      node->safety = BoundSet();
    }
    return node;
  }

  std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelExprAsTerm>&) override {
    throw std::logic_error("SafetyInferrer: RelExprAsTerm leaked past TermRewriter");
  }

  std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelStringTerm>& node) override {
    node->safety = BoundSet();
    return node;
  }

 private:
  void ComputeBindingsSafety(RelNode& current, RelNode& child,
                             const std::vector<std::shared_ptr<RelBinding>>& bindings) {
    std::vector<std::string> variables;
    for (const auto& b : bindings) {
      if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) {
        variables.push_back(vb->id);
      }
    }
    current.safety = child.safety.WithRemovedVariables(variables);
  }

  void ComputeIDApplicationSafety(RelNode& node, const std::vector<std::shared_ptr<RelApplParam>>& params,
                                  const std::string& id) {
    std::vector<std::string> variable_names;
    std::vector<size_t> variable_indices;
    std::vector<std::optional<std::pair<double, double>>> coeffs;

    for (size_t i = 0; i < params.size(); ++i) {
      auto expr = params[i] ? params[i]->GetExpr() : nullptr;
      if (!expr) continue;
      auto term = std::dynamic_pointer_cast<RelTerm>(expr);
      if (!term) continue;
      if (expr->variables.size() != 1) continue;

      std::string variable = *expr->variables.begin();
      variable_indices.push_back(i);
      variable_names.push_back(variable);

      if (term->term_linear_coeffs && !term->IsInvalidTermExpression()) {
        coeffs.push_back(term->GetSingleVarCoeffs());
      } else {
        coeffs.emplace_back(std::nullopt);
      }
    }

    int arity = container_->GetArity(id);
    std::unique_ptr<Domain> domain = std::make_unique<DefinedDomain>(id, arity);
    if (variable_indices.size() != static_cast<size_t>(arity)) {
      domain = std::make_unique<Projection>(variable_indices, std::move(domain));
    }

    auto bound = Bound(std::move(variable_names), std::move(domain));
    bound.coeffs = std::move(coeffs);

    node.safety = BoundSet({std::move(bound)});
  }

  void ComputeRelAbsApplicationSafety(const std::shared_ptr<RelExpr>& application_node,
                                      const std::shared_ptr<RelExpr>& expr,
                                      const std::vector<std::shared_ptr<RelApplParam>>& params) {
    std::vector<std::string> variable_names;
    std::vector<size_t> variable_indices;
    std::vector<std::optional<std::pair<double, double>>> coeffs;

    for (size_t i = 0; i < params.size(); ++i) {
      auto expr = params[i] ? params[i]->GetExpr() : nullptr;
      if (!expr) continue;
      auto term = std::dynamic_pointer_cast<RelTerm>(expr);
      if (!term || expr->variables.size() != 1) continue;

      variable_names.push_back(*expr->variables.begin());
      variable_indices.push_back(i);

      if (term->term_linear_coeffs && !term->IsInvalidTermExpression()) {
        coeffs.push_back(term->GetSingleVarCoeffs());
      } else {
        coeffs.emplace_back(std::nullopt);
      }
    }

    auto promised_source = std::make_unique<IntensionalDomain>(expr);
    auto projection = std::make_unique<Projection>(variable_indices, std::move(promised_source));
    auto bound = Bound(std::move(variable_names), std::move(projection));
    bound.coeffs = std::move(coeffs);

    application_node->safety = BoundSet({bound});
  }

  RelContextBuilder* container_;
};

}  // namespace

SafetyInferrer::SafetyInferrer(RelContextBuilder* container) : container_(container) {}

void SafetyInferrer::Run(std::shared_ptr<RelNode> root) {
  std::vector<std::shared_ptr<RelNode>> nodes = CollectNodesPostOrder(root);
  RunSafetyFixpoint(nodes);
  ApplyRecursiveCallSafetyInheritance(root);
  for (const auto& node : nodes) {
    ComputeSafetyFromChildren(node);
  }
  for (size_t i = nodes.size(); i-- > 0;) {
    InheritSafetyToChildren(nodes[i].get());
  }
}

void SafetyInferrer::RunSafetyFixpoint(const std::vector<std::shared_ptr<RelNode>>& nodes) {
  const size_t n = nodes.size();
  while (true) {
    std::unordered_map<RelNode*, std::set<std::string>> old_bound_vars;
    for (const auto& node : nodes) {
      old_bound_vars[node.get()] = node->safety.bound_variables;
    }

    for (const auto& node : nodes) {
      ComputeSafetyFromChildren(node);
    }

    for (size_t i = n; i-- > 0;) {
      InheritSafetyToChildren(nodes[i].get());
    }

    bool changed = false;
    for (const auto& node : nodes) {
      if (node->safety.bound_variables != old_bound_vars[node.get()]) {
        changed = true;
        break;
      }
    }
    if (!changed) break;
  }
}

void SafetyInferrer::ApplyRecursiveCallSafetyInheritance(std::shared_ptr<RelNode> root) {
  auto prog = std::dynamic_pointer_cast<RelProgram>(root);
  if (!prog) return;

  for (const auto& def : prog->defs) {
    if (!def || !def->body || def->body->exprs.empty()) continue;
    auto bf = std::dynamic_pointer_cast<RelFormulaAbstraction>(def->body->exprs[0]);
    if (!bf || !bf->is_recursive || bf->recursive_definition_name != def->name) continue;

    auto meta = container_->GetRecursionMetadata(def->name);
    if (!meta) continue;

    BoundSet base_safety;
    bool have_base = false;
    for (const auto& nu : meta->non_recursive_disjuncts) {
      if (!nu || nu->exprs.empty()) continue;
      auto gf = std::dynamic_pointer_cast<RelFormula>(nu->exprs[0]);
      if (!gf) continue;
      if (!have_base) {
        base_safety = gf->safety;
        have_base = true;
      } else {
        base_safety = base_safety.MergeWith(gf->safety);
      }
    }

    std::vector<std::string> head_vars;
    for (const auto& b : bf->bindings) {
      if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) {
        head_vars.push_back(vb->id);
      }
    }
    const int arity = static_cast<int>(head_vars.size());

    for (const auto& br : meta->recursive_disjuncts) {
      if (!have_base) break;
      auto call = std::dynamic_pointer_cast<RelFullApplication>(br.recursive_call);
      if (!call) continue;
      auto* idb = dynamic_cast<RelIDApplBase*>(call->base.get());
      if (!idb || idb->id != def->name) continue;
      if (arity <= 0 || static_cast<int>(call->params.size()) != arity) continue;

      std::unordered_map<std::string, std::string> rename;
      bool ok = true;
      for (int i = 0; i < arity; ++i) {
        auto expr = call->params[static_cast<size_t>(i)] ? call->params[static_cast<size_t>(i)]->GetExpr() : nullptr;
        auto* term = dynamic_cast<RelIDTerm*>(expr.get());
        if (!term || !container_->IsVar(term->id)) {
          ok = false;
          break;
        }
        rename[head_vars[static_cast<size_t>(i)]] = term->id;
      }
      if (!ok) continue;

      call->safety = call->safety.UnionWith(base_safety.Renamed(rename));
    }
  }
}

void SafetyInferrer::ComputeSafetyFromChildren(const std::shared_ptr<RelNode>& node) {
  SafetyComputeVisitor compute_visitor(container_);
  compute_visitor.Visit(node);
}

void SafetyInferrer::InheritSafetyToChildren(RelNode* node) {
  for (const auto& child : node->Children()) {
    if (!child) continue;
    auto inherited = InheritBounds(node->safety, *child);
    child->safety = child->safety.UnionWith(inherited);
  }
}

std::vector<std::shared_ptr<RelNode>> SafetyInferrer::CollectNodesPostOrder(std::shared_ptr<RelNode> root) {
  std::vector<std::shared_ptr<RelNode>> result;

  std::function<void(std::shared_ptr<RelNode>)> collect = [&result, &collect](std::shared_ptr<RelNode> node) {
    if (!node) return;
    for (const auto& child : node->Children()) {
      collect(child);
    }
    result.push_back(node);
  };

  collect(root);
  return result;
}

}  // namespace rel2sql

#include "preprocessing/safety_inferrer.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>

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
    if (bound.variables.size() == 1 && dynamic_cast<const ConstantDomain*>(bound.domain.get())) {
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
      node->safety = node->rhs->safety.UnionWith(node->lhs->safety);
    }
    return node;
  }

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelExprAbstraction>& node) override {
    if (node->expr) ComputeBindingsSafety(*node, *node->expr, node->bindings);
    return node;
  }

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelFormulaAbstraction>& node) override {
    if (node->formula) ComputeBindingsSafety(*node, *node->formula, node->bindings);
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
      node->safety = node->lhs->safety.MergeWith(node->rhs->safety);
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
    // If not an equality comparison, there's no possible inference to be made.
    if (node->op != RelCompOp::EQ) return node;

    // If the comparison is t1 = t2, then let's get the net linear coeffs of t1 - t2 (if possible).
    auto net_opt = NetLinearCoeffs(node->lhs.get(), node->rhs.get());

    if (!net_opt) return node;

    const auto& net = *net_opt;
    const auto& all_vars = node->variables;
    const auto& bound_vars = node->safety.bound_variables;

    std::set<std::string> unbound_vars;
    for (const auto& v : all_vars) {
      if (!bound_vars.count(v)) unbound_vars.insert(v);
    }

    // If there is two or more unbound variables, there's no possible inference to be made.
    if (unbound_vars.size() != 1) return node;

    const std::string xj = *unbound_vars.begin();
    auto it = net.var_coeffs.find(xj);

    assert(it != net.var_coeffs.end());

    const auto& [_, coeff_xj] = *it;

    // If the coefficient for the unbound variable is 0, there's no possible inference to be made.
    if (coeff_xj == 0.0) return node;

    std::unique_ptr<Domain> result;
    const double constant_term = -net.constant / coeff_xj;

    // Build domain tree from projected domains
    // Omit ConstantDomain(0) when constant is 0; omit MUL when scale is 1
    if (constant_term != 0.0) {
      result = std::make_unique<ConstantDomain>(DoubleToConstant(constant_term));
    }
    for (const auto& [v, coeff] : net.var_coeffs) {
      if (v == xj) continue;
      auto D_v = GetProjectedDomainForVariable(node->safety, v);
      if (!D_v) return node;
      double scale = -coeff / coeff_xj;
      if (scale == 0.0) continue;  // term contributes nothing
      std::unique_ptr<Domain> term_v =
          (scale == 1.0) ? std::move(*D_v)
                         : std::make_unique<DomainOperation>(
                               std::move(*D_v),
                               std::make_unique<ConstantDomain>(DoubleToConstant(scale)),
                               RelTermOp::MUL);
      if (!result) {
        result = std::move(term_v);
      } else {
        result = std::make_unique<DomainOperation>(std::move(result), std::move(term_v),
                                                  RelTermOp::ADD);
      }
    }
    if (!result) {
      result = std::make_unique<ConstantDomain>(DoubleToConstant(0));  // xj = 0
    }

    std::unordered_set<Bound> new_bounds;
    new_bounds.insert(Bound({xj}, std::move(result)));
    node->safety = node->safety.UnionWith(BoundSet(std::move(new_bounds)));
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
  const size_t n = nodes.size();

  while (true) {
    std::unordered_map<RelNode*, std::set<std::string>> old_bound_vars;
    for (const auto& node : nodes) {
      old_bound_vars[node.get()] = node->safety.bound_variables;
    }

    // Bottom-up: compute safety from children
    for (const auto& node : nodes) {
      ComputeSafetyFromChildren(node);
    }

    // Top-down: inherit bounds from parent to children
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

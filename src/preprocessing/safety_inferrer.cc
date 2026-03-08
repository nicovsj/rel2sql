#include "preprocessing/safety_inferrer.h"

#include "rel_ast/rel_ast_visitor.h"
#include "sql/aggregate_map.h"
#include "support/exceptions.h"

#include <functional>
#include <unordered_map>

namespace rel2sql {

namespace {

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
      if (abs_base->expr) ComputeRelAbsApplicationSafety(*node, *abs_base->expr, node->params);
    }
    return node;
  }

  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelFullApplication>& node) override {
    if (auto* id_base = dynamic_cast<RelIDApplBase*>(node->base.get())) {
      ComputeIDApplicationSafety(*node, node->params, id_base->id);
    } else if (auto* abs_base = dynamic_cast<RelExprApplBase*>(node->base.get())) {
      if (abs_base->expr) ComputeRelAbsApplicationSafety(*node, *abs_base->expr, node->params);
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
    if (node->op != RelCompOp::EQ && node->op != RelCompOp::NEQ) return node;
    if (node->op == RelCompOp::NEQ) return node;

    auto* lhs_id = dynamic_cast<RelIDTerm*>(node->lhs.get());
    auto* rhs_id = dynamic_cast<RelIDTerm*>(node->rhs.get());

    std::string variable_name;
    sql::ast::constant_t constant;

    if (lhs_id && node->rhs && node->rhs->constant) {
      variable_name = lhs_id->id;
      constant = *node->rhs->constant;
    } else if (rhs_id && node->lhs && node->lhs->constant) {
      variable_name = rhs_id->id;
      constant = *node->lhs->constant;
    } else {
      return node;
    }

    node->safety = BoundSet({Bound({variable_name}, {Projection(ConstantSource(constant))})});
    return node;
  }

 private:
  void ComputeBindingsSafety(RelNode& current, RelNode& child,
                             const std::vector<std::shared_ptr<RelBinding>>& bindings) {
    std::vector<std::string> variables;
    for (const auto& b : bindings) {
      if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) {
        variables.push_back(vb->id);
        if (vb->domain) {
          int domain_arity = container_->GetArity(*vb->domain);
          auto table_source = TableSource(*vb->domain, domain_arity);
          auto projection = Projection(table_source);
          container_->AddVariableDomain(vb->id, {projection});
        }
      }
    }
    current.safety = child.safety.WithRemovedVariables(variables);
    ExtractAndStoreVariableDomains(child.safety);
  }

  void ComputeIDApplicationSafety(RelNode& node,
                                  const std::vector<std::shared_ptr<RelApplParam>>& params,
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
        coeffs.push_back(term->term_linear_coeffs);
      } else {
        coeffs.emplace_back(std::nullopt);
      }
    }

    Bound bound{variable_names};
    bound.coeffs = std::move(coeffs);
    int arity = container_->GetArity(id);
    auto table_source = TableSource(id, arity);
    auto projection = Projection(variable_indices, table_source);
    bound.Add(projection);

    node.safety = BoundSet({bound});
  }

  void ComputeRelAbsApplicationSafety(RelNode& node, RelNode& base_node,
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
        coeffs.push_back(term->term_linear_coeffs);
      } else {
        coeffs.emplace_back(std::nullopt);
      }
    }

    auto promised_source = PromisedSource(base_node.arity);
    auto projection = Projection(variable_indices, promised_source);
    Bound bound(variable_names, {projection});
    bound.coeffs = std::move(coeffs);

    node.safety = BoundSet({bound});
  }

  Projection ExtractSingleVariableProjection(const Projection& proj, size_t variable_index) const {
    if (variable_index >= proj.projected_indices.size()) {
      throw std::runtime_error("Variable index out of bounds");
    }
    return Projection({proj.projected_indices[variable_index]}, proj.source);
  }

  void ExtractAndStoreVariableDomains(const BoundSet& safety) {
    for (const auto& bound : safety.bounds) {
      for (size_t var_index = 0; var_index < bound.variables.size(); ++var_index) {
        const std::string& var = bound.variables[var_index];
        std::unordered_set<Projection> var_domain;
        for (const auto& proj : bound.domain) {
          var_domain.insert(ExtractSingleVariableProjection(proj, var_index));
        }
        container_->AddVariableDomain(var, var_domain);
      }
    }
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

std::vector<std::shared_ptr<RelNode>> SafetyInferrer::CollectNodesPostOrder(
    std::shared_ptr<RelNode> root) {
  std::vector<std::shared_ptr<RelNode>> result;

  std::function<void(std::shared_ptr<RelNode>)> collect = [&result, &collect](
                                                             std::shared_ptr<RelNode> node) {
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

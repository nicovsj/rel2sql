#include "preprocessing/safety_visitor.h"

#include "sql/aggregate_map.h"
#include "support/exceptions.h"

namespace rel2sql {

std::shared_ptr<RelProgram> SafetyVisitor::Visit(const std::shared_ptr<RelProgram>& node) {
  std::unordered_set<std::string> visited;
  for (const auto& id : container_->SortedIDs()) {
    for (auto& def : node->defs) {
      if (!def || def->name != id) continue;
      Visit(def);
      visited.insert(id);
    }
  }
  for (auto& def : node->defs) {
    if (!def || visited.count(def->name)) continue;
    Visit(def);
  }
  return node;
}

std::shared_ptr<RelUnion> SafetyVisitor::Visit(const std::shared_ptr<RelUnion>& node) {
  if (node->exprs.empty()) return node;
  Visit(node->exprs[0]);
  node->safety = node->exprs[0]->safety;
  for (size_t i = 1; i < node->exprs.size(); ++i) {
    Visit(node->exprs[i]);
    node->safety = node->safety.IntersectWith(node->exprs[i]->safety);
  }
  return node;
}

std::shared_ptr<RelExpr> SafetyVisitor::Visit(const std::shared_ptr<RelProduct>& node) {
  node->safety = BoundSet();
  for (auto& expr : node->exprs) {
    if (expr) {
      Visit(expr);
      node->safety = node->safety.UnionWith(expr->safety);
    }
  }
  return node;
}

std::shared_ptr<RelExpr> SafetyVisitor::Visit(const std::shared_ptr<RelCondition>& node) {
  if (node->lhs) Visit(node->lhs);
  if (node->rhs) Visit(node->rhs);
  if (node->lhs && node->rhs) {
    node->safety = node->rhs->safety.UnionWith(node->lhs->safety);
  }
  return node;
}

std::shared_ptr<RelExpr> SafetyVisitor::Visit(const std::shared_ptr<RelAbstractionExpr>& node) {
  if (node->rel_abs) {
    Visit(node->rel_abs);
    node->safety = node->rel_abs->safety;
  }
  return node;
}

std::shared_ptr<RelExpr> SafetyVisitor::Visit(const std::shared_ptr<RelFormulaExpr>& node) {
  if (node->formula) {
    Visit(node->formula);
    node->safety = node->formula->safety;
  }
  return node;
}

std::shared_ptr<RelExpr> SafetyVisitor::Visit(const std::shared_ptr<RelExprAbstraction>& node) {
  if (node->expr) Visit(node->expr);
  if (node->expr) ComputeBindingsSafety(*node, *node->expr, node->bindings);
  return node;
}

std::shared_ptr<RelExpr> SafetyVisitor::Visit(const std::shared_ptr<RelFormulaAbstraction>& node) {
  if (node->formula) Visit(node->formula);
  if (node->formula) ComputeBindingsSafety(*node, *node->formula, node->bindings);
  return node;
}

std::shared_ptr<RelExpr> SafetyVisitor::Visit(const std::shared_ptr<RelPartialApplication>& node) {
  if (auto* abs_base = dynamic_cast<RelAbstractionApplBase*>(node->base.get())) {
    if (abs_base->rel_abs) Visit(abs_base->rel_abs);
  }

  if (auto* id_base = dynamic_cast<RelIDApplBase*>(node->base.get())) {
    std::string id = id_base->id;
    if (GetAggregateMap().find(id) != GetAggregateMap().end()) {
      if (!node->params.empty()) {
        auto expr = node->params[0]->GetExpr();
        if (expr) {
          Visit(expr);
          node->safety = expr->safety;
        }
      }
      return node;
    }
    ComputeIDApplicationSafety(*node, node->params, id);
  } else if (auto* abs_base = dynamic_cast<RelAbstractionApplBase*>(node->base.get())) {
    if (abs_base->rel_abs) ComputeRelAbsApplicationSafety(*node, *abs_base->rel_abs, node->params);
  }
  return node;
}

std::shared_ptr<RelFormula> SafetyVisitor::Visit(const std::shared_ptr<RelFullApplication>& node) {
  if (auto* abs_base = dynamic_cast<RelAbstractionApplBase*>(node->base.get())) {
    if (abs_base->rel_abs) Visit(abs_base->rel_abs);
  }

  if (auto* id_base = dynamic_cast<RelIDApplBase*>(node->base.get())) {
    ComputeIDApplicationSafety(*node, node->params, id_base->id);
  } else if (auto* abs_base = dynamic_cast<RelAbstractionApplBase*>(node->base.get())) {
    if (abs_base->rel_abs) ComputeRelAbsApplicationSafety(*node, *abs_base->rel_abs, node->params);
  }
  return node;
}

std::shared_ptr<RelFormula> SafetyVisitor::Visit(const std::shared_ptr<RelConjunction>& node) {
  if (node->lhs) Visit(node->lhs);
  if (node->rhs) Visit(node->rhs);
  if (node->lhs && node->rhs) {
    node->safety = node->lhs->safety.UnionWith(node->rhs->safety);
  }
  return node;
}

std::shared_ptr<RelFormula> SafetyVisitor::Visit(const std::shared_ptr<RelDisjunction>& node) {
    if (node->lhs) Visit(node->lhs);
    if (node->rhs) Visit(node->rhs);
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

std::shared_ptr<RelFormula> SafetyVisitor::Visit(const std::shared_ptr<RelNegation>& node) {
  if (node->formula) {
    Visit(node->formula);
    node->safety = node->formula->safety;
  }
  return node;
}

std::shared_ptr<RelFormula> SafetyVisitor::Visit(const std::shared_ptr<RelExistential>& node) {
  if (node->formula) Visit(node->formula);
  if (node->formula) ComputeBindingsSafety(*node, *node->formula, node->bindings);
  return node;
}

std::shared_ptr<RelFormula> SafetyVisitor::Visit(const std::shared_ptr<RelUniversal>& node) {
  if (node->formula) Visit(node->formula);
  if (node->formula) ComputeBindingsSafety(*node, *node->formula, node->bindings);
  return node;
}

std::shared_ptr<RelFormula> SafetyVisitor::Visit(const std::shared_ptr<RelParen>& node) {
  if (node->formula) {
    Visit(node->formula);
    node->safety = node->formula->safety;
  }
  return node;
}

std::shared_ptr<RelFormula> SafetyVisitor::Visit(const std::shared_ptr<RelComparison>& node) {
  if (node->lhs) Visit(node->lhs);
  if (node->rhs) Visit(node->rhs);

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

void SafetyVisitor::ComputeBindingsSafety(RelNode& current, RelNode& child,
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

void SafetyVisitor::ComputeIDApplicationSafety(RelNode& node, const std::vector<std::shared_ptr<RelApplParam>>& params,
                                               const std::string& id) {
  std::vector<std::string> variable_names;
  std::vector<size_t> variable_indices;
  std::vector<std::optional<std::pair<double, double>>> coeffs;

  for (size_t i = 0; i < params.size(); ++i) {
    auto expr = params[i] ? params[i]->GetExpr() : nullptr;
    if (!expr) continue;
    Visit(expr);
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

void SafetyVisitor::ComputeRelAbsApplicationSafety(RelNode& node, RelNode& base_node,
                                                   const std::vector<std::shared_ptr<RelApplParam>>& params) {
  std::vector<std::string> variable_names;
  std::vector<size_t> variable_indices;
  std::vector<std::optional<std::pair<double, double>>> coeffs;

  for (size_t i = 0; i < params.size(); ++i) {
    auto expr = params[i] ? params[i]->GetExpr() : nullptr;
    if (!expr) continue;
    Visit(expr);
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

Projection SafetyVisitor::ExtractSingleVariableProjection(const Projection& proj, size_t variable_index) const {
  if (variable_index >= proj.projected_indices.size()) {
    throw std::runtime_error("Variable index out of bounds");
  }
  return Projection({proj.projected_indices[variable_index]}, proj.source);
}

void SafetyVisitor::ExtractAndStoreVariableDomains(const BoundSet& safety) {
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

}  // namespace rel2sql

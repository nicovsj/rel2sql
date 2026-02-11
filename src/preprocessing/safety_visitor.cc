#include "preprocessing/safety_visitor.h"

#include "preprocessing/fixpoint_safety_visitor.h"
#include "sql/aggregate_map.h"
#include "support/exceptions.h"

namespace rel2sql {

void SafetyVisitor::Visit(RelProgram& node) {
  std::unordered_set<std::string> visited;
  for (const auto& id : container_->SortedIDs()) {
    for (auto& def : node.defs) {
      if (!def || def->name != id) continue;
      def->Accept(*this);
      visited.insert(id);
    }
  }
  for (auto& def : node.defs) {
    if (!def || visited.count(def->name)) continue;
    def->Accept(*this);
  }
}

void SafetyVisitor::Visit(RelDef& node) {
  current_relation_ = node.name;

  auto info = container_->GetRelationInfo(current_relation_);
  if (info && !info->recursion_metadata.empty()) {
    if (node.body && node.body->exprs.size() == 1) {
      auto* bf = dynamic_cast<RelBindingsFormula*>(node.body->exprs[0].get());
      if (bf) {
        FixpointSafetyVisitor fixpoint_visitor(container_, node.name);
        BoundSet fixpoint_safety = fixpoint_visitor.ComputeFixpoint(*bf);
        if (bf->formula) bf->formula->safety = fixpoint_safety;
        fixpoint_visitor.ComputeBindingsSafety(*bf, *bf->formula, bf->bindings);
        node.body->safety = bf->safety;
        current_relation_.clear();
        return;
      }
    }
  }

  if (node.body) node.body->Accept(*this);
  current_relation_.clear();
}

void SafetyVisitor::Visit(RelAbstraction& node) {
  if (node.exprs.empty()) return;
  node.exprs[0]->Accept(*this);
  node.safety = node.exprs[0]->safety;
  for (size_t i = 1; i < node.exprs.size(); ++i) {
    node.exprs[i]->Accept(*this);
    node.safety = node.safety.IntersectWith(node.exprs[i]->safety);
  }
}

void SafetyVisitor::Visit(RelTermExpr& node) {
  if (node.term) node.term->Accept(*this);
  if (node.term) node.safety = node.term->safety;
}

void SafetyVisitor::Visit(RelProductExpr& node) {
  node.safety = BoundSet();
  for (auto& expr : node.exprs) {
    if (expr) {
      expr->Accept(*this);
      node.safety = node.safety.UnionWith(expr->safety);
    }
  }
}

void SafetyVisitor::Visit(RelConditionExpr& node) {
  if (node.lhs) node.lhs->Accept(*this);
  if (node.rhs) node.rhs->Accept(*this);
  if (node.lhs && node.rhs) {
    node.safety = node.rhs->safety.UnionWith(node.lhs->safety);
  }
}

void SafetyVisitor::Visit(RelAbstractionExpr& node) {
  if (node.rel_abs) {
    node.rel_abs->Accept(*this);
    node.safety = node.rel_abs->safety;
  }
}

void SafetyVisitor::Visit(RelFormulaExpr& node) {
  if (node.formula) {
    node.formula->Accept(*this);
    node.safety = node.formula->safety;
  }
}

void SafetyVisitor::Visit(RelBindingsExpr& node) {
  if (node.expr) node.expr->Accept(*this);
  if (node.expr) ComputeBindingsSafety(node, *node.expr, node.bindings);
}

void SafetyVisitor::Visit(RelBindingsFormula& node) {
  if (node.formula) node.formula->Accept(*this);
  if (node.formula) ComputeBindingsSafety(node, *node.formula, node.bindings);
}

void SafetyVisitor::Visit(RelPartialAppl& node) {
  if (auto* abs_base = dynamic_cast<RelAbstractionApplBase*>(node.base.get())) {
    if (abs_base->rel_abs) abs_base->rel_abs->Accept(*this);
  }

  if (auto* id_base = dynamic_cast<RelIDApplBase*>(node.base.get())) {
    std::string id = id_base->id;
    if (GetAggregateMap().find(id) != GetAggregateMap().end()) {
      if (!node.params.empty()) {
        auto expr = node.params[0]->GetExpr();
        if (expr) {
          expr->Accept(*this);
          node.safety = expr->safety;
        }
      }
      return;
    }
    ComputeIDApplicationSafety(node, node.params, id);
  } else if (auto* abs_base = dynamic_cast<RelAbstractionApplBase*>(node.base.get())) {
    if (abs_base->rel_abs) ComputeRelAbsApplicationSafety(node, *abs_base->rel_abs, node.params);
  }
}

void SafetyVisitor::Visit(RelFullAppl& node) {
  if (auto* abs_base = dynamic_cast<RelAbstractionApplBase*>(node.base.get())) {
    if (abs_base->rel_abs) abs_base->rel_abs->Accept(*this);
  }

  if (auto* id_base = dynamic_cast<RelIDApplBase*>(node.base.get())) {
    ComputeIDApplicationSafety(node, node.params, id_base->id);
  } else if (auto* abs_base = dynamic_cast<RelAbstractionApplBase*>(node.base.get())) {
    if (abs_base->rel_abs) ComputeRelAbsApplicationSafety(node, *abs_base->rel_abs, node.params);
  }
}

void SafetyVisitor::Visit(RelBinOp& node) {
  if (node.op == RelLogicalOp::AND) {
    if (node.lhs) node.lhs->Accept(*this);
    if (node.rhs) node.rhs->Accept(*this);
    if (node.lhs && node.rhs) {
      node.safety = node.lhs->safety.UnionWith(node.rhs->safety);
    }
    return;
  }
  if (node.op == RelLogicalOp::OR) {
    if (node.lhs) node.lhs->Accept(*this);
    if (node.rhs) node.rhs->Accept(*this);
    if (node.lhs && node.rhs) {
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
      if (has_non_trivial_affine(node.lhs->safety) || has_non_trivial_affine(node.rhs->safety)) {
        throw NotImplementedException(
            "Safety analysis for disjunctions with linear term parameters (a*x + b) "
            "is not supported yet when (a,b) != (1,0).",
            SourceLocation(0, 0));
      }
      node.safety = node.lhs->safety.MergeWith(node.rhs->safety);
    }
    return;
  }
  throw TranslationException("Unknown binary operator", ErrorCode::UNKNOWN_BINARY_OPERATOR,
                             SourceLocation(0, 0));
}

void SafetyVisitor::Visit(RelUnOp& node) {
  if (node.formula) {
    node.formula->Accept(*this);
    node.safety = node.formula->safety;
  }
}

void SafetyVisitor::Visit(RelQuantification& node) {
  if (node.formula) node.formula->Accept(*this);
  if (node.formula) ComputeBindingsSafety(node, *node.formula, node.bindings);
}

void SafetyVisitor::Visit(RelParen& node) {
  if (node.formula) {
    node.formula->Accept(*this);
    node.safety = node.formula->safety;
  }
}

void SafetyVisitor::Visit(RelComparison& node) {
  if (node.lhs) node.lhs->Accept(*this);
  if (node.rhs) node.rhs->Accept(*this);

  if (node.op != RelCompOp::EQ && node.op != RelCompOp::NEQ) return;
  if (node.op == RelCompOp::NEQ) return;

  auto* lhs_id = dynamic_cast<RelIDTerm*>(node.lhs.get());
  auto* rhs_id = dynamic_cast<RelIDTerm*>(node.rhs.get());

  std::string variable_name;
  sql::ast::constant_t constant;

  if (lhs_id && node.rhs && node.rhs->constant) {
    variable_name = lhs_id->id;
    constant = *node.rhs->constant;
  } else if (rhs_id && node.lhs && node.lhs->constant) {
    variable_name = rhs_id->id;
    constant = *node.lhs->constant;
  } else {
    return;
  }

  node.safety = BoundSet({Bound({variable_name}, {Projection(ConstantSource(constant))})});
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

void SafetyVisitor::ComputeIDApplicationSafety(RelNode& node,
                                                const std::vector<std::shared_ptr<RelApplParam>>& params,
                                                const std::string& id) {
  std::vector<std::string> variable_names;
  std::vector<size_t> variable_indices;
  std::vector<std::optional<std::pair<double, double>>> coeffs;

  for (size_t i = 0; i < params.size(); ++i) {
    auto expr = params[i] ? params[i]->GetExpr() : nullptr;
    if (!expr) continue;
    expr->Accept(*this);
    auto* term_expr = dynamic_cast<RelTermExpr*>(expr.get());
    if (!term_expr || !term_expr->term) continue;
    if (expr->variables.size() != 1) continue;

    std::string variable = *expr->variables.begin();
    variable_indices.push_back(i);
    variable_names.push_back(variable);

    if (term_expr->term->term_linear_coeffs &&
        !term_expr->term->IsInvalidTermExpression()) {
      coeffs.push_back(term_expr->term->term_linear_coeffs);
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
    expr->Accept(*this);
    auto* term_expr = dynamic_cast<RelTermExpr*>(expr.get());
    if (!term_expr || !term_expr->term || expr->variables.size() != 1) continue;

    variable_names.push_back(*expr->variables.begin());
    variable_indices.push_back(i);

    if (term_expr->term->term_linear_coeffs &&
        !term_expr->term->IsInvalidTermExpression()) {
      coeffs.push_back(term_expr->term->term_linear_coeffs);
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

Projection SafetyVisitor::ExtractSingleVariableProjection(const Projection& proj,
                                                           size_t variable_index) const {
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

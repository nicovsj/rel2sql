#include "preprocessing/vars_visitor.h"

#include "support/exceptions.h"

namespace rel2sql {

std::shared_ptr<RelProgram> VariablesVisitor::Visit(const std::shared_ptr<RelProgram>& node) {
  for (auto& def : node->defs) {
    if (def) {
      Visit(def);
      node->VariablesInplaceUnion(*def);
    }
  }
  return node;
}

std::shared_ptr<RelDef> VariablesVisitor::Visit(const std::shared_ptr<RelDef>& node) {
  if (node->body) {
    Visit(node->body);
    node->variables = node->body->variables;
    node->free_variables = node->body->free_variables;
  }
  return node;
}

std::shared_ptr<RelUnion> VariablesVisitor::Visit(const std::shared_ptr<RelUnion>& node) {
  for (auto& expr : node->exprs) {
    if (expr) {
      Visit(expr);
      node->VariablesInplaceUnion(*expr);
    }
  }
  return node;
}

std::shared_ptr<RelTerm> VariablesVisitor::Visit(const std::shared_ptr<RelIDTerm>& node) {
  if (builder_->IsVar(node->id)) {
    node->free_variables.insert(node->id);
    node->variables.insert(node->id);
  }
  return node;
}

std::shared_ptr<RelTerm> VariablesVisitor::Visit(const std::shared_ptr<RelOpTerm>& node) {
  if (node->lhs) Visit(node->lhs);
  if (node->rhs) Visit(node->rhs);
  if (node->lhs) {
    node->variables = node->lhs->variables;
    node->free_variables = node->lhs->free_variables;
  }
  if (node->rhs) node->VariablesInplaceUnion(*node->rhs);
  return node;
}

std::shared_ptr<RelTerm> VariablesVisitor::Visit(const std::shared_ptr<RelParenthesisTerm>& node) {
  if (node->term) Visit(node->term);
  if (node->term) {
    node->variables = node->term->variables;
    node->free_variables = node->term->free_variables;
  }
  return node;
}

std::shared_ptr<RelExpr> VariablesVisitor::Visit(const std::shared_ptr<RelProduct>& node) {
  for (auto& expr : node->exprs) {
    if (expr) {
      Visit(expr);
      node->VariablesInplaceUnion(*expr);
    }
  }
  return node;
}

std::shared_ptr<RelExpr> VariablesVisitor::Visit(const std::shared_ptr<RelCondition>& node) {
  if (node->lhs) Visit(node->lhs);
  if (node->rhs) Visit(node->rhs);
  if (node->lhs) node->VariablesInplaceUnion(*node->lhs);
  if (node->rhs) node->VariablesInplaceUnion(*node->rhs);
  return node;
}

std::shared_ptr<RelExpr> VariablesVisitor::Visit(const std::shared_ptr<RelExprAbstraction>& node) {
  if (node->expr) Visit(node->expr);
  if (node->expr) {
    node->variables = node->expr->variables;
    node->free_variables = node->expr->free_variables;
  }
  std::set<std::string> bindings_vars, bindings_free;
  for (const auto& b : node->bindings) {
    if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) {
      bindings_vars.insert(vb->id);
      bindings_free.insert(vb->id);
    }
  }
  node->variables.insert(bindings_vars.begin(), bindings_vars.end());
  for (const auto& var : bindings_free) node->free_variables.erase(var);
  return node;
}

std::shared_ptr<RelExpr> VariablesVisitor::Visit(const std::shared_ptr<RelFormulaAbstraction>& node) {
  if (node->formula) {
    Visit(node->formula);
    node->variables = node->formula->variables;
    node->free_variables = node->formula->free_variables;
  }
  std::set<std::string> bindings_vars, bindings_free;
  for (const auto& b : node->bindings) {
    if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) {
      bindings_vars.insert(vb->id);
      bindings_free.insert(vb->id);
    }
  }
  node->variables.insert(bindings_vars.begin(), bindings_vars.end());
  for (const auto& var : bindings_free) node->free_variables.erase(var);
  return node;
}

std::shared_ptr<RelExpr> VariablesVisitor::Visit(const std::shared_ptr<RelPartialApplication>& node) {
  std::set<std::string> base_vars, base_free;
  if (auto* abs_base = dynamic_cast<RelExprApplBase*>(node->base.get())) {
    if (abs_base->expr) {
      Visit(abs_base->expr);
      base_vars = abs_base->expr->variables;
      base_free = abs_base->expr->free_variables;
    }
  }
  node->variables = base_vars;
  node->free_variables = base_free;
  for (const auto& param : node->params) {
    if (param && param->GetExpr()) {
      Visit(param->GetExpr());
      node->VariablesInplaceUnion(*param->GetExpr());
    }
  }
  return node;
}

std::shared_ptr<RelFormula> VariablesVisitor::Visit(const std::shared_ptr<RelFullApplication>& node) {
  std::set<std::string> base_vars, base_free;
  if (auto* abs_base = dynamic_cast<RelExprApplBase*>(node->base.get())) {
    if (abs_base->expr) {
      Visit(abs_base->expr);
      base_vars = abs_base->expr->variables;
      base_free = abs_base->expr->free_variables;
    }
  }
  node->variables = base_vars;
  node->free_variables = base_free;
  for (const auto& param : node->params) {
    if (param && param->GetExpr()) {
      Visit(param->GetExpr());
      node->VariablesInplaceUnion(*param->GetExpr());
    }
  }
  return node;
}

std::shared_ptr<RelFormula> VariablesVisitor::Visit(const std::shared_ptr<RelConjunction>& node) {
  if (node->lhs) Visit(node->lhs);
  if (node->rhs) Visit(node->rhs);

  if (node->lhs) node->VariablesInplaceUnion(*node->lhs);
  if (node->rhs) node->VariablesInplaceUnion(*node->rhs);
  return node;
}

std::shared_ptr<RelFormula> VariablesVisitor::Visit(const std::shared_ptr<RelDisjunction>& node) {
  if (node->lhs) Visit(node->lhs);
  if (node->rhs) Visit(node->rhs);

  // Allow different free variables; safety inference (inherited bounds from parent) ensures
  // FV(F1) ∪ FV(F2) ⊆ bound(F) when symmetric difference is non-empty.
  if (node->lhs) node->VariablesInplaceUnion(*node->lhs);
  if (node->rhs) node->VariablesInplaceUnion(*node->rhs);
  return node;
}

std::shared_ptr<RelFormula> VariablesVisitor::Visit(const std::shared_ptr<RelNegation>& node) {
  if (node->formula) {
    Visit(node->formula);
    node->variables = node->formula->variables;
    node->free_variables = node->formula->free_variables;
  }
  return node;
}

std::shared_ptr<RelFormula> VariablesVisitor::Visit(const std::shared_ptr<RelExistential>& node) {
  if (node->formula) Visit(node->formula);
  if (node->formula) {
    node->variables = node->formula->variables;
    node->free_variables = node->formula->free_variables;
  }
  std::set<std::string> bindings_vars, bindings_free;
  for (const auto& b : node->bindings) {
    if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) {
      bindings_vars.insert(vb->id);
      bindings_free.insert(vb->id);
    }
  }
  node->variables.insert(bindings_vars.begin(), bindings_vars.end());
  for (const auto& var : bindings_free) node->free_variables.erase(var);
  return node;
}

std::shared_ptr<RelFormula> VariablesVisitor::Visit(const std::shared_ptr<RelUniversal>& node) {
  if (node->formula) Visit(node->formula);
  if (node->formula) {
    node->variables = node->formula->variables;
    node->free_variables = node->formula->free_variables;
  }
  std::set<std::string> bindings_vars, bindings_free;
  for (const auto& b : node->bindings) {
    if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) {
      bindings_vars.insert(vb->id);
      bindings_free.insert(vb->id);
    }
  }
  node->variables.insert(bindings_vars.begin(), bindings_vars.end());
  for (const auto& var : bindings_free) node->free_variables.erase(var);
  return node;
}

std::shared_ptr<RelFormula> VariablesVisitor::Visit(const std::shared_ptr<RelParen>& node) {
  if (node->formula) {
    Visit(node->formula);
    node->variables = node->formula->variables;
    node->free_variables = node->formula->free_variables;
  }
  return node;
}

std::shared_ptr<RelFormula> VariablesVisitor::Visit(const std::shared_ptr<RelComparison>& node) {
  if (node->lhs) Visit(node->lhs);
  if (node->rhs) Visit(node->rhs);
  if (node->lhs) {
    node->variables = node->lhs->variables;
    node->free_variables = node->lhs->free_variables;
  }
  if (node->rhs) node->VariablesInplaceUnion(*node->rhs);
  return node;
}

}  // namespace rel2sql

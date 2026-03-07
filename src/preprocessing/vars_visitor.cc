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

std::shared_ptr<RelAbstraction> VariablesVisitor::Visit(const std::shared_ptr<RelAbstraction>& node) {
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

std::shared_ptr<RelExpr> VariablesVisitor::Visit(const std::shared_ptr<RelTermExpr>& node) {
  if (node->term) Visit(node->term);
  if (node->term) {
    node->variables = node->term->variables;
    node->free_variables = node->term->free_variables;
  }
  return node;
}

std::shared_ptr<RelExpr> VariablesVisitor::Visit(const std::shared_ptr<RelProductExpr>& node) {
  for (auto& expr : node->exprs) {
    if (expr) {
      Visit(expr);
      node->VariablesInplaceUnion(*expr);
    }
  }
  return node;
}

std::shared_ptr<RelExpr> VariablesVisitor::Visit(const std::shared_ptr<RelConditionExpr>& node) {
  if (node->lhs) Visit(node->lhs);
  if (node->rhs) Visit(node->rhs);
  if (node->lhs) node->VariablesInplaceUnion(*node->lhs);
  if (node->rhs) node->VariablesInplaceUnion(*node->rhs);
  return node;
}

std::shared_ptr<RelExpr> VariablesVisitor::Visit(const std::shared_ptr<RelAbstractionExpr>& node) {
  if (node->rel_abs) {
    Visit(node->rel_abs);
    node->variables = node->rel_abs->variables;
    node->free_variables = node->rel_abs->free_variables;
  }
  return node;
}

std::shared_ptr<RelExpr> VariablesVisitor::Visit(const std::shared_ptr<RelFormulaExpr>& node) {
  if (node->formula) {
    Visit(node->formula);
    node->variables = node->formula->variables;
    node->free_variables = node->formula->free_variables;
  }
  return node;
}

std::shared_ptr<RelExpr> VariablesVisitor::Visit(const std::shared_ptr<RelBindingsExpr>& node) {
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

std::shared_ptr<RelExpr> VariablesVisitor::Visit(const std::shared_ptr<RelBindingsFormula>& node) {
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

std::shared_ptr<RelExpr> VariablesVisitor::Visit(const std::shared_ptr<RelPartialAppl>& node) {
  std::set<std::string> base_vars, base_free;
  if (auto* abs_base = dynamic_cast<RelAbstractionApplBase*>(node->base.get())) {
    if (abs_base->rel_abs) {
      Visit(abs_base->rel_abs);
      base_vars = abs_base->rel_abs->variables;
      base_free = abs_base->rel_abs->free_variables;
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

std::shared_ptr<RelFormula> VariablesVisitor::Visit(const std::shared_ptr<RelFullAppl>& node) {
  std::set<std::string> base_vars, base_free;
  if (auto* abs_base = dynamic_cast<RelAbstractionApplBase*>(node->base.get())) {
    if (abs_base->rel_abs) {
      Visit(abs_base->rel_abs);
      base_vars = abs_base->rel_abs->variables;
      base_free = abs_base->rel_abs->free_variables;
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

std::shared_ptr<RelFormula> VariablesVisitor::Visit(const std::shared_ptr<RelBinOp>& node) {
  if (node->lhs) Visit(node->lhs);
  if (node->rhs) Visit(node->rhs);

  if (node->lhs) {
    node->variables = node->lhs->variables;
    node->free_variables = node->lhs->free_variables;
  }
  if (node->rhs) {
    if (node->op == RelLogicalOp::OR && node->rhs->free_variables != node->lhs->free_variables) {
      throw TranslationException("Disjunction formula with different free variables", ErrorCode::UNBALANCED_VARIABLE, SourceLocation(0, 0));
    }
    node->VariablesInplaceUnion(*node->rhs);
  }


  return node;
}

std::shared_ptr<RelFormula> VariablesVisitor::Visit(const std::shared_ptr<RelUnOp>& node) {
  if (node->formula) {
    Visit(node->formula);
    node->variables = node->formula->variables;
    node->free_variables = node->formula->free_variables;
  }
  return node;
}

std::shared_ptr<RelFormula> VariablesVisitor::Visit(const std::shared_ptr<RelQuantification>& node) {
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

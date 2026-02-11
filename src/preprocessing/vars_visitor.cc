#include "preprocessing/vars_visitor.h"

namespace rel2sql {

void VariablesVisitor::Visit(RelProgram& node) {
  for (auto& def : node.defs) {
    if (def) {
      def->Accept(*this);
      node.VariablesInplaceUnion(*def);
    }
  }
}

void VariablesVisitor::Visit(RelDef& node) {
  if (node.body) {
    node.body->Accept(*this);
    node.variables = node.body->variables;
    node.free_variables = node.body->free_variables;
  }
}

void VariablesVisitor::Visit(RelAbstraction& node) {
  for (auto& expr : node.exprs) {
    if (expr) {
      expr->Accept(*this);
      node.VariablesInplaceUnion(*expr);
    }
  }
}

void VariablesVisitor::Visit(RelIDTerm& node) {
  if (container_->IsVar(node.id)) {
    node.free_variables.insert(node.id);
    node.variables.insert(node.id);
  }
}

void VariablesVisitor::Visit(RelNumTerm& node) { RelASTVisitor::Visit(node); }

void VariablesVisitor::Visit(RelOpTerm& node) {
  if (node.lhs) node.lhs->Accept(*this);
  if (node.rhs) node.rhs->Accept(*this);
  if (node.lhs) {
    node.variables = node.lhs->variables;
    node.free_variables = node.lhs->free_variables;
  }
  if (node.rhs) node.VariablesInplaceUnion(*node.rhs);
}

void VariablesVisitor::Visit(RelParenthesisTerm& node) {
  if (node.term) node.term->Accept(*this);
  if (node.term) {
    node.variables = node.term->variables;
    node.free_variables = node.term->free_variables;
  }
}

void VariablesVisitor::Visit(RelLitExpr& node) { RelASTVisitor::Visit(node); }

void VariablesVisitor::Visit(RelTermExpr& node) {
  if (node.term) node.term->Accept(*this);
  if (node.term) {
    node.variables = node.term->variables;
    node.free_variables = node.term->free_variables;
  }
}

void VariablesVisitor::Visit(RelProductExpr& node) {
  for (auto& expr : node.exprs) {
    if (expr) {
      expr->Accept(*this);
      node.VariablesInplaceUnion(*expr);
    }
  }
}

void VariablesVisitor::Visit(RelConditionExpr& node) {
  if (node.lhs) node.lhs->Accept(*this);
  if (node.rhs) node.rhs->Accept(*this);
  if (node.lhs) node.VariablesInplaceUnion(*node.lhs);
  if (node.rhs) node.VariablesInplaceUnion(*node.rhs);
}

void VariablesVisitor::Visit(RelAbstractionExpr& node) {
  if (node.rel_abs) {
    node.rel_abs->Accept(*this);
    node.variables = node.rel_abs->variables;
    node.free_variables = node.rel_abs->free_variables;
  }
}

void VariablesVisitor::Visit(RelFormulaExpr& node) {
  if (node.formula) {
    node.formula->Accept(*this);
    node.variables = node.formula->variables;
    node.free_variables = node.formula->free_variables;
  }
}

void VariablesVisitor::Visit(RelBindingsExpr& node) {
  if (node.expr) node.expr->Accept(*this);
  if (node.expr) {
    node.variables = node.expr->variables;
    node.free_variables = node.expr->free_variables;
  }
  std::set<std::string> bindings_vars, bindings_free;
  for (const auto& b : node.bindings) {
    if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) {
      bindings_vars.insert(vb->id);
      bindings_free.insert(vb->id);
    }
  }
  node.variables.insert(bindings_vars.begin(), bindings_vars.end());
  for (const auto& var : bindings_free) node.free_variables.erase(var);
}

void VariablesVisitor::Visit(RelBindingsFormula& node) {
  if (node.formula) {
    node.formula->Accept(*this);
    node.variables = node.formula->variables;
    node.free_variables = node.formula->free_variables;
  }
  std::set<std::string> bindings_vars, bindings_free;
  for (const auto& b : node.bindings) {
    if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) {
      bindings_vars.insert(vb->id);
      bindings_free.insert(vb->id);
    }
  }
  node.variables.insert(bindings_vars.begin(), bindings_vars.end());
  for (const auto& var : bindings_free) node.free_variables.erase(var);
}

void VariablesVisitor::Visit(RelPartialAppl& node) {
  std::set<std::string> base_vars, base_free;
  if (auto* abs_base = dynamic_cast<RelAbstractionApplBase*>(node.base.get())) {
    if (abs_base->rel_abs) {
      abs_base->rel_abs->Accept(*this);
      base_vars = abs_base->rel_abs->variables;
      base_free = abs_base->rel_abs->free_variables;
    }
  }
  node.variables = base_vars;
  node.free_variables = base_free;
  for (const auto& param : node.params) {
    if (param && param->GetExpr()) {
      param->GetExpr()->Accept(*this);
      node.VariablesInplaceUnion(*param->GetExpr());
    }
  }
}

void VariablesVisitor::Visit(RelFullAppl& node) {
  std::set<std::string> base_vars, base_free;
  if (auto* abs_base = dynamic_cast<RelAbstractionApplBase*>(node.base.get())) {
    if (abs_base->rel_abs) {
      abs_base->rel_abs->Accept(*this);
      base_vars = abs_base->rel_abs->variables;
      base_free = abs_base->rel_abs->free_variables;
    }
  }
  node.variables = base_vars;
  node.free_variables = base_free;
  for (const auto& param : node.params) {
    if (param && param->GetExpr()) {
      param->GetExpr()->Accept(*this);
      node.VariablesInplaceUnion(*param->GetExpr());
    }
  }
}

void VariablesVisitor::Visit(RelBinOp& node) {
  if (node.lhs) node.lhs->Accept(*this);
  if (node.rhs) node.rhs->Accept(*this);
  if (node.lhs) {
    node.variables = node.lhs->variables;
    node.free_variables = node.lhs->free_variables;
  }
  if (node.rhs) node.VariablesInplaceUnion(*node.rhs);
}

void VariablesVisitor::Visit(RelUnOp& node) {
  if (node.formula) {
    node.formula->Accept(*this);
    node.variables = node.formula->variables;
    node.free_variables = node.formula->free_variables;
  }
}

void VariablesVisitor::Visit(RelQuantification& node) {
  if (node.formula) node.formula->Accept(*this);
  if (node.formula) {
    node.variables = node.formula->variables;
    node.free_variables = node.formula->free_variables;
  }
  std::set<std::string> bindings_vars, bindings_free;
  for (const auto& b : node.bindings) {
    if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) {
      bindings_vars.insert(vb->id);
      bindings_free.insert(vb->id);
    }
  }
  node.variables.insert(bindings_vars.begin(), bindings_vars.end());
  for (const auto& var : bindings_free) node.free_variables.erase(var);
}

void VariablesVisitor::Visit(RelParen& node) {
  if (node.formula) {
    node.formula->Accept(*this);
    node.variables = node.formula->variables;
    node.free_variables = node.formula->free_variables;
  }
}

void VariablesVisitor::Visit(RelComparison& node) {
  if (node.lhs) node.lhs->Accept(*this);
  if (node.rhs) node.rhs->Accept(*this);
  if (node.lhs) {
    node.variables = node.lhs->variables;
    node.free_variables = node.lhs->free_variables;
  }
  if (node.rhs) node.VariablesInplaceUnion(*node.rhs);
}

}  // namespace rel2sql

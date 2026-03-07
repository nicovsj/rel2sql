#include "preprocessing/recursion_visitor.h"

namespace rel2sql {

std::shared_ptr<RelProgram> RecursionVisitor::Visit(const std::shared_ptr<RelProgram>& node) {
  for (auto& def : node->defs) {
    if (def) Visit(def);
  }
  return node;
}

std::shared_ptr<RelDef> RecursionVisitor::Visit(const std::shared_ptr<RelDef>& node) {
  current_q_ = node->name;
  if (node->body) Visit(node->body);
  current_q_.clear();
  return node;
}

std::shared_ptr<RelAbstraction> RecursionVisitor::Visit(const std::shared_ptr<RelAbstraction>& node) {
  if (node->exprs.size() != 1) return node;
  auto bf = std::dynamic_pointer_cast<RelBindingsFormula>(node->exprs[0]);
  if (!bf) return node;
  Visit(bf);
  if (bf->is_recursive) {
    node->is_recursive = true;
    node->recursive_definition_name = current_q_;
  }
  return node;
}

std::shared_ptr<RelExpr> RecursionVisitor::Visit(const std::shared_ptr<RelBindingsFormula>& node) {
  RecursionPatternMatch match;
  if (!CheckRecursionPattern(node->formula, node->bindings, match)) return node;

  node->is_recursive = true;
  node->recursive_definition_name = current_q_;

  if (current_q_.empty()) return node;

  for (const auto& g : match.base_disjuncts) {
    auto formula_expr = std::make_shared<RelFormulaExpr>(g);
    std::vector<std::shared_ptr<RelExpr>> exprs = {formula_expr};
    auto rel_abs = std::make_shared<RelAbstraction>(std::move(exprs));
    container_->RegisterRecursiveBaseDisjunct(current_q_, rel_abs);
  }

  for (const auto& branch : match.recursive_disjuncts) {
    RecursiveBranchInfoTyped info;
    info.exists_clause = branch.exists_clause;
    info.recursive_call = branch.recursive_call;
    info.residual_formula = branch.residual_formula;
    container_->RegisterRecursiveBranch(current_q_, info);
  }
  return node;
}

std::shared_ptr<RelFormula> RecursionVisitor::Visit(const std::shared_ptr<RelFullAppl>& node) {
  if (auto abs_base = std::dynamic_pointer_cast<RelAbstractionApplBase>(node->base)) {
    if (abs_base->rel_abs) Visit(abs_base->rel_abs);
  }
  for (const auto& param : node->params) {
    if (param && param->GetExpr()) Visit(param->GetExpr());
  }
  return node;
}

bool RecursionVisitor::IsRecursiveID(const std::string& id) const {
  std::unordered_set<std::string> visited;
  std::unordered_set<std::string> rec_stack;
  std::stack<std::string> stack;

  stack.push(id);
  rec_stack.insert(id);

  while (!stack.empty()) {
    std::string current = stack.top();
    stack.pop();

    if (visited.count(current)) continue;
    visited.insert(current);

    auto info = container_->GetRelationInfo(current);
    if (info) {
      for (const auto& dep : info->dependencies) {
        if (dep == id) return true;
        if (!visited.count(dep) && !rec_stack.count(dep)) {
          stack.push(dep);
          rec_stack.insert(dep);
        }
      }
    }
  }
  return false;
}

std::unordered_set<std::string> RecursionVisitor::CollectIDs(const std::shared_ptr<RelFormula>& formula) const {
  std::unordered_set<std::string> ids;
  if (!formula) return ids;

  auto* full = dynamic_cast<RelFullAppl*>(formula.get());
  if (full) {
    if (auto* id_base = dynamic_cast<RelIDApplBase*>(full->base.get())) {
      std::string id = id_base->id;
      if (!container_->IsVar(id)) ids.insert(id);
    }
  }

  auto* partial = dynamic_cast<RelPartialAppl*>(formula.get());
  if (partial) {
    (void)partial;
  }

  auto* conj = dynamic_cast<RelConjunction*>(formula.get());
  if (conj) {
    auto lhs_ids = CollectIDs(conj->lhs);
    auto rhs_ids = CollectIDs(conj->rhs);
    ids.insert(lhs_ids.begin(), lhs_ids.end());
    ids.insert(rhs_ids.begin(), rhs_ids.end());
  }

  auto* disj = dynamic_cast<RelDisjunction*>(formula.get());
  if (disj) {
    auto lhs_ids = CollectIDs(disj->lhs);
    auto rhs_ids = CollectIDs(disj->rhs);
    ids.insert(lhs_ids.begin(), lhs_ids.end());
    ids.insert(rhs_ids.begin(), rhs_ids.end());
  }

  auto* exists = dynamic_cast<RelExistential*>(formula.get());
  if (exists) {
    auto inner = CollectIDs(exists->formula);
    ids.insert(inner.begin(), inner.end());
  }

  auto* unop = dynamic_cast<RelNegation*>(formula.get());
  if (unop && unop->formula) {
    auto inner = CollectIDs(unop->formula);
    ids.insert(inner.begin(), inner.end());
  }

  auto* paren = dynamic_cast<RelParen*>(formula.get());
  if (paren && paren->formula) {
    auto inner = CollectIDs(paren->formula);
    ids.insert(inner.begin(), inner.end());
  }

  return ids;
}

bool RecursionVisitor::OnlyEDBsOrNonRecursiveIDBs(const std::unordered_set<std::string>& ids,
                                                  const std::string& current_q) const {
  for (const auto& id : ids) {
    if (id == current_q) return false;
    if (container_->IsEDB(id)) continue;
    if (container_->IsIDB(id)) {
      if (IsRecursiveID(id)) return false;
      continue;
    }
    return false;
  }
  return true;
}

bool RecursionVisitor::VariablesFromBindingOrQuantification(
    const std::set<std::string>& vars, const std::vector<std::shared_ptr<RelBinding>>& outer_bindings,
    const std::vector<std::shared_ptr<RelBinding>>& quant_bindings) const {
  std::set<std::string> binding_vars;
  for (const auto& b : outer_bindings) {
    if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) {
      binding_vars.insert(vb->id);
    }
  }
  std::set<std::string> quant_vars;
  for (const auto& b : quant_bindings) {
    if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) {
      quant_vars.insert(vb->id);
    }
  }
  for (const auto& var : vars) {
    if (!binding_vars.count(var) && !quant_vars.count(var)) return false;
  }
  return true;
}

bool RecursionVisitor::CheckRecursionPattern(const std::shared_ptr<RelFormula>& formula,
                                             const std::vector<std::shared_ptr<RelBinding>>& bindings,
                                             RecursionPatternMatch& match) {
  match.base_disjuncts.clear();
  match.recursive_disjuncts.clear();
  if (!formula) return false;

  auto* disj = dynamic_cast<RelDisjunction*>(formula.get());
  if (!disj) {
    auto exists = std::dynamic_pointer_cast<RelExistential>(formula);
    if (exists) {
      RecursiveBranchMatch branch;
      if (CheckExistsPattern(exists, current_q_, bindings, branch)) {
        match.recursive_disjuncts.push_back(branch);
        return true;
      }
    }
    return false;
  }

  std::vector<std::shared_ptr<RelFormula>> disjuncts;
  CollectOrDisjuncts(formula, disjuncts);

  std::vector<std::shared_ptr<RelFormula>> g_parts;
  std::vector<std::shared_ptr<RelExistential>> exists_parts;

  for (const auto& d : disjuncts) {
    auto exists = std::dynamic_pointer_cast<RelExistential>(d);
    if (exists) {
      exists_parts.push_back(exists);
    } else {
      g_parts.push_back(d);
    }
  }

  if (g_parts.empty() || exists_parts.empty()) return false;

  for (const auto& g : g_parts) {
    auto g_ids = CollectIDs(g);
    if (g_ids.count(current_q_)) return false;
    if (!OnlyEDBsOrNonRecursiveIDBs(g_ids, current_q_)) return false;
    match.base_disjuncts.push_back(g);
  }

  for (const auto& exists : exists_parts) {
    RecursiveBranchMatch branch;
    if (!CheckExistsPattern(exists, current_q_, bindings, branch)) return false;
    match.recursive_disjuncts.push_back(branch);
  }

  if (match.base_disjuncts.empty() || match.recursive_disjuncts.empty()) return false;
  return true;
}

bool RecursionVisitor::CheckExistsPattern(const std::shared_ptr<RelExistential>& exists, const std::string& q,
                                          const std::vector<std::shared_ptr<RelBinding>>& outer_bindings,
                                          RecursiveBranchMatch& match) {
  if (!exists || !exists->formula) return false;

  std::shared_ptr<RelFullAppl> q_call;
  std::shared_ptr<RelFormula> f_part;
  FindAndPatternParts(exists->formula, q, q_call, f_part);

  if (!q_call || !f_part) return false;

  auto f_ids = CollectIDs(f_part);
  if (f_ids.count(q)) return false;
  if (!OnlyEDBsOrNonRecursiveIDBs(f_ids, q)) return false;

  std::set<std::string> w_vars;
  for (const auto& param : q_call->params) {
    auto expr = param ? param->GetExpr() : nullptr;
    if (expr) {
      auto* term_expr = dynamic_cast<RelTermExpr*>(expr.get());
      if (term_expr && term_expr->term) {
        auto* id_term = dynamic_cast<RelIDTerm*>(term_expr->term.get());
        if (id_term && container_->IsVar(id_term->id)) {
          w_vars.insert(id_term->id);
        }
      }
    }
  }
  if (!VariablesFromBindingOrQuantification(w_vars, outer_bindings, exists->bindings)) {
    return false;
  }

  std::set<std::string> v_vars = f_part->free_variables;
  if (!VariablesFromBindingOrQuantification(v_vars, outer_bindings, exists->bindings)) {
    return false;
  }

  match.exists_clause = exists;
  match.recursive_call = q_call;
  match.residual_formula = f_part;
  return true;
}

bool RecursionVisitor::IsCallToQ(const RelFullAppl& appl, const std::string& q) const {
  auto* id_base = dynamic_cast<RelIDApplBase*>(appl.base.get());
  return id_base && id_base->id == q;
}

void RecursionVisitor::CollectOrDisjuncts(const std::shared_ptr<RelFormula>& formula,
                                          std::vector<std::shared_ptr<RelFormula>>& disjuncts) const {
  if (!formula) return;
  auto* disj = dynamic_cast<RelDisjunction*>(formula.get());
  if (disj) {
    CollectOrDisjuncts(disj->lhs, disjuncts);
    CollectOrDisjuncts(disj->rhs, disjuncts);
    return;
  }
  disjuncts.push_back(formula);
}

void RecursionVisitor::FindAndPatternParts(const std::shared_ptr<RelFormula>& formula, const std::string& q,
                                           std::shared_ptr<RelFullAppl>& q_call,
                                           std::shared_ptr<RelFormula>& f_part) const {
  if (!formula) return;
  auto* conj = dynamic_cast<RelConjunction*>(formula.get());
  if (conj) {
    FindAndPatternParts(conj->lhs, q, q_call, f_part);
    FindAndPatternParts(conj->rhs, q, q_call, f_part);
    return;
  }
  auto* full = dynamic_cast<RelFullAppl*>(formula.get());
  if (full && IsCallToQ(*full, q)) {
    if (!q_call) q_call = std::dynamic_pointer_cast<RelFullAppl>(formula);
    return;
  }
  if (!f_part) f_part = formula;
}

}  // namespace rel2sql

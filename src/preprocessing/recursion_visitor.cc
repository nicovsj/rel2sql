#include "preprocessing/recursion_visitor.h"

#include <stack>

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

std::shared_ptr<RelUnion> RecursionVisitor::Visit(const std::shared_ptr<RelUnion>& node) {
  if (node->exprs.size() != 1) return node;
  auto bf = std::dynamic_pointer_cast<RelFormulaAbstraction>(node->exprs[0]);
  if (!bf) return node;
  Visit(bf);
  if (bf->is_recursive) {
    node->is_recursive = true;
    node->recursive_definition_name = current_q_;
  }
  return node;
}

std::shared_ptr<RelExpr> RecursionVisitor::Visit(const std::shared_ptr<RelFormulaAbstraction>& node) {
  RecursionPatternMatch match;
  if (!CheckRecursionPattern(node->formula, node->bindings, match)) return node;

  node->is_recursive = true;
  node->recursive_definition_name = current_q_;

  if (node->formula) {
    RegisterUnionSafetyDisjunctions(node->formula);
  }

  if (current_q_.empty()) return node;

  for (const auto& g : match.base_disjuncts) {
    std::vector<std::shared_ptr<RelExpr>> exprs = {g};
    auto rel_abs = std::make_shared<RelUnion>(std::move(exprs));
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

std::shared_ptr<RelFormula> RecursionVisitor::Visit(const std::shared_ptr<RelFullApplication>& node) {
  if (auto abs_base = std::dynamic_pointer_cast<RelExprApplBase>(node->base)) {
    if (abs_base->expr) Visit(abs_base->expr);
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

  auto* full = dynamic_cast<RelFullApplication*>(formula.get());
  if (full) {
    if (auto* id_base = dynamic_cast<RelIDApplBase*>(full->base.get())) {
      std::string id = id_base->id;
      if (!container_->IsVar(id)) ids.insert(id);
    }
  }

  auto* partial = dynamic_cast<RelPartialApplication*>(formula.get());
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

  std::shared_ptr<RelFormula> f = formula;
  while (auto p = std::dynamic_pointer_cast<RelParen>(f)) {
    f = p->formula;
  }

  auto* disj = dynamic_cast<RelDisjunction*>(f.get());
  if (!disj) {
    auto exists = std::dynamic_pointer_cast<RelExistential>(f);
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
  CollectOrDisjuncts(f, disjuncts);

  for (const auto& d : disjuncts) {
    const int nq = CountCallsToRelation(d, current_q_);
    if (nq == 0) {
      if (std::dynamic_pointer_cast<RelExistential>(d)) {
        return false;
      }
      auto g_ids = CollectIDs(d);
      if (g_ids.count(current_q_)) return false;
      if (!OnlyEDBsOrNonRecursiveIDBs(g_ids, current_q_)) return false;
      match.base_disjuncts.push_back(d);
    } else if (auto exists = std::dynamic_pointer_cast<RelExistential>(d)) {
      RecursiveBranchMatch branch;
      if (!CheckExistsPattern(exists, current_q_, bindings, branch)) return false;
      match.recursive_disjuncts.push_back(branch);
    } else {
      RecursiveBranchMatch branch;
      if (!CheckFlatRecursiveBranch(d, current_q_, bindings, branch)) return false;
      match.recursive_disjuncts.push_back(branch);
    }
  }

  if (match.base_disjuncts.empty() || match.recursive_disjuncts.empty()) return false;
  return true;
}

int RecursionVisitor::CountCallsToRelation(const std::shared_ptr<RelFormula>& formula,
                                            const std::string& id) const {
  if (!formula) return 0;
  int n = 0;
  if (auto* full = dynamic_cast<RelFullApplication*>(formula.get())) {
    if (auto* id_base = dynamic_cast<RelIDApplBase*>(full->base.get())) {
      if (id_base->id == id) n++;
    }
  }
  if (auto* conj = dynamic_cast<RelConjunction*>(formula.get())) {
    n += CountCallsToRelation(conj->lhs, id);
    n += CountCallsToRelation(conj->rhs, id);
  }
  if (auto* disj = dynamic_cast<RelDisjunction*>(formula.get())) {
    n += CountCallsToRelation(disj->lhs, id);
    n += CountCallsToRelation(disj->rhs, id);
  }
  if (auto* exists = dynamic_cast<RelExistential*>(formula.get())) {
    n += CountCallsToRelation(exists->formula, id);
  }
  if (auto* univ = dynamic_cast<RelUniversal*>(formula.get())) {
    n += CountCallsToRelation(univ->formula, id);
  }
  if (auto* unop = dynamic_cast<RelNegation*>(formula.get())) {
    n += CountCallsToRelation(unop->formula, id);
  }
  if (auto* paren = dynamic_cast<RelParen*>(formula.get())) {
    n += CountCallsToRelation(paren->formula, id);
  }
  return n;
}

bool RecursionVisitor::CheckFlatRecursiveBranch(const std::shared_ptr<RelFormula>& branch, const std::string& q,
                                                const std::vector<std::shared_ptr<RelBinding>>& outer_bindings,
                                                RecursiveBranchMatch& match) {
  if (CountCallsToRelation(branch, q) != 1) return false;

  std::shared_ptr<RelFullApplication> q_call;
  std::shared_ptr<RelFormula> f_part;
  FindAndPatternParts(branch, q, q_call, f_part);

  if (!q_call || !f_part) return false;
  if (CountCallsToRelation(f_part, q) != 0) return false;

  auto f_ids = CollectIDs(f_part);
  if (f_ids.count(q)) return false;
  if (!OnlyEDBsOrNonRecursiveIDBs(f_ids, q)) return false;

  std::set<std::string> outer_names;
  for (const auto& b : outer_bindings) {
    if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) {
      outer_names.insert(vb->id);
    }
  }

  std::set<std::string> w_vars;
  for (const auto& param : q_call->params) {
    auto expr = param ? param->GetExpr() : nullptr;
    if (expr) {
      auto* term = dynamic_cast<RelIDTerm*>(expr.get());
      if (term) {
        w_vars.insert(term->id);
      }
    }
  }
  // Q(...) arguments may use head binders and/or variables bound by the residual F2 in the same branch.
  for (const auto& w : w_vars) {
    if (!outer_names.count(w) && !f_part->free_variables.count(w)) {
      return false;
    }
  }

  std::set<std::string> allow_fv = outer_names;
  allow_fv.insert(w_vars.begin(), w_vars.end());
  for (const auto& v : f_part->free_variables) {
    if (!allow_fv.count(v)) {
      return false;
    }
  }

  match.exists_clause = nullptr;
  match.recursive_call = q_call;
  match.residual_formula = f_part;
  return true;
}

bool RecursionVisitor::CheckExistsPattern(const std::shared_ptr<RelExistential>& exists, const std::string& q,
                                          const std::vector<std::shared_ptr<RelBinding>>& outer_bindings,
                                          RecursiveBranchMatch& match) {
  if (!exists || !exists->formula) return false;

  std::shared_ptr<RelFullApplication> q_call;
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
      auto* term = dynamic_cast<RelIDTerm*>(expr.get());
      if (term && container_->IsVar(term->id)) {
        w_vars.insert(term->id);
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

bool RecursionVisitor::IsCallToQ(const RelFullApplication& appl, const std::string& q) const {
  auto* id_base = dynamic_cast<RelIDApplBase*>(appl.base.get());
  return id_base && id_base->id == q;
}

void RecursionVisitor::CollectOrDisjuncts(const std::shared_ptr<RelFormula>& formula,
                                          std::vector<std::shared_ptr<RelFormula>>& disjuncts) const {
  if (!formula) return;
  if (auto* paren = dynamic_cast<RelParen*>(formula.get())) {
    CollectOrDisjuncts(paren->formula, disjuncts);
    return;
  }
  auto* disj = dynamic_cast<RelDisjunction*>(formula.get());
  if (disj) {
    CollectOrDisjuncts(disj->lhs, disjuncts);
    CollectOrDisjuncts(disj->rhs, disjuncts);
    return;
  }
  disjuncts.push_back(formula);
}

void RecursionVisitor::RegisterUnionSafetyDisjunctions(const std::shared_ptr<RelFormula>& formula) {
  if (!formula) return;
  if (auto d = std::dynamic_pointer_cast<RelDisjunction>(formula)) {
    d->use_union_branch_safety = true;
    RegisterUnionSafetyDisjunctions(d->lhs);
    RegisterUnionSafetyDisjunctions(d->rhs);
    return;
  }
  if (auto c = std::dynamic_pointer_cast<RelConjunction>(formula)) {
    RegisterUnionSafetyDisjunctions(c->lhs);
    RegisterUnionSafetyDisjunctions(c->rhs);
    return;
  }
  if (auto n = std::dynamic_pointer_cast<RelNegation>(formula)) {
    RegisterUnionSafetyDisjunctions(n->formula);
    return;
  }
  if (auto p = std::dynamic_pointer_cast<RelParen>(formula)) {
    RegisterUnionSafetyDisjunctions(p->formula);
    return;
  }
  if (auto e = std::dynamic_pointer_cast<RelExistential>(formula)) {
    RegisterUnionSafetyDisjunctions(e->formula);
    return;
  }
  if (auto u = std::dynamic_pointer_cast<RelUniversal>(formula)) {
    RegisterUnionSafetyDisjunctions(u->formula);
    return;
  }
}

void RecursionVisitor::FindAndPatternParts(const std::shared_ptr<RelFormula>& formula, const std::string& q,
                                           std::shared_ptr<RelFullApplication>& q_call,
                                           std::shared_ptr<RelFormula>& f_part) const {
  if (!formula) return;
  if (auto* paren = dynamic_cast<RelParen*>(formula.get())) {
    FindAndPatternParts(paren->formula, q, q_call, f_part);
    return;
  }
  auto* conj = dynamic_cast<RelConjunction*>(formula.get());
  if (conj) {
    FindAndPatternParts(conj->lhs, q, q_call, f_part);
    FindAndPatternParts(conj->rhs, q, q_call, f_part);
    return;
  }
  auto* full = dynamic_cast<RelFullApplication*>(formula.get());
  if (full && IsCallToQ(*full, q)) {
    if (!q_call) q_call = std::dynamic_pointer_cast<RelFullApplication>(formula);
    return;
  }
  if (!f_part) f_part = formula;
}

}  // namespace rel2sql

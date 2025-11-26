#include "recursion_visitor.h"

#include <stack>
#include <unordered_set>

using StringSet = std::unordered_set<std::string>;

namespace rel2sql {

RecursionVisitor::RecursionVisitor(std::shared_ptr<RelAST> extended_ast) : BaseVisitor(extended_ast) {}

std::any RecursionVisitor::visitProgram(psr::ProgramContext* ctx) {
  for (auto& child_ctx : ctx->relDef()) {
    visit(child_ctx);
  }
  return {};
}

std::any RecursionVisitor::visitRelDef(psr::RelDefContext* ctx) {
  std::string q = ctx->T_ID()->getText();
  current_q_ = q;
  visit(ctx->relAbs());
  current_q_.clear();
  return {};
}

std::any RecursionVisitor::visitRelAbs(psr::RelAbsContext* ctx) {
  // RelAbs should have exactly one expression that is a BindingsFormula
  if (ctx->expr().size() != 1) {
    return {};
  }

  auto expr_ctx = ctx->expr()[0];
  auto bindings_formula = dynamic_cast<psr::BindingsFormulaContext*>(expr_ctx);
  if (!bindings_formula) {
    return {};
  }

  visit(bindings_formula);

  // If the BindingsFormula is recursable, also mark the RelAbs as recursable
  if (GetNode(bindings_formula)->is_recursive) {
    GetNode(ctx)->is_recursive = true;
    GetNode(ctx)->recursive_definition_name = current_q_;
  }

  return {};
}

std::any RecursionVisitor::visitBindingsFormula(psr::BindingsFormulaContext* ctx) {
  // Extract binding B and formula
  auto binding_ctx = ctx->bindingInner();
  auto formula_ctx = ctx->formula();

  RecursionPatternMatch pattern_match;

  // Check if formula matches the recursable pattern
  if (CheckRecursionPattern(formula_ctx, binding_ctx, pattern_match)) {
    GetNode(ctx)->is_recursive = true;
    GetNode(ctx)->recursive_definition_name = current_q_;

    if (!current_q_.empty()) {
      for (auto* base_ctx : pattern_match.base_disjuncts) {
        ast_->RegisterRecursiveBaseDisjunct(current_q_, base_ctx);
      }

      for (const auto& branch : pattern_match.recursive_disjuncts) {
        RecursiveBranchInfo info;
        info.exists_clause = GetNode(branch.exists_ctx);
        info.recursive_call = GetNode(branch.recursive_call);
        info.residual_formula = GetNode(branch.residual_formula);
        ast_->RegisterRecursiveBranch(current_q_, info);
      }
    }
  }

  return {};
}

std::any RecursionVisitor::visitBinOp(psr::BinOpContext* ctx) {
  visit(ctx->lhs);
  visit(ctx->rhs);
  return {};
}

std::any RecursionVisitor::visitQuantification(psr::QuantificationContext* ctx) {
  visit(ctx->formula());
  return {};
}

std::any RecursionVisitor::visitFullAppl(psr::FullApplContext* ctx) {
  // Visit children to collect information
  visit(ctx->applBase());
  if (ctx->applParams()) {
    visit(ctx->applParams());
  }
  return {};
}

bool RecursionVisitor::IsRecursiveID(const std::string& id) const {
  // Check if ID appears in a cycle in the dependency graph
  // Use DFS to detect cycles
  std::unordered_set<std::string> visited;
  std::unordered_set<std::string> rec_stack;
  std::stack<std::string> stack;

  stack.push(id);
  rec_stack.insert(id);

  while (!stack.empty()) {
    std::string current = stack.top();
    stack.pop();

    if (visited.find(current) != visited.end()) {
      continue;
    }

    visited.insert(current);

    auto relation_info = ast_->GetRelationInfo(current);


    if (relation_info != std::nullopt) {
      for (const auto& dep : relation_info->dependencies) {
        if (dep == id) {
          return true;  // Found cycle back to id
        }
        if (visited.find(dep) == visited.end() && rec_stack.find(dep) == rec_stack.end()) {
          stack.push(dep);
          rec_stack.insert(dep);
        }
      }
    }
  }

  return false;
}

std::unordered_set<std::string> RecursionVisitor::CollectIDs(antlr4::ParserRuleContext* ctx) const {
  // Collect IDs referenced in the context (not variables)
  // We look for FullAppl nodes (calls to relations) and extract the ID from applBase
  StringSet ids;

  if (!ctx) {
    return ids;
  }

  // Traverse the parse tree and look for FullAppl nodes
  std::function<void(antlr4::tree::ParseTree*)> collect = [&](antlr4::tree::ParseTree* tree) {
    if (!tree) return;

    // Check if this is a FullAppl - these are calls to relations
    auto full_appl = dynamic_cast<psr::FullApplContext*>(tree);
    if (full_appl && full_appl->applBase() && full_appl->applBase()->T_ID()) {
      std::string id = full_appl->applBase()->T_ID()->getText();
      // Only add if it's not a variable
      if (ast_->IsVar(id)) {
        ids.insert(id);
      }
    }

    // Also check for PartialAppl
    auto partial_appl = dynamic_cast<psr::PartialApplContext*>(tree);
    if (partial_appl && partial_appl->applBase() && partial_appl->applBase()->T_ID()) {
      std::string id = partial_appl->applBase()->T_ID()->getText();
      if (ast_->IsVar(id)) {
        ids.insert(id);
      }
    }

    // Recursively visit children
    for (size_t i = 0; i < tree->children.size(); i++) {
      collect(tree->children[i]);
    }
  };

  collect(ctx);

  return ids;
}

bool RecursionVisitor::OnlyEDBsOrNonRecursiveIDBs(const std::unordered_set<std::string>& ids,
                                                  const std::string& current_q) const {
  for (const auto& id : ids) {
    // Skip current_q
    if (id == current_q) {
      return false;
    }

    // Check if it's an EDB
    if (ast_->IsEDB(id)) continue;

    // Check if it's a non-recursive IDB
    if (ast_->IsIDB(id)) {
      if (IsRecursiveID(id)) return false;  // Recursive IDB is not allowed

      continue;  // Non-recursive IDB is OK
    }

    // Unknown ID type
    return false;
  }

  return true;
}

bool RecursionVisitor::VariablesFromBindingOrQuantification(const std::set<std::string>& vars,
                                                            psr::BindingInnerContext* binding_ctx,
                                                            psr::BindingInnerContext* quant_binding_ctx) const {
  // Collect variables from binding B
  std::set<std::string> binding_vars;
  if (binding_ctx) {
    for (auto& binding : binding_ctx->binding()) {
      if (binding->id) {
        binding_vars.insert(binding->id->getText());
      }
    }
  }

  // Collect variables from quantification binding u
  std::set<std::string> quant_vars;
  if (quant_binding_ctx) {
    for (auto& binding : quant_binding_ctx->binding()) {
      if (binding->id) {
        quant_vars.insert(binding->id->getText());
      }
    }
  }

  // Check if all vars are from binding or quantification
  for (const auto& var : vars) {
    if (binding_vars.find(var) == binding_vars.end() && quant_vars.find(var) == quant_vars.end()) {
      return false;
    }
  }

  return true;
}

bool RecursionVisitor::CheckRecursionPattern(psr::FormulaContext* formula_ctx, psr::BindingInnerContext* binding_ctx,
                                             RecursionPatternMatch& match) {
  match.base_disjuncts.clear();
  match.recursive_disjuncts.clear();

  if (!formula_ctx) {
    return false;
  }

  // Formula should be: G or exists(...) or exists(...)
  // We need to check if it's a disjunction (or) with:
  // - One part G that doesn't refer to Q
  // - One or more exists parts

  // Check if it's a BinOp with 'or'
  auto bin_op = dynamic_cast<psr::BinOpContext*>(formula_ctx);
  if (!bin_op || !bin_op->K_or()) {
    // If it's not a BinOp with 'or', check if it's a single exists
    auto quant = dynamic_cast<psr::QuantificationContext*>(formula_ctx);
    if (quant && quant->K_exists()) {
      RecursiveBranchMatch branch;
      if (CheckExistsPattern(quant, current_q_, binding_ctx, branch)) {
        match.recursive_disjuncts.push_back(branch);
        return true;
      }
    }
    return false;
  }

  // It's an 'or' operation, collect all disjuncts
  std::vector<psr::FormulaContext*> disjuncts;
  CollectOrDisjuncts(formula_ctx, disjuncts);

  // Separate into G (non-exists) and exists parts
  std::vector<psr::FormulaContext*> g_parts;
  std::vector<psr::QuantificationContext*> exists_parts;

  for (auto* disjunct : disjuncts) {
    auto quant = dynamic_cast<psr::QuantificationContext*>(disjunct);
    if (quant && quant->K_exists()) {
      exists_parts.push_back(quant);
    } else {
      g_parts.push_back(disjunct);
    }
  }

  // Must have at least one G part and at least one exists part
  if (g_parts.empty() || exists_parts.empty()) {
    return false;
  }

  // Check G parts: they should not refer to Q and only refer to EDBs or non-recursive IDBs
  for (auto* g : g_parts) {
    auto g_ids = CollectIDs(g);
    if (g_ids.find(current_q_) != g_ids.end()) {
      return false;  // G refers to Q
    }
    if (!OnlyEDBsOrNonRecursiveIDBs(g_ids, current_q_)) {
      return false;  // G refers to something other than EDBs or non-recursive IDBs
    }
    match.base_disjuncts.push_back(g);
  }

  // Check each exists part
  for (auto* exists : exists_parts) {
    RecursiveBranchMatch branch;
    if (!CheckExistsPattern(exists, current_q_, binding_ctx, branch)) {
      return false;
    }
    match.recursive_disjuncts.push_back(branch);
  }

  if (match.base_disjuncts.empty() || match.recursive_disjuncts.empty()) {
    return false;
  }

  return true;
}

bool RecursionVisitor::CheckExistsPattern(psr::QuantificationContext* quant_ctx, const std::string& q,
                                          psr::BindingInnerContext* outer_binding_ctx, RecursiveBranchMatch& match) {
  if (!quant_ctx || !quant_ctx->formula()) {
    return false;
  }

  auto formula_ctx = quant_ctx->formula();

  // Formula should be: Q(w) and F(v)
  // Check if it's a BinOp with 'and'
  auto bin_op = dynamic_cast<psr::BinOpContext*>(formula_ctx);
  if (!bin_op || !bin_op->K_and()) {
    return false;
  }

  // Find Q(w) and F(v) parts
  psr::FullApplContext* q_call = nullptr;
  psr::FormulaContext* f_part = nullptr;
  FindAndPatternParts(formula_ctx, q, q_call, f_part);

  if (!q_call || !f_part) {
    return false;
  }

  // Check that Q(w) doesn't refer to Q (it's just a call to Q)
  // This is already satisfied since we checked IsCallToQ

  // Check that F doesn't refer to Q
  auto f_ids = CollectIDs(f_part);
  if (f_ids.find(q) != f_ids.end()) {
    return false;  // F refers to Q
  }

  // Check that F only refers to EDBs or non-recursive IDBs
  if (!OnlyEDBsOrNonRecursiveIDBs(f_ids, q)) {
    return false;
  }

  auto quant_binding_ctx = quant_ctx->bindingInner();

  // Check variables in w (parameters of Q(w))
  if (q_call->applParams()) {
    std::set<std::string> w_vars;
    for (auto& param : q_call->applParams()->applParam()) {
      if (param->expr()) {
        auto id_expr = dynamic_cast<psr::IDExprContext*>(param->expr());
        if (id_expr) {
          std::string var = id_expr->T_ID()->getText();
          if (ast_->IsVar(var)) {
            w_vars.insert(var);
          }
        }
      }
    }
    if (!VariablesFromBindingOrQuantification(w_vars, outer_binding_ctx, quant_binding_ctx)) {
      return false;
    }
  }

  // Check variables in v (free variables in F)
  auto f_node = GetNode(f_part);
  std::set<std::string> v_vars = f_node->free_variables;
  if (!VariablesFromBindingOrQuantification(v_vars, outer_binding_ctx, quant_binding_ctx)) {
    return false;
  }

  match.exists_ctx = quant_ctx;
  match.recursive_call = q_call;
  match.residual_formula = f_part;

  return true;
}

bool RecursionVisitor::IsCallToQ(psr::FullApplContext* ctx, const std::string& q) const {
  if (!ctx->applBase()->T_ID()) {
    return false;
  }

  std::string id = ctx->applBase()->T_ID()->getText();
  return id == q;
}

void RecursionVisitor::CollectOrDisjuncts(psr::FormulaContext* formula_ctx,
                                          std::vector<psr::FormulaContext*>& disjuncts) const {
  if (!formula_ctx) return;

  auto bin_op = dynamic_cast<psr::BinOpContext*>(formula_ctx);
  if (bin_op && bin_op->K_or()) {
    CollectOrDisjuncts(bin_op->lhs, disjuncts);
    CollectOrDisjuncts(bin_op->rhs, disjuncts);
    return;
  }

  disjuncts.push_back(formula_ctx);
}

void RecursionVisitor::FindAndPatternParts(psr::FormulaContext* formula_ctx, const std::string& q,
                                           psr::FullApplContext*& q_call, psr::FormulaContext*& f_part) const {
  if (!formula_ctx) return;

  auto bin_op = dynamic_cast<psr::BinOpContext*>(formula_ctx);
  if (bin_op && bin_op->K_and()) {
    FindAndPatternParts(bin_op->lhs, q, q_call, f_part);
    FindAndPatternParts(bin_op->rhs, q, q_call, f_part);
    return;
  }

  auto full_appl = dynamic_cast<psr::FullApplContext*>(formula_ctx);
  if (full_appl && IsCallToQ(full_appl, q)) {
    if (!q_call) {
      q_call = full_appl;
    }
    return;
  }

  if (!f_part) {
    f_part = formula_ctx;
  }
}

}  // namespace rel2sql

#include "rewriter/underscore_rewriter.h"

#include <memory>
#include <vector>

#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_context.h"

namespace rel2sql {

std::string UnderscoreRewriter::FreshVarName() {
  return std::format("_z{}", fresh_var_counter_++);
}

std::shared_ptr<RelApplParam> UnderscoreRewriter::MakeVarParam(const std::string& var) {
  auto id_term = std::make_shared<RelIDTerm>(var);
  auto term_expr = std::make_shared<RelTermExpr>(std::move(id_term));
  return std::make_shared<RelExprApplParam>(std::move(term_expr));
}

int UnderscoreRewriter::GetRelationArity(const std::string& id) const {
  if (!container_) return 0;
  return container_->GetArity(id);
}

void UnderscoreRewriter::Visit(RelFullAppl& node) {
  BaseRelRewriter::Visit(node);

  std::vector<int> underscore_positions;
  for (size_t i = 0; i < node.params.size(); i++) {
    if (node.params[i] && node.params[i]->IsUnderscore()) {
      underscore_positions.push_back(static_cast<int>(i));
    }
  }
  if (underscore_positions.empty()) return;

  std::vector<std::string> fresh_vars;
  for (size_t i = 0; i < underscore_positions.size(); i++) {
    fresh_vars.push_back(FreshVarName());
  }

  std::vector<std::shared_ptr<RelApplParam>> new_params;
  int uv_idx = 0;
  for (size_t i = 0; i < node.params.size(); i++) {
    if (node.params[i] && node.params[i]->IsUnderscore()) {
      new_params.push_back(MakeVarParam(fresh_vars[uv_idx++]));
    } else {
      new_params.push_back(node.params[i]);
    }
  }

  std::vector<std::shared_ptr<RelBinding>> bindings;
  for (const auto& v : fresh_vars) {
    bindings.push_back(std::make_shared<RelVarBinding>(v, std::nullopt));
  }

  auto new_appl = std::make_shared<RelFullAppl>(node.base, std::move(new_params));
  auto quant = std::make_shared<RelQuantification>(
      RelQuantOp::EXISTS, std::move(bindings), std::move(new_appl));

  SetFormulaReplacement(std::move(quant));
}

void UnderscoreRewriter::Visit(RelPartialAppl& node) {
  BaseRelRewriter::Visit(node);

  int underscore_pos = -1;
  for (size_t i = 0; i < node.params.size(); i++) {
    if (node.params[i] && node.params[i]->IsUnderscore()) {
      if (underscore_pos >= 0) {
        return;
      }
      underscore_pos = static_cast<int>(i);
    }
  }
  if (underscore_pos < 0) return;

  auto* id_base = dynamic_cast<RelIDApplBase*>(node.base.get());
  if (!id_base) return;

  int rel_arity = GetRelationArity(id_base->id);
  if (rel_arity <= 0 || static_cast<size_t>(rel_arity) <= node.params.size()) {
    return;
  }

  std::string z = FreshVarName();
  std::vector<std::string> rest_vars;
  size_t rest_count = static_cast<size_t>(rel_arity) - node.params.size();
  for (size_t i = 0; i < rest_count; i++) {
    rest_vars.push_back(FreshVarName());
  }

  std::vector<std::shared_ptr<RelApplParam>> full_params;
  for (size_t i = 0; i < node.params.size(); i++) {
    if (node.params[i] && node.params[i]->IsUnderscore()) {
      full_params.push_back(MakeVarParam(z));
    } else {
      full_params.push_back(node.params[i]);
    }
  }
  for (const auto& v : rest_vars) {
    full_params.push_back(MakeVarParam(v));
  }

  auto full_appl = std::make_shared<RelFullAppl>(node.base, std::move(full_params));

  std::vector<std::shared_ptr<RelBinding>> bindings;
  bindings.push_back(std::make_shared<RelVarBinding>(z, std::nullopt));

  auto exists_formula = std::make_shared<RelQuantification>(
      RelQuantOp::EXISTS, std::move(bindings), std::move(full_appl));

  // (zk+1, ..., z|A|) : exists((z) | A(...))  via RelBindingsFormula
  std::vector<std::shared_ptr<RelBinding>> output_bindings;
  for (const auto& v : rest_vars) {
    output_bindings.push_back(std::make_shared<RelVarBinding>(v, std::nullopt));
  }
  auto bindings_formula =
      std::make_shared<RelBindingsFormula>(std::move(output_bindings), std::move(exists_formula));

  SetExprReplacement(std::move(bindings_formula));
}

}  // namespace rel2sql

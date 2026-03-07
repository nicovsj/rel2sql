#include "rewriter/wildcard_rewriter.h"

#include <memory>
#include <vector>

#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_context_builder.h"

namespace rel2sql {

std::string WildcardRewriter::FreshVarName() { return std::format("_z{}", fresh_var_counter_++); }

std::shared_ptr<RelApplParam> WildcardRewriter::MakeVarParam(const std::string& var) {
  auto id_term = std::make_shared<RelIDTerm>(var);
  return std::make_shared<RelExprApplParam>(std::move(id_term));
}

int WildcardRewriter::GetRelationArity(const std::string& id) const {
  if (!container_) return 0;
  return container_->GetArity(id);
}

std::shared_ptr<RelFormula> WildcardRewriter::Visit(const std::shared_ptr<RelFullApplication>& node) {
  auto result = std::dynamic_pointer_cast<RelFullApplication>(BaseRelVisitor::Visit(node));
  if (!result) return result;

  std::vector<int> underscore_positions;
  for (size_t i = 0; i < result->params.size(); i++) {
    if (result->params[i] && result->params[i]->IsUnderscore()) {
      underscore_positions.push_back(static_cast<int>(i));
    }
  }
  if (underscore_positions.empty()) return result;

  std::vector<std::string> fresh_vars;
  for (size_t i = 0; i < underscore_positions.size(); i++) {
    fresh_vars.push_back(FreshVarName());
  }

  std::vector<std::shared_ptr<RelApplParam>> new_params;
  int uv_idx = 0;
  for (size_t i = 0; i < result->params.size(); i++) {
    if (result->params[i] && result->params[i]->IsUnderscore()) {
      new_params.push_back(MakeVarParam(fresh_vars[uv_idx++]));
    } else {
      new_params.push_back(result->params[i]);
    }
  }

  std::vector<std::shared_ptr<RelBinding>> bindings;
  for (const auto& v : fresh_vars) {
    bindings.push_back(std::make_shared<RelVarBinding>(v, std::nullopt));
  }

  auto new_appl = std::make_shared<RelFullApplication>(result->base, std::move(new_params));
  return std::make_shared<RelExistential>(std::move(bindings), std::move(new_appl));
}

std::shared_ptr<RelExpr> WildcardRewriter::Visit(const std::shared_ptr<RelPartialApplication>& node) {
  auto result = std::dynamic_pointer_cast<RelPartialApplication>(BaseRelVisitor::Visit(node));
  if (!result) return result;

  int underscore_pos = -1;
  for (size_t i = 0; i < result->params.size(); i++) {
    if (result->params[i] && result->params[i]->IsUnderscore()) {
      if (underscore_pos >= 0) return result;
      underscore_pos = static_cast<int>(i);
    }
  }
  if (underscore_pos < 0) return result;

  auto* id_base = dynamic_cast<RelIDApplBase*>(result->base.get());
  if (!id_base) return result;

  int rel_arity = GetRelationArity(id_base->id);
  if (rel_arity <= 0 || static_cast<size_t>(rel_arity) <= result->params.size()) {
    return result;
  }

  std::string z = FreshVarName();
  std::vector<std::string> rest_vars;
  size_t rest_count = static_cast<size_t>(rel_arity) - result->params.size();
  for (size_t i = 0; i < rest_count; i++) {
    rest_vars.push_back(FreshVarName());
  }

  std::vector<std::shared_ptr<RelApplParam>> full_params;
  for (size_t i = 0; i < result->params.size(); i++) {
    if (result->params[i] && result->params[i]->IsUnderscore()) {
      full_params.push_back(MakeVarParam(z));
    } else {
      full_params.push_back(result->params[i]);
    }
  }
  for (const auto& v : rest_vars) {
    full_params.push_back(MakeVarParam(v));
  }

  auto full_appl = std::make_shared<RelFullApplication>(result->base, std::move(full_params));

  std::vector<std::shared_ptr<RelBinding>> bindings;
  bindings.push_back(std::make_shared<RelVarBinding>(z, std::nullopt));

  auto exists_formula = std::make_shared<RelExistential>(std::move(bindings), std::move(full_appl));

  std::vector<std::shared_ptr<RelBinding>> output_bindings;
  for (const auto& v : rest_vars) {
    output_bindings.push_back(std::make_shared<RelVarBinding>(v, std::nullopt));
  }
  return std::make_shared<RelFormulaAbstraction>(std::move(output_bindings), std::move(exists_formula));
}

}  // namespace rel2sql

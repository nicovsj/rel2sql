#include "safe_visitor.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "preprocessing/fixpoint_safety_visitor.h"
#include "support/exceptions.h"

namespace rel2sql {

SafeVisitor::SafeVisitor(std::shared_ptr<RelAST> ast) : BaseVisitor(ast) {}

std::any SafeVisitor::visitProgram(psr::ProgramContext* ctx) {
  std::unordered_map<std::string, std::vector<psr::RelDefContext*>> defs_by_id;
  for (auto* rel_def : ctx->relDef()) {
    defs_by_id[rel_def->name->getText()].push_back(rel_def);
  }

  std::unordered_set<std::string> visited;
  for (const auto& id : ast_->SortedIDs()) {
    if (!defs_by_id.contains(id)) continue;
    for (auto* rel_def : defs_by_id[id]) {
      visit(rel_def);
    }
    visited.insert(id);
  }

  for (auto* rel_def : ctx->relDef()) {
    if (visited.contains(rel_def->name->getText())) continue;
    visit(rel_def);
  }

  return {};
}

std::any SafeVisitor::visitRelDef(psr::RelDefContext* ctx) {
  current_relation_ = ctx->name->getText();

  // Check if this relation has recursion metadata (is recursive)
  auto relation_info = ast_->GetRelationInfo(current_relation_);
  if (relation_info && relation_info->HasRecursionMetadata()) {
    // Find the BindingsFormulaContext in the relAbs
    auto rel_abs = ctx->relAbs();
    if (rel_abs && rel_abs->expr().size() == 1) {
      auto expr = rel_abs->expr()[0];
      auto bindings_formula = dynamic_cast<psr::BindingsFormulaContext*>(expr);
      if (bindings_formula) {
        // Use fixpoint algorithm for recursive definitions
        FixpointSafetyVisitor fixpoint_visitor(ast_, current_relation_);
        auto fixpoint_safety = fixpoint_visitor.ComputeFixpoint(bindings_formula);

        // The fixpoint visitor has already visited the formula, so we just need to:
        // 1. Set the safety on the bindings formula node
        auto formula_node = GetNode(bindings_formula->formula());
        formula_node->safety = fixpoint_safety;
        // 3. Visit bindings to ensure they're processed
        for (auto& binding : bindings_formula->bindingInner()->binding()) {
          visit(binding);
        }

        current_relation_.clear();
        return {};
      }
    }
  }

  // Non-recursive: use normal safety computation
  visit(ctx->relAbs());

  current_relation_.clear();

  return {};
}

std::any SafeVisitor::visitRelAbs(psr::RelAbsContext* ctx) {
  visitChildren(ctx);

  auto current_node = GetNode(ctx);

  current_node->safety = GetNode(ctx->expr()[0])->safety;

  for (size_t i = 1; i < ctx->expr().size(); i++) {
    auto& sub_safety = GetNode(ctx->expr()[i])->safety;
    current_node->safety = current_node->safety.IntersectWith(sub_safety);
  }

  return {};
}

std::any SafeVisitor::visitIDExpr(psr::IDExprContext* ctx) {
  GetNode(ctx)->safety = {};

  return {};
}

std::any SafeVisitor::visitProductExpr(psr::ProductExprContext* ctx) {
  visitChildren(ctx);

  auto current_node = GetNode(ctx);

  for (auto sub_ctx : ctx->productInner()->expr()) {
    auto& sub_safety = GetNode(sub_ctx)->safety;
    current_node->safety = current_node->safety.UnionWith(sub_safety);
  }

  return {};
}

std::any SafeVisitor::visitConditionExpr(psr::ConditionExprContext* ctx) {
  visit(ctx->expr());
  visit(ctx->formula());

  auto current_node = GetNode(ctx);

  auto expr_node = GetNode(ctx->expr());

  auto formula_node = GetNode(ctx->formula());

  current_node->safety = formula_node->safety.UnionWith(expr_node->safety);

  return {};
}

std::any SafeVisitor::visitRelAbsExpr(psr::RelAbsExprContext* ctx) {
  visitChildren(ctx);

  GetNode(ctx)->safety = GetNode(ctx->relAbs())->safety;

  return {};
}

std::any SafeVisitor::visitFormulaExpr(psr::FormulaExprContext* ctx) {
  visit(ctx->formula());

  GetNode(ctx)->safety = GetNode(ctx->formula())->safety;

  return {};
}

std::any SafeVisitor::visitBindingsExpr(psr::BindingsExprContext* ctx) {
  auto current_node = GetNode(ctx);

  visit(ctx->expr());

  auto expr_node = GetNode(ctx->expr());

  std::vector<std::string> variables;
  for (auto& binding : ctx->bindingInner()->binding()) {
    variables.push_back(binding->id->getText());
  }

  current_node->safety = expr_node->safety.WithRemovedVariables(variables);

  return {};
}

std::any SafeVisitor::visitBindingsFormula(psr::BindingsFormulaContext* ctx) {
  auto current_node = GetNode(ctx);

  visit(ctx->formula());

  auto formula_node = GetNode(ctx->formula());

  std::vector<std::string> variables;

  for (auto& binding : ctx->bindingInner()->binding()) {
    variables.push_back(binding->id->getText());
  }

  current_node->safety = formula_node->safety.WithRemovedVariables(variables);

  return {};
}

std::any SafeVisitor::visitPartialAppl(psr::PartialApplContext* ctx) {
  if (ctx->applBase()->T_ID() && AGGREGATE_MAP.find(ctx->applBase()->T_ID()->getText()) != AGGREGATE_MAP.end()) {
    auto param_ctx = *ctx->applParams()->applParam().begin();
    visit(param_ctx);

    auto node = GetNode(ctx);

    auto child_node = GetNode(param_ctx);

    node->safety = child_node->safety;

    return {};
  }

  if (ctx->applBase()->T_ID()) {
    std::vector<size_t> variable_indices;
    std::vector<std::string> variable_names;

    for (size_t i = 0; i < ctx->applParams()->applParam().size(); i++) {
      auto param = ctx->applParams()->applParam()[i];
      visit(param);

      auto node = GetNode(param);
      if (!dynamic_cast<psr::IDExprContext*>(param->expr())) continue;
      if (node->variables.size() != 1) continue;

      auto variable = *node->variables.begin();

      variable_indices.push_back(i);
      variable_names.push_back(variable);
    }

    Bound bound{variable_names};

    std::string id = ctx->applBase()->T_ID()->getText();
    auto arity = ast_->GetArity(id);

    auto table_source = TableSource(id, arity);
    auto projection = Projection(variable_indices, table_source);

    bound.Add(projection);

    GetNode(ctx)->safety = BoundSet({bound});

    return {};
  }

  visit(ctx->applBase());

  return {};
}

std::any SafeVisitor::visitFullAppl(psr::FullApplContext* ctx) {
  visit(ctx->applBase());

  // Base is an ID
  if (ctx->applBase()->T_ID()) {
    std::string id = ctx->applBase()->T_ID()->getText();
    ComputeFullApplicationOnIDSafety(ctx, id);
    return {};
  }

  if (ctx->applBase()->relAbs()) {
    std::vector<std::string> variable_names;
    std::vector<size_t> variable_indices;

    for (size_t i = 0; i < ctx->applParams()->applParam().size(); i++) {
      auto param = ctx->applParams()->applParam()[i];
      visit(param);
      auto node = GetNode(param);
      if (!dynamic_cast<psr::IDExprContext*>(param->expr())) continue;
      if (node->variables.size() != 1) continue;
      auto variable = *node->variables.begin();

      variable_names.push_back(variable);
      variable_indices.push_back(i);
    }

    auto node = GetNode(ctx);
    auto base_node = GetNode(ctx->applBase());

    auto promised_source = PromisedSource{base_node->arity};

    auto projection = Projection(variable_indices, promised_source);

    auto bound = Bound(variable_names, {projection});

    node->safety = BoundSet({bound});
  }

  return {};
}

void SafeVisitor::ComputeFullApplicationOnIDSafety(psr::FullApplContext* ctx, const std::string& id) {
  std::vector<std::string> variable_names;
  std::vector<size_t> variable_indices;

  for (size_t i = 0; i < ctx->applParams()->applParam().size(); i++) {
    auto param = ctx->applParams()->applParam()[i];
    visit(param);
    auto node = GetNode(param);

    if (!dynamic_cast<psr::IDExprContext*>(param->expr())) continue;
    if (node->variables.size() != 1) continue;

    auto variable = *node->variables.begin();

    variable_indices.push_back(i);
    variable_names.push_back(variable);
  }

  Bound binding_bound{variable_names};

  auto arity = ast_->GetArity(id);

  auto table_source = TableSource(id, arity);
  auto projection = Projection(variable_indices, table_source);

  binding_bound.Add(projection);

  GetNode(ctx)->safety = BoundSet({binding_bound});
}

std::any SafeVisitor::visitBinOp(psr::BinOpContext* ctx) {
  if (ctx->K_and()) {
    return VisitConjunction(ctx);
  } else if (ctx->K_or()) {
    return VisitDisjunction(ctx);
  }

  SourceLocation location = GetSourceLocation(ctx);
  throw TranslationException("Unknown binary operator", ErrorCode::UNKNOWN_BINARY_OPERATOR, location);
}

std::any SafeVisitor::VisitConjunction(psr::BinOpContext* ctx) {
  visit(ctx->lhs);
  visit(ctx->rhs);

  auto lhs_safety = GetNode(ctx->lhs)->safety;
  auto rhs_safety = GetNode(ctx->rhs)->safety;

  auto current_node = GetNode(ctx);

  current_node->safety = lhs_safety.UnionWith(rhs_safety);

  return {};
}

std::any SafeVisitor::VisitDisjunction(psr::BinOpContext* ctx) {
  visit(ctx->lhs);
  visit(ctx->rhs);

  auto lhs_safeness = GetNode(ctx->lhs)->safety;
  auto rhs_safeness = GetNode(ctx->rhs)->safety;

  auto current_node = GetNode(ctx);

  current_node->safety = lhs_safeness.MergeWith(rhs_safeness);

  return {};
}

std::any SafeVisitor::visitUnOp(psr::UnOpContext* ctx) {
  visit(ctx->formula());

  GetNode(ctx)->safety = GetNode(ctx->formula())->safety;

  return {};
}

std::any SafeVisitor::visitQuantification(psr::QuantificationContext* ctx) {
  auto current_node = GetNode(ctx);

  visit(ctx->formula());

  auto formula_node = GetNode(ctx->formula());

  std::vector<std::string> variables;
  for (auto& binding : ctx->bindingInner()->binding()) {
    variables.push_back(binding->id->getText());
  }

  current_node->safety = formula_node->safety.WithRemovedVariables(variables);

  return {};
}

std::any SafeVisitor::visitParen(psr::ParenContext* ctx) {
  visit(ctx->formula());

  GetNode(ctx)->safety = GetNode(ctx->formula())->safety;

  return {};
}

std::any SafeVisitor::visitComparison(psr::ComparisonContext* ctx) {
  visit(ctx->lhs);
  visit(ctx->rhs);

  // A comparison is safe only if it's of the form x = c or c = x,
  // where x is a variable and c is a constant.
  // TODO: Maybe we can allow for bounded variables if we have a type system?

  auto current_node = GetNode(ctx);

  // If the comparator is not an equality, the comparison is not safe
  if (!ctx->comparator()->T_OP_EQ()) return {};

  auto lhs_id_term = dynamic_cast<psr::IDTermContext*>(ctx->lhs);
  auto rhs_id_term = dynamic_cast<psr::IDTermContext*>(ctx->rhs);

  auto lhs_node = GetNode(ctx->lhs);
  auto rhs_node = GetNode(ctx->rhs);

  std::string variable_name;
  sql::ast::constant_t constant;

  if (lhs_id_term && rhs_node->constant.has_value()) {
    variable_name = lhs_id_term->T_ID()->getText();
    constant = rhs_node->constant.value();
  } else if (rhs_id_term && lhs_node->constant.has_value()) {
    variable_name = rhs_id_term->T_ID()->getText();
    constant = lhs_node->constant.value();
  } else {
    // If the equality is not between a variable and a constant, the comparison is not safe
    return {};
  }

  auto projection = Projection(ConstantSource(constant));

  Bound bound({variable_name}, {projection});

  current_node->safety = BoundSet({bound});

  return {};
}

std::any SafeVisitor::visitApplBase(psr::ApplBaseContext* ctx) {
  visitChildren(ctx);
  return {};
}

std::any SafeVisitor::visitApplParam(psr::ApplParamContext* ctx) {
  visitChildren(ctx);

  if (ctx->expr()) {
    GetNode(ctx)->safety = GetNode(ctx->expr())->safety;
  }

  return {};
}


}  // namespace rel2sql

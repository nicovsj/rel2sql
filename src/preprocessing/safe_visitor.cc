#include "safe_visitor.h"

#include "support/exceptions.h"

namespace rel2sql {

SafeVisitor::SafeVisitor(std::shared_ptr<RelAST> ast) : BaseVisitor(ast) {}

std::any SafeVisitor::visitProgram(psr::ProgramContext* ctx) {
  visitChildren(ctx);
  return {};
}

std::any SafeVisitor::visitRelDef(psr::RelDefContext* ctx) {
  visit(ctx->relAbs());

  return {};
}

std::any SafeVisitor::visitRelAbs(psr::RelAbsContext* ctx) {
  visitChildren(ctx);

  auto current_node = GetNode(ctx);

  current_node->safety = GetNode(ctx->expr()[0])->safety;

  for (int i = 1; i < ctx->expr().size(); i++) {
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

  auto formula_node = GetNode(ctx->formula());

  current_node->safety = formula_node->safety;

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
  visit(ctx->formula());

  auto formula_node = GetNode(ctx->formula());

  GetNode(ctx)->safety = {};

  for (auto& binding : ctx->bindingInner()->binding()) {
    visit(binding);
  }

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
    bool all_vars = true;
    for (auto& param : ctx->applParams()->applParam()) {
      visit(param);
      if (*GetNode(param)->variables.begin() != param->getText()) {
        all_vars = false;
      }
    }

    if (all_vars) {
      std::vector<int> indices;

      for (int i = 0; i < ctx->applParams()->applParam().size(); i++) {
        indices.push_back(i);
      }

      BindingsBound binding_bound;

      std::string id = ctx->applBase()->T_ID()->getText();
      auto arity = ast_->GetArity(id);

      auto table_source = TableSource(id, arity);
      auto projection = SourceProjection(table_source);

      binding_bound.Add(projection);

      for (auto& param : ctx->applParams()->applParam()) {
        auto variable = param->getText();

        binding_bound.variables.push_back(variable);
      }

      GetNode(ctx)->safety = BindingBoundSet({binding_bound});

      return {};
    }
  }

  visit(ctx->applBase());

  return {};
}

std::any SafeVisitor::visitFullAppl(psr::FullApplContext* ctx) {
  visit(ctx->applBase());
  for (auto& param : ctx->applParams()->applParam()) {
    visit(param);
  }

  // If the base is not an ID, the safeness is empty
  if (!ctx->applBase()->T_ID()) {
    return {};
  }

  BindingsBound binding_bound;

  std::string id = ctx->applBase()->T_ID()->getText();
  auto arity = ast_->GetArity(id);

  auto table_source = TableSource(id, arity);
  auto projection = SourceProjection(table_source);

  binding_bound.Add(projection);

  for (auto& param : ctx->applParams()->applParam()) {
    auto id_expr = dynamic_cast<psr::IDExprContext*>(param->expr());
    if (!id_expr) {
      return {};
    }

    auto variable = id_expr->T_ID()->getText();

    binding_bound.variables.push_back(variable);
  }

  GetNode(ctx)->safety = BindingBoundSet({binding_bound});

  return {};
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

  GetNode(ctx)->safety = lhs_safeness.DisjunctMergeWith(rhs_safeness);

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

  BindingsBound binding_bound;
  binding_bound.variables.push_back(variable_name);
  binding_bound.Add(SourceProjection(ConstantSource(constant)));
  current_node->safety = BindingBoundSet({binding_bound});

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


std::unordered_set<BindingsBound> SafeVisitor::MergeCompatibleProjections(
    const std::unordered_set<BindingsBound>& bindings) const {
  // TODO: Implement projection merging logic
  // For now, this is a black box that just returns the input unchanged
  // The implementation should:
  // 1. Group BindingsBound objects by their source table (if all projections come from same table)
  // 2. Find projections with disjoint indices from the same table
  // 3. Merge them, especially when they form a complete covering of the table
  // 4. Return the merged result
  return bindings;
}

}  // namespace rel2sql

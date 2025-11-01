#include "safe_visitor.h"

#include "exceptions.h"

namespace rel2sql {

SafeVisitor::SafeVisitor(std::shared_ptr<ExtendedASTData> data) : BaseVisitor(data) {}

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

  auto& current_node = GetNode(ctx);

  std::vector<std::unordered_set<BindingsBound>> sets_to_intersect;

  for (auto& expr_ctx : ctx->expr()) {
    if (!GetNode(expr_ctx).safeness.has_value()) {
      current_node.safeness = std::nullopt;
      return {};
    }
    auto& expr_safeness = GetNode(expr_ctx).safeness.value();
    sets_to_intersect.push_back(expr_safeness);
  }

  // Intersect all the sets
  current_node.safeness = utl::IntersectSets(sets_to_intersect);

  auto special_intersection = SpecialIntersectionOfBindingBounds(sets_to_intersect);

  current_node.safeness.value().insert(special_intersection.begin(), special_intersection.end());

  return {};
}

std::any SafeVisitor::visitIDExpr(psr::IDExprContext* ctx) {
  GetNode(ctx).safeness = {};

  return {};
}

std::any SafeVisitor::visitProductExpr(psr::ProductExprContext* ctx) {
  visitChildren(ctx);

  auto& current_node = GetNode(ctx);

  current_node.safeness = std::unordered_set<BindingsBound>();

  for (auto sub_ctx : ctx->productInner()->expr()) {
    if (!GetNode(sub_ctx).safeness.has_value()) {
      current_node.safeness = std::nullopt;
      return {};
    }

    current_node.safeness.value().insert(GetNode(sub_ctx).safeness.value().begin(),
                                         GetNode(sub_ctx).safeness.value().end());
  }

  return {};
}

std::any SafeVisitor::visitConditionExpr(psr::ConditionExprContext* ctx) {
  visit(ctx->expr());
  visit(ctx->formula());

  auto& current_node = GetNode(ctx);

  current_node.safeness = std::unordered_set<BindingsBound>();

  auto formula_node = GetNode(ctx->formula());

  current_node.safeness.value().insert(formula_node.safeness.value().begin(), formula_node.safeness.value().end());

  return {};
}

std::any SafeVisitor::visitRelAbsExpr(psr::RelAbsExprContext* ctx) {
  visitChildren(ctx);

  GetNode(ctx).safeness = GetNode(ctx->relAbs()).safeness;

  return {};
}

std::any SafeVisitor::visitFormulaExpr(psr::FormulaExprContext* ctx) {
  visit(ctx->formula());

  GetNode(ctx).safeness = GetNode(ctx->formula()).safeness;

  return {};
}

std::any SafeVisitor::visitBindingsExpr(psr::BindingsExprContext* ctx) {
  auto& current_node = GetNode(ctx);

  visit(ctx->expr());

  auto expr_node = GetNode(ctx->expr());

  // If the expression is not safe, the whole expression is not safe
  if (!expr_node.safeness.has_value()) {
    current_node.safeness = std::nullopt;
    return {};
  }

  current_node.safeness = RemoveBoundVariables(expr_node.safeness.value(), ctx->bindingInner());

  return {};
}

std::any SafeVisitor::visitBindingsFormula(psr::BindingsFormulaContext* ctx) {
  visit(ctx->formula());

  auto formula_node = GetNode(ctx->formula());

  GetNode(ctx).safeness = {};

  for (auto& binding : ctx->bindingInner()->binding()) {
    visit(binding);
  }

  return {};
}

std::any SafeVisitor::visitPartialAppl(psr::PartialApplContext* ctx) {
  if (ctx->applBase()->T_ID() && AGGREGATE_MAP.find(ctx->applBase()->T_ID()->getText()) != AGGREGATE_MAP.end()) {
    auto param_ctx = *ctx->applParams()->applParam().begin();
    visit(param_ctx);

    auto& node = GetNode(ctx);

    auto& child_node = GetNode(param_ctx);

    node.safeness = child_node.safeness;

    return {};
  }

  if (ctx->applBase()->T_ID()) {
    bool all_vars = true;
    for (auto& param : ctx->applParams()->applParam()) {
      visit(param);
      if (*GetNode(param).variables.begin() != param->getText()) {
        all_vars = false;
      }
    }

    if (all_vars) {
      std::vector<int> indices;

      for (int i = 0; i < ctx->applParams()->applParam().size(); i++) {
        indices.push_back(i);
      }

      BindingsBound binding_bound;

      auto table_source = TableSource(ctx->applBase()->T_ID()->getText(), GetNode(ctx->applBase()).arity);
      auto projection = SourceProjection(table_source);

      binding_bound.Add(projection);

      for (auto& param : ctx->applParams()->applParam()) {
        auto variable = param->getText();

        binding_bound.variables.push_back(variable);
      }

      GetNode(ctx).safeness = {binding_bound};

      return {};
    }
  }

  visit(ctx->applBase());

  return {};
}

std::any SafeVisitor::visitFullAppl(psr::FullApplContext* ctx) {
  visit(ctx->applBase());

  if (!ctx->applBase()->T_ID()) {
    return {};
  }

  BindingsBound binding_bound;

  auto table_source = TableSource(ctx->applBase()->T_ID()->getText(), GetNode(ctx->applBase()).arity);
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

  GetNode(ctx).safeness = {binding_bound};

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

  auto lhs_safeness = GetNode(ctx->lhs).safeness;
  auto rhs_safeness = GetNode(ctx->rhs).safeness;

  auto& current_node = GetNode(ctx);

  if (!lhs_safeness.has_value() || !rhs_safeness.has_value()) {
    current_node.safeness = std::nullopt;
    return {};
  }

  current_node.safeness = std::unordered_set<BindingsBound>();

  current_node.safeness.value().insert(lhs_safeness.value().begin(), lhs_safeness.value().end());
  current_node.safeness.value().insert(rhs_safeness.value().begin(), rhs_safeness.value().end());

  return {};
}

std::any SafeVisitor::VisitDisjunction(psr::BinOpContext* ctx) {
  visit(ctx->lhs);
  visit(ctx->rhs);

  auto lhs_safeness = GetNode(ctx->lhs).safeness;
  auto rhs_safeness = GetNode(ctx->rhs).safeness;

  GetNode(ctx).safeness = std::unordered_set<BindingsBound>();

  if (!lhs_safeness.has_value() || !rhs_safeness.has_value()) {
    GetNode(ctx).safeness = std::nullopt;
    return {};
  }

  for (const auto& lhs : lhs_safeness.value()) {
    for (const auto& rhs : rhs_safeness.value()) {
      if (lhs.variables == rhs.variables) {
        GetNode(ctx).safeness.value().insert(lhs.mergedWith(rhs));
      }
    }
  }

  return {};
}

std::any SafeVisitor::visitUnOp(psr::UnOpContext* ctx) {
  visit(ctx->formula());

  GetNode(ctx).safeness = GetNode(ctx->formula()).safeness;

  return {};
}

std::any SafeVisitor::visitQuantification(psr::QuantificationContext* ctx) {
  auto& current_node = GetNode(ctx);

  visit(ctx->formula());

  auto formula_node = GetNode(ctx->formula());

  if (!formula_node.safeness.has_value()) {
    GetNode(ctx).safeness = std::nullopt;
    return {};
  }

  current_node.safeness = RemoveBoundVariables(formula_node.safeness.value(), ctx->bindingInner());

  return {};
}

std::any SafeVisitor::visitParen(psr::ParenContext* ctx) {
  visit(ctx->formula());

  GetNode(ctx).safeness = GetNode(ctx->formula()).safeness;

  return {};
}

std::any SafeVisitor::visitApplBase(psr::ApplBaseContext* ctx) {
  visitChildren(ctx);

  GetNode(ctx).safeness = std::unordered_set<BindingsBound>{};

  return {};
}

std::any SafeVisitor::visitApplParam(psr::ApplParamContext* ctx) {
  visitChildren(ctx);

  if (ctx->expr()) {
    GetNode(ctx).safeness = GetNode(ctx->expr()).safeness;
  }

  return {};
}

std::unordered_set<BindingsBound> SafeVisitor::SpecialIntersectionOfBindingBounds(
    const std::vector<std::unordered_set<BindingsBound>>& sets) const {
  if (sets.empty()) {
    return {};
  }

  std::unordered_set<BindingsBound> intersection = sets[0];

  for (size_t i = 1; i < sets.size(); ++i) {
    std::unordered_set<BindingsBound> currentIntersection;

    for (const BindingsBound& binding_bound1 : intersection) {
      // If the element is in the next set, add it to the current intersection
      for (const BindingsBound& binding_bound2 : sets[i]) {
        if (binding_bound1.variables == binding_bound2.variables) {
          ;
          currentIntersection.insert(binding_bound1.mergedWith(binding_bound2));
        }
      }
    }

    intersection = std::move(currentIntersection);

    if (intersection.empty()) {
      break;
    }
  }

  return intersection;
}

std::unordered_set<BindingsBound> SafeVisitor::RemoveBoundVariables(
    const std::unordered_set<BindingsBound>& bindings_bound, psr::BindingInnerContext* binding_inner) const {
  std::unordered_set<BindingsBound> result;

  for (auto& binding_bound : bindings_bound) {
    std::vector<size_t> indices_to_remove;
    for (auto& binding : binding_inner->binding()) {
      std::string binding_var = binding->id->getText();

      auto it = std::find(binding_bound.variables.begin(), binding_bound.variables.end(), binding_var);
      if (it == binding_bound.variables.end()) continue;

      int index = std::distance(binding_bound.variables.begin(), it);
      indices_to_remove.push_back(index);
    }

    auto new_bindings_bound = binding_bound.WithRemovedIndices(indices_to_remove);
    result.insert(new_bindings_bound);
  }

  return result;
}

}  // namespace rel2sql

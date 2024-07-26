#include "safe_visitor.h"

SafeVisitor::SafeVisitor(std::shared_ptr<ExtendedASTData> data) : BaseVisitor(data) {}

std::any SafeVisitor::visitProgram(psr::ProgramContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any SafeVisitor::visitRelDef(psr::RelDefContext *ctx) {
  visit(ctx->relAbs());

  return {};
}

std::any SafeVisitor::visitRelAbs(psr::RelAbsContext *ctx) {
  visitChildren(ctx);

  return {};
}

std::any SafeVisitor::visitIDExpr(psr::IDExprContext *ctx) {
  GetNode(ctx).safeness = {};

  return {};
}

std::any SafeVisitor::visitProductExpr(psr::ProductExprContext *ctx) {
  visitChildren(ctx);

  return {};
}

std::any SafeVisitor::visitConditionExpr(psr::ConditionExprContext *ctx) {
  visitChildren(ctx);

  GetNode(ctx).safeness = GetNode(ctx->formula()).safeness;

  return {};
}

std::any SafeVisitor::visitRelAbsExpr(psr::RelAbsExprContext *ctx) {
  visitChildren(ctx);

  return {};
}

std::any SafeVisitor::visitFormulaExpr(psr::FormulaExprContext *ctx) {
  visit(ctx->formula());

  GetNode(ctx).safeness = GetNode(ctx->formula()).safeness;

  return {};
}

std::any SafeVisitor::visitBindingsExpr(psr::BindingsExprContext *ctx) {
  visit(ctx->expr());

  GetNode(ctx).safeness = GetNode(ctx->expr()).safeness;

  return {};
}

std::any SafeVisitor::visitBindingsFormula(psr::BindingsFormulaContext *ctx) {
  GetNode(ctx).safeness = {};

  for (auto &binding : ctx->bindingInner()->binding()) {
    visit(binding);
  }

  return {};
}

std::any SafeVisitor::visitPartialAppl(psr::PartialApplContext *ctx) {
  visit(ctx->applBase());

  for (auto &param : ctx->applParams()->applParam()) {
    visit(param);
  }

  return {};
}

std::any SafeVisitor::visitFullAppl(psr::FullApplContext *ctx) {
  visit(ctx->applBase());

  if (!ctx->applBase()->T_ID()) {
    return {};
  }

  TupleBinding tuple_binding;

  tuple_binding.union_domain.insert(ctx->applBase()->T_ID()->getText());

  for (auto &param : ctx->applParams()->applParam()) {
    auto id_expr = dynamic_cast<psr::IDExprContext *>(param->expr());
    if (!id_expr) {
      return {};
    }

    auto variable = id_expr->T_ID()->getText();

    tuple_binding.vars_tuple.push_back(variable);
  }

  GetNode(ctx).safeness = {tuple_binding};

  return {};
}

std::any SafeVisitor::visitBinOp(psr::BinOpContext *ctx) {
  if (ctx->K_and()) {
    return VisitConjunction(ctx);
  } else if (ctx->K_or()) {
    return VisitDisjunction(ctx);
  }

  throw std::runtime_error("Unknown binary operator");
}

std::any SafeVisitor::VisitConjunction(psr::BinOpContext *ctx) {
  visit(ctx->lhs);
  visit(ctx->rhs);

  auto lhs_safeness = GetNode(ctx->lhs).safeness;
  auto rhs_safeness = GetNode(ctx->rhs).safeness;

  auto current_node = GetNode(ctx);

  if (!lhs_safeness.has_value() || !rhs_safeness.has_value()) {
    current_node.safeness = std::nullopt;
    return {};
  }

  current_node.safeness = std::unordered_set<TupleBinding>();

  current_node.safeness.value().insert(lhs_safeness.value().begin(), lhs_safeness.value().end());
  current_node.safeness.value().insert(rhs_safeness.value().begin(), rhs_safeness.value().end());

  return {};
}

std::any SafeVisitor::VisitDisjunction(psr::BinOpContext *ctx) {
  visit(ctx->lhs);
  visit(ctx->rhs);

  auto lhs_safeness = GetNode(ctx->lhs).safeness;
  auto rhs_safeness = GetNode(ctx->rhs).safeness;

  GetNode(ctx).safeness = std::unordered_set<TupleBinding>();

  if (!lhs_safeness.has_value() || !rhs_safeness.has_value()) {
    GetNode(ctx).safeness = std::nullopt;
    return {};
  }

  for (const auto &lhs : lhs_safeness.value()) {
    for (const auto &rhs : rhs_safeness.value()) {
      if (lhs.vars_tuple == rhs.vars_tuple) {
        std::unordered_set<std::string> new_union_domain(lhs.union_domain);
        new_union_domain.insert(rhs.union_domain.begin(), rhs.union_domain.end());
        GetNode(ctx).safeness.value().insert({lhs.vars_tuple, new_union_domain});
      }
    }
  }

  return {};
}

std::any SafeVisitor::visitUnOp(psr::UnOpContext *ctx) {
  visit(ctx->formula());

  GetNode(ctx).safeness = GetNode(ctx->formula()).safeness;

  return {};
}

std::any SafeVisitor::visitQuantification(psr::QuantificationContext *ctx) {
  visit(ctx->formula());

  GetNode(ctx).safeness = GetNode(ctx->formula()).safeness;

  return {};
}

std::any SafeVisitor::visitParen(psr::ParenContext *ctx) {
  visit(ctx->formula());

  GetNode(ctx).safeness = GetNode(ctx->formula()).safeness;

  return {};
}

std::any SafeVisitor::visitApplBase(psr::ApplBaseContext *ctx) {
  visitChildren(ctx);

  GetNode(ctx).safeness = {};

  return {};
}

std::any SafeVisitor::visitApplParam(psr::ApplParamContext *ctx) {
  visitChildren(ctx);

  return {};
}

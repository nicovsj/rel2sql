#include "vars_visitor.h"

FreeVariablesVisitor::FreeVariablesVisitor(std::shared_ptr<ExtendedASTData> data) : BaseVisitor(data) {}

std::any FreeVariablesVisitor::visitProgram(psr::ProgramContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  for (auto &child_ctx : ctx->relDef()) {
    visit(child_ctx);
    node.VariablesInplaceUnion(GetNode(child_ctx));
  }

  return node;
}

std::any FreeVariablesVisitor::visitRelDef(psr::RelDefContext *ctx) {
  visit(ctx->relAbs());

  GetNode(ctx) = GetNode(ctx->relAbs());

  return {};
}

std::any FreeVariablesVisitor::visitRelAbs(psr::RelAbsContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  for (auto &child_ctx : ctx->expr()) {
    visit(child_ctx);
    node.VariablesInplaceUnion(GetNode(child_ctx));
  }

  return {};
}

std::any FreeVariablesVisitor::visitIDExpr(psr::IDExprContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  std::string id = ctx->T_ID()->getText();

  node.free_variables.insert(id);
  node.variables.insert(id);

  return {};
}

std::any FreeVariablesVisitor::visitProductExpr(psr::ProductExprContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  for (auto &child : ctx->productInner()->expr()) {
    visit(child);
    node.VariablesInplaceUnion(GetNode(child));
  }

  return {};
}

std::any FreeVariablesVisitor::visitConditionExpr(psr::ConditionExprContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  visit(ctx->lhs);
  visit(ctx->rhs);

  node.VariablesInplaceUnion(GetNode(ctx->lhs));
  node.VariablesInplaceUnion(GetNode(ctx->rhs));

  return {};
}

std::any FreeVariablesVisitor::visitRelAbsExpr(psr::RelAbsExprContext *ctx) {
  visit(ctx->relAbs());

  GetNode(ctx) = GetNode(ctx->relAbs());

  return {};
}

std::any FreeVariablesVisitor::visitFormulaExpr(psr::FormulaExprContext *ctx) {
  visit(ctx->formula());

  GetNode(ctx) = GetNode(ctx->formula());

  return {};
}

std::any FreeVariablesVisitor::visitBindingsExpr(psr::BindingsExprContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  visit(ctx->expr());
  visit(ctx->bindingInner());

  node = GetNode(ctx->expr());

  node.VariablesInplaceDifference(GetNode(ctx->bindingInner()));

  return {};
}

std::any FreeVariablesVisitor::visitBindingsFormula(psr::BindingsFormulaContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  visit(ctx->formula());
  visit(ctx->bindingInner());

  node = GetNode(ctx->formula());

  node.VariablesInplaceDifference(GetNode(ctx->bindingInner()));

  return {};
}

std::any FreeVariablesVisitor::visitPartialAppl(psr::PartialApplContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  visit(ctx->applBase());
  visit(ctx->applParams());

  node = GetNode(ctx->applBase());

  node.VariablesInplaceUnion(GetNode(ctx->applParams()));

  return {};
}

std::any FreeVariablesVisitor::visitFullAppl(psr::FullApplContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  visit(ctx->applBase());
  visit(ctx->applParams());

  node = GetNode(ctx->applBase());

  node.VariablesInplaceUnion(GetNode(ctx->applParams()));

  return {};
}

std::any FreeVariablesVisitor::visitBinOp(psr::BinOpContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  visit(ctx->lhs);
  visit(ctx->rhs);

  node = GetNode(ctx->lhs);

  node.VariablesInplaceUnion(GetNode(ctx->rhs));

  return {};
}

std::any FreeVariablesVisitor::visitUnOp(psr::UnOpContext *ctx) {
  visit(ctx->formula());

  GetNode(ctx) = GetNode(ctx->formula());

  return {};
}

std::any FreeVariablesVisitor::visitQuantification(psr::QuantificationContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  visit(ctx->formula());
  visit(ctx->bindingInner());

  node = GetNode(ctx->formula());

  node.VariablesInplaceDifference(GetNode(ctx->bindingInner()));

  return {};
}

std::any FreeVariablesVisitor::visitParen(psr::ParenContext *ctx) {
  visit(ctx->formula());

  GetNode(ctx) = GetNode(ctx->formula());

  return {};
}

std::any FreeVariablesVisitor::visitBindingInner(psr::BindingInnerContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  for (auto &child_ctx : ctx->binding()) {
    visit(child_ctx);
    node.VariablesInplaceUnion(GetNode(child_ctx));
  }

  return {};
}

std::any FreeVariablesVisitor::visitBinding(psr::BindingContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  if (ctx->id) {
    std::string id = ctx->id->getText();
    node.variables.insert(id);
    node.free_variables.insert(id);
  }

  return {};
}

std::any FreeVariablesVisitor::visitApplBase(psr::ApplBaseContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  if (ctx->relAbs()) {
    visit(ctx->relAbs());
    node = GetNode(ctx->relAbs());
  }
  // Do nothing in the other case because the ID is not a variable

  return {};
}

std::any FreeVariablesVisitor::visitApplParams(psr::ApplParamsContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  for (auto &child_ctx : ctx->applParam()) {
    visit(child_ctx);
    node.VariablesInplaceUnion(GetNode(child_ctx));
  }

  return {};
}

std::any FreeVariablesVisitor::visitApplParam(psr::ApplParamContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  if (ctx->expr()) {
    visit(ctx->expr());
    node = GetNode(ctx->expr());
  }

  return {};
}

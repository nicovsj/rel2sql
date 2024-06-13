#include "fv_visitor.h"

std::any ExtendedASTVisitor::visitProgram(rel_parser::PrunedCoreRelParser::ProgramContext *ctx) {
  ExtendedData data;

  for (auto &child : ctx->relDef()) {
    auto child_data = std::any_cast<ExtendedData>(visit(child));
    data.InplaceUnion(child_data);
  }

  data_[ctx] = data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitRelDef(rel_parser::PrunedCoreRelParser::RelDefContext *ctx) {
  auto data = std::any_cast<ExtendedData>(visit(ctx->relAbs()));

  data_[ctx] = data;

  return data;
}

std::any ExtendedASTVisitor::visitRelAbs(rel_parser::PrunedCoreRelParser::RelAbsContext *ctx) {
  ExtendedData data;

  for (auto &child : ctx->expr()) {
    auto child_data = std::any_cast<ExtendedData>(visit(child));
    data.InplaceUnion(child_data);
  }

  data_[ctx] = data;

  return data;
}

std::any ExtendedASTVisitor::visitLitExpr(rel_parser::PrunedCoreRelParser::LitExprContext *ctx) {
  ExtendedData data;

  data_[ctx] = data;

  return data;
}

std::any ExtendedASTVisitor::visitIDExpr(rel_parser::PrunedCoreRelParser::IDExprContext *ctx) {
  ExtendedData data;

  std::string id = ctx->T_ID()->getText();

  data.free_variables.insert(id);
  data.variables.insert(id);

  data_[ctx] = data;

  return data;
}

std::any ExtendedASTVisitor::visitProductExpr(rel_parser::PrunedCoreRelParser::ProductExprContext *ctx) {
  ExtendedData data;

  for (auto &child : ctx->productInner()->expr()) {
    auto child_data = std::any_cast<ExtendedData>(visit(child));
    data.InplaceUnion(child_data);
  }

  data_[ctx] = data;

  return data;
}

std::any ExtendedASTVisitor::visitConditionExpr(rel_parser::PrunedCoreRelParser::ConditionExprContext *ctx) {
  ExtendedData data;

  auto lh_result = std::any_cast<ExtendedData>(visit(ctx->lhs));

  auto rh_result = std::any_cast<ExtendedData>(visit(ctx->rhs));

  data.InplaceUnion(lh_result);
  data.InplaceUnion(rh_result);

  data_[ctx] = data;

  return data;
}

std::any ExtendedASTVisitor::visitRelAbsExpr(rel_parser::PrunedCoreRelParser::RelAbsExprContext *ctx) {
  auto data = std::any_cast<ExtendedData>(visit(ctx->relAbs()));

  data_[ctx] = data;

  return data;
}

std::any ExtendedASTVisitor::visitFormulaExpr(rel_parser::PrunedCoreRelParser::FormulaExprContext *ctx) {
  auto data = std::any_cast<ExtendedData>(visit(ctx->formula()));

  data_[ctx] = data;

  return data;
}

std::any ExtendedASTVisitor::visitBindingsExpr(rel_parser::PrunedCoreRelParser::BindingsExprContext *ctx) {
  auto bindings_data = std::any_cast<ExtendedData>(visit(ctx->bindingInner()));

  auto expr_data = std::any_cast<ExtendedData>(visit(ctx->expr()));

  expr_data.InplaceDifference(bindings_data);

  data_[ctx] = expr_data;

  return expr_data;
}

std::any ExtendedASTVisitor::visitBindingsFormula(rel_parser::PrunedCoreRelParser::BindingsFormulaContext *ctx) {
  auto bindings_data = std::any_cast<ExtendedData>(visit(ctx->bindingInner()));

  auto formula_data = std::any_cast<ExtendedData>(visit(ctx->formula()));

  formula_data.InplaceDifference(bindings_data);

  data_[ctx] = formula_data;

  return formula_data;
}

std::any ExtendedASTVisitor::visitPartialAppl(rel_parser::PrunedCoreRelParser::PartialApplContext *ctx) {
  auto base_data = std::any_cast<ExtendedData>(visit(ctx->applBase()));

  auto params_data = std::any_cast<ExtendedData>(visit(ctx->applParams()));

  base_data.InplaceUnion(params_data);

  data_[ctx] = base_data;

  return base_data;
}

std::any ExtendedASTVisitor::visitFullAppl(rel_parser::PrunedCoreRelParser::FullApplContext *ctx) {
  auto base_data = std::any_cast<ExtendedData>(visit(ctx->applBase()));

  auto params_data = std::any_cast<ExtendedData>(visit(ctx->applParams()));

  base_data.InplaceUnion(params_data);

  data_[ctx] = base_data;

  return base_data;
}

std::any ExtendedASTVisitor::visitBinOp(rel_parser::PrunedCoreRelParser::BinOpContext *ctx) {
  auto lhs_data = std::any_cast<ExtendedData>(visit(ctx->lhs));

  auto rhs_data = std::any_cast<ExtendedData>(visit(ctx->rhs));

  lhs_data.InplaceUnion(rhs_data);

  data_[ctx] = lhs_data;

  return lhs_data;
}

std::any ExtendedASTVisitor::visitUnOp(rel_parser::PrunedCoreRelParser::UnOpContext *ctx) {
  auto data = std::any_cast<ExtendedData>(visit(ctx->formula()));

  data_[ctx] = data;

  return data;
}

std::any ExtendedASTVisitor::visitQuantification(rel_parser::PrunedCoreRelParser::QuantificationContext *ctx) {
  auto formula_data = std::any_cast<ExtendedData>(visit(ctx->formula()));

  auto bindings_data = std::any_cast<ExtendedData>(visit(ctx->bindingInner()));

  formula_data.InplaceDifference(bindings_data);

  data_[ctx] = formula_data;

  return formula_data;
}

std::any ExtendedASTVisitor::visitParen(rel_parser::PrunedCoreRelParser::ParenContext *ctx) {
  auto data = std::any_cast<ExtendedData>(visit(ctx->formula()));

  data_[ctx] = data;

  return data;
}

std::any ExtendedASTVisitor::visitBindingInner(rel_parser::PrunedCoreRelParser::BindingInnerContext *ctx) {
  ExtendedData data;

  for (auto &child : ctx->binding()) {
    auto child_data = std::any_cast<ExtendedData>(visit(child));
    data.InplaceUnion(child_data);
  }

  data_[ctx] = data;

  return data;
}

std::any ExtendedASTVisitor::visitBinding(rel_parser::PrunedCoreRelParser::BindingContext *ctx) {
  ExtendedData data;

  if (ctx->id) {
    std::string id = ctx->id->getText();
    data.variables.insert(id);
    data.free_variables.insert(id);
  }

  data_[ctx] = data;

  return data;
}

std::any ExtendedASTVisitor::visitApplBase(rel_parser::PrunedCoreRelParser::ApplBaseContext *ctx) {
  ExtendedData data;

  if (ctx->relAbs()) {
    data = std::any_cast<ExtendedData>(visit(ctx->relAbs()));
  }

  // Do nothing in the other case because the ID is not a variable

  data_[ctx] = data;

  return data;
}

std::any ExtendedASTVisitor::visitApplParams(rel_parser::PrunedCoreRelParser::ApplParamsContext *ctx) {
  ExtendedData data;

  for (auto &child : ctx->applParam()) {
    auto child_data = std::any_cast<ExtendedData>(visit(child));

    data.InplaceUnion(child_data);
  }

  data_[ctx] = data;

  return data;
}

std::any ExtendedASTVisitor::visitApplParam(rel_parser::PrunedCoreRelParser::ApplParamContext *ctx) {
  ExtendedData data;

  if (ctx->expr()) {
    data = std::any_cast<ExtendedData>(visit(ctx->expr()));
  }

  data_[ctx] = data;

  return data;
}

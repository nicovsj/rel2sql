#include "lit_visitor.h"

LiteralVisitor::LiteralVisitor(std::shared_ptr<ExtendedASTData> data) : BaseVisitor(data) {}

std::any LiteralVisitor::visitLitExpr(psr::LitExprContext *ctx) {
  visit(ctx->literal());

  GetNode(ctx).constant = GetNode(ctx->literal()).constant;

  return {};
}

std::any LiteralVisitor::visitInt(psr::IntContext *ctx) {
  int constant = std::stoi(ctx->getText());

  GetNode(ctx).constant = constant;

  return {};
}

std::any LiteralVisitor::visitNegInt(psr::NegIntContext *ctx) {
  int constant = std::stoi(ctx->getText());

  GetNode(ctx).constant = constant;

  return {};
}

std::any LiteralVisitor::visitFloat(psr::FloatContext *ctx) {
  float constant = std::stof(ctx->getText());

  GetNode(ctx).constant = constant;

  return {};
}

std::any LiteralVisitor::visitNegFloat(psr::NegFloatContext *ctx) {
  float constant = std::stof(ctx->getText());

  GetNode(ctx).constant = constant;

  return {};
}

std::any LiteralVisitor::visitChar(psr::CharContext *ctx) {
  std::string constant = ctx->getText();

  constant.erase(std::remove(constant.begin(), constant.end(), '\''), constant.end());

  GetNode(ctx).constant = constant;

  return {};
}

std::any LiteralVisitor::visitStr(psr::StrContext *ctx) {
  std::string constant = ctx->getText();

  constant.erase(std::remove(constant.begin(), constant.end(), '\"'), constant.end());

  GetNode(ctx).constant = constant;

  return {};
}

std::any LiteralVisitor::visitBool(psr::BoolContext *ctx) {
  bool constant = ctx->getText() == "true";

  GetNode(ctx).constant = constant;

  return {};
}

std::any LiteralVisitor::visitNumInt(psr::NumIntContext *ctx) {
  int constant = std::stoi(ctx->getText());

  GetNode(ctx).constant = constant;

  return {};
}

std::any LiteralVisitor::visitNumNegInt(psr::NumNegIntContext *ctx) {
  int constant = std::stoi(ctx->getText());

  GetNode(ctx).constant = constant;

  return {};
}

std::any LiteralVisitor::visitNumFloat(psr::NumFloatContext *ctx) {
  float constant = std::stof(ctx->getText());

  GetNode(ctx).constant = constant;

  return {};
}

std::any LiteralVisitor::visitNumNegFloat(psr::NumNegFloatContext *ctx) {
  float constant = std::stof(ctx->getText());

  GetNode(ctx).constant = constant;

  return {};
}

std::any LiteralVisitor::visitProgram(psr::ProgramContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any LiteralVisitor::visitRelDef(psr::RelDefContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any LiteralVisitor::visitRelAbs(psr::RelAbsContext *ctx) {
  GetNode(ctx).has_only_literal_values = true;
  for (auto &child : ctx->expr()) {
    visit(child);
    if (!GetNode(child).has_only_literal_values) {
      GetNode(ctx).has_only_literal_values = false;
      break;
    }
  }

  if (!GetNode(ctx).has_only_literal_values) {
    for (auto &child : ctx->expr()) {
      GetNode(child).has_only_literal_values = false;
    }
  }
  return {};
}

std::any LiteralVisitor::visitIDExpr(psr::IDExprContext *ctx) { return {}; }

std::any LiteralVisitor::visitProductExpr(psr::ProductExprContext *ctx) {
  visit(ctx->productInner());

  GetNode(ctx).has_only_literal_values = GetNode(ctx->productInner()).has_only_literal_values;

  return {};
}

std::any LiteralVisitor::visitConditionExpr(psr::ConditionExprContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any LiteralVisitor::visitRelAbsExpr(psr::RelAbsExprContext *ctx) {
  visitChildren(ctx);

  GetNode(ctx).has_only_literal_values = GetNode(ctx->relAbs()).has_only_literal_values;

  return {};
}

std::any LiteralVisitor::visitFormulaExpr(psr::FormulaExprContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any LiteralVisitor::visitBindingsExpr(psr::BindingsExprContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any LiteralVisitor::visitBindingsFormula(psr::BindingsFormulaContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any LiteralVisitor::visitPartialAppl(psr::PartialApplContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any LiteralVisitor::visitProductInner(psr::ProductInnerContext *ctx) {
  GetNode(ctx).has_only_literal_values = true;
  for (auto &child : ctx->expr()) {
    visit(child);
    if (!GetNode(child).constant.has_value()) {
      GetNode(ctx).has_only_literal_values = false;
      break;
    }
  }

  return {};
}

std::any LiteralVisitor::visitFullAppl(psr::FullApplContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any LiteralVisitor::visitBinOp(psr::BinOpContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any LiteralVisitor::visitUnOp(psr::UnOpContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any LiteralVisitor::visitQuantification(psr::QuantificationContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any LiteralVisitor::visitParen(psr::ParenContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any LiteralVisitor::visitBindingInner(psr::BindingInnerContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any LiteralVisitor::visitBinding(psr::BindingContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any LiteralVisitor::visitApplBase(psr::ApplBaseContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any LiteralVisitor::visitApplParams(psr::ApplParamsContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any LiteralVisitor::visitApplParam(psr::ApplParamContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any LiteralVisitor::visitComparison(psr::ComparisonContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any LiteralVisitor::visitNumTerm(psr::NumTermContext *ctx) {
  visitChildren(ctx);
  return {};
}

std::any LiteralVisitor::visitOpTerm(psr::OpTermContext *ctx) {
  visitChildren(ctx);
  return {};
}

#include "lit_visitor.h"

LiteralVisitor::LiteralVisitor(std::shared_ptr<ExtendedASTIndex> index) : extended_ast_index_(index) {}

std::any LiteralVisitor::visitLitExpr(rel_parser::PrunedCoreRelParser::LitExprContext *ctx) {
  auto result = std::any_cast<ExtendedAST>(visit(ctx->literal()));

  extended_ast_index_->at(ctx).constant = result.Root().constant;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitInt(rel_parser::PrunedCoreRelParser::IntContext *ctx) {
  int constant = std::stoi(ctx->getText());

  (*extended_ast_index_)[ctx].constant = constant;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitNegInt(rel_parser::PrunedCoreRelParser::NegIntContext *ctx) {
  int constant = std::stoi(ctx->getText());

  (*extended_ast_index_)[ctx].constant = constant;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitFloat(rel_parser::PrunedCoreRelParser::FloatContext *ctx) {
  float constant = std::stof(ctx->getText());

  (*extended_ast_index_)[ctx].constant = constant;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitNegFloat(rel_parser::PrunedCoreRelParser::NegFloatContext *ctx) {
  float constant = std::stof(ctx->getText());

  (*extended_ast_index_)[ctx].constant = constant;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitChar(rel_parser::PrunedCoreRelParser::CharContext *ctx) {
  std::string constant = ctx->getText();

  constant.erase(std::remove(constant.begin(), constant.end(), '\''), constant.end());

  (*extended_ast_index_)[ctx].constant = constant;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitStr(rel_parser::PrunedCoreRelParser::StrContext *ctx) {
  std::string constant = ctx->getText();

  constant.erase(std::remove(constant.begin(), constant.end(), '\"'), constant.end());

  (*extended_ast_index_)[ctx].constant = constant;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitBool(rel_parser::PrunedCoreRelParser::BoolContext *ctx) {
  bool constant = ctx->getText() == "true";

  (*extended_ast_index_)[ctx].constant = constant;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitProgram(rel_parser::PrunedCoreRelParser::ProgramContext *ctx) {
  visitChildren(ctx);
  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitRelDef(rel_parser::PrunedCoreRelParser::RelDefContext *ctx) {
  visitChildren(ctx);
  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitRelAbs(rel_parser::PrunedCoreRelParser::RelAbsContext *ctx) {
  visitChildren(ctx);
  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitIDExpr(rel_parser::PrunedCoreRelParser::IDExprContext *ctx) {
  return ExtendedAST(ctx, extended_ast_index_);
}

std::any LiteralVisitor::visitProductExpr(rel_parser::PrunedCoreRelParser::ProductExprContext *ctx) {
  visitChildren(ctx);
  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitConditionExpr(rel_parser::PrunedCoreRelParser::ConditionExprContext *ctx) {
  visitChildren(ctx);
  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitRelAbsExpr(rel_parser::PrunedCoreRelParser::RelAbsExprContext *ctx) {
  visitChildren(ctx);
  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitFormulaExpr(rel_parser::PrunedCoreRelParser::FormulaExprContext *ctx) {
  visitChildren(ctx);
  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitBindingsExpr(rel_parser::PrunedCoreRelParser::BindingsExprContext *ctx) {
  visitChildren(ctx);
  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitBindingsFormula(rel_parser::PrunedCoreRelParser::BindingsFormulaContext *ctx) {
  visitChildren(ctx);
  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitPartialAppl(rel_parser::PrunedCoreRelParser::PartialApplContext *ctx) {
  visitChildren(ctx);
  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitFullAppl(rel_parser::PrunedCoreRelParser::FullApplContext *ctx) {
  visitChildren(ctx);
  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitBinOp(rel_parser::PrunedCoreRelParser::BinOpContext *ctx) {
  visitChildren(ctx);
  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitUnOp(rel_parser::PrunedCoreRelParser::UnOpContext *ctx) {
  visitChildren(ctx);
  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitQuantification(rel_parser::PrunedCoreRelParser::QuantificationContext *ctx) {
  visitChildren(ctx);
  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitParen(rel_parser::PrunedCoreRelParser::ParenContext *ctx) {
  visitChildren(ctx);
  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitBindingInner(rel_parser::PrunedCoreRelParser::BindingInnerContext *ctx) {
  visitChildren(ctx);
  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitBinding(rel_parser::PrunedCoreRelParser::BindingContext *ctx) {
  visitChildren(ctx);
  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitApplBase(rel_parser::PrunedCoreRelParser::ApplBaseContext *ctx) {
  visitChildren(ctx);
  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitApplParams(rel_parser::PrunedCoreRelParser::ApplParamsContext *ctx) {
  visitChildren(ctx);
  return ExtendedAST{ctx, extended_ast_index_};
}

std::any LiteralVisitor::visitApplParam(rel_parser::PrunedCoreRelParser::ApplParamContext *ctx) {
  visitChildren(ctx);
  return ExtendedAST{ctx, extended_ast_index_};
}

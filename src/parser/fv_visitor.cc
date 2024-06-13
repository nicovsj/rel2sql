#include "fv_visitor.h"

std::any ExtendedASTVisitor::visitProgram(rel_parser::PrunedCoreRelParser::ProgramContext *ctx) {
  ExtendedData data;

  for (auto &child : ctx->relDef()) {
    auto child_ast = std::any_cast<ExtendedAST>(visit(child));
    auto child_data = child_ast.RootExtendedData();

    data.InplaceUnion(child_data);
  }

  (*data_)[ctx] = data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitRelDef(rel_parser::PrunedCoreRelParser::RelDefContext *ctx) {
  auto ast = std::any_cast<ExtendedAST>(visit(ctx->relAbs()));
  auto data = ast.RootExtendedData();

  (*data_)[ctx] = data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitRelAbs(rel_parser::PrunedCoreRelParser::RelAbsContext *ctx) {
  ExtendedData data;

  for (auto &child : ctx->expr()) {
    auto child_ast = std::any_cast<ExtendedAST>(visit(child));
    auto child_data = child_ast.RootExtendedData();
    data.InplaceUnion(child_data);
  }

  (*data_)[ctx] = data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitLitExpr(rel_parser::PrunedCoreRelParser::LitExprContext *ctx) {
  ExtendedData data;

  (*data_)[ctx] = data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitIDExpr(rel_parser::PrunedCoreRelParser::IDExprContext *ctx) {
  ExtendedData data;

  std::string id = ctx->T_ID()->getText();

  data.free_variables.insert(id);
  data.variables.insert(id);

  (*data_)[ctx] = data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitProductExpr(rel_parser::PrunedCoreRelParser::ProductExprContext *ctx) {
  ExtendedData data;

  for (auto &child : ctx->productInner()->expr()) {
    auto child_ast = std::any_cast<ExtendedAST>(visit(child));
    auto child_data = child_ast.RootExtendedData();
    data.InplaceUnion(child_data);
  }

  (*data_)[ctx] = data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitConditionExpr(rel_parser::PrunedCoreRelParser::ConditionExprContext *ctx) {
  ExtendedData data;

  auto lh_ast = std::any_cast<ExtendedAST>(visit(ctx->lhs));
  auto lh_data = lh_ast.RootExtendedData();

  auto rh_ast = std::any_cast<ExtendedAST>(visit(ctx->rhs));
  auto rh_data = rh_ast.RootExtendedData();

  data.InplaceUnion(lh_data);
  data.InplaceUnion(rh_data);

  (*data_)[ctx] = data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitRelAbsExpr(rel_parser::PrunedCoreRelParser::RelAbsExprContext *ctx) {
  auto ast = std::any_cast<ExtendedAST>(visit(ctx->relAbs()));
  auto data = ast.RootExtendedData();

  (*data_)[ctx] = data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitFormulaExpr(rel_parser::PrunedCoreRelParser::FormulaExprContext *ctx) {
  auto ast = std::any_cast<ExtendedAST>(visit(ctx->formula()));
  auto data = ast.RootExtendedData();

  (*data_)[ctx] = data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitBindingsExpr(rel_parser::PrunedCoreRelParser::BindingsExprContext *ctx) {
  auto bindings_ast = std::any_cast<ExtendedAST>(visit(ctx->bindingInner()));
  auto bindings_data = bindings_ast.RootExtendedData();

  auto expr_ast = std::any_cast<ExtendedAST>(visit(ctx->expr()));
  auto expr_data = expr_ast.RootExtendedData();

  expr_data.InplaceDifference(bindings_data);

  (*data_)[ctx] = expr_data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitBindingsFormula(rel_parser::PrunedCoreRelParser::BindingsFormulaContext *ctx) {
  auto bindings_ast = std::any_cast<ExtendedAST>(visit(ctx->bindingInner()));
  auto bindings_data = bindings_ast.RootExtendedData();

  auto formula_ast = std::any_cast<ExtendedAST>(visit(ctx->formula()));
  auto formula_data = formula_ast.RootExtendedData();

  formula_data.InplaceDifference(bindings_data);

  (*data_)[ctx] = formula_data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitPartialAppl(rel_parser::PrunedCoreRelParser::PartialApplContext *ctx) {
  auto base_ast = std::any_cast<ExtendedAST>(visit(ctx->applBase()));
  auto base_data = base_ast.RootExtendedData();

  auto params_ast = std::any_cast<ExtendedAST>(visit(ctx->applParams()));
  auto params_data = params_ast.RootExtendedData();

  base_data.InplaceUnion(params_data);

  (*data_)[ctx] = base_data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitFullAppl(rel_parser::PrunedCoreRelParser::FullApplContext *ctx) {
  auto base_ast = std::any_cast<ExtendedAST>(visit(ctx->applBase()));
  auto base_data = base_ast.RootExtendedData();

  auto params_ast = std::any_cast<ExtendedAST>(visit(ctx->applParams()));
  auto params_data = params_ast.RootExtendedData();

  base_data.InplaceUnion(params_data);

  (*data_)[ctx] = base_data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitBinOp(rel_parser::PrunedCoreRelParser::BinOpContext *ctx) {
  auto lhs_ast = std::any_cast<ExtendedAST>(visit(ctx->lhs));
  auto lhs_data = lhs_ast.RootExtendedData();

  auto rhs_ast = std::any_cast<ExtendedAST>(visit(ctx->rhs));
  auto rhs_data = rhs_ast.RootExtendedData();

  lhs_data.InplaceUnion(rhs_data);

  (*data_)[ctx] = lhs_data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitUnOp(rel_parser::PrunedCoreRelParser::UnOpContext *ctx) {
  auto ast = std::any_cast<ExtendedAST>(visit(ctx->formula()));
  auto data = ast.RootExtendedData();

  (*data_)[ctx] = data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitQuantification(rel_parser::PrunedCoreRelParser::QuantificationContext *ctx) {
  auto formula_ast = std::any_cast<ExtendedAST>(visit(ctx->formula()));
  auto formula_data = formula_ast.RootExtendedData();

  auto bindings_ast = std::any_cast<ExtendedAST>(visit(ctx->bindingInner()));
  auto bindings_data = bindings_ast.RootExtendedData();

  formula_data.InplaceDifference(bindings_data);

  (*data_)[ctx] = formula_data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitParen(rel_parser::PrunedCoreRelParser::ParenContext *ctx) {
  auto ast = std::any_cast<ExtendedAST>(visit(ctx->formula()));
  auto data = ast.RootExtendedData();

  (*data_)[ctx] = data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitBindingInner(rel_parser::PrunedCoreRelParser::BindingInnerContext *ctx) {
  ExtendedData data;

  for (auto &child : ctx->binding()) {
    auto child_ast = std::any_cast<ExtendedAST>(visit(child));
    auto child_data = child_ast.RootExtendedData();
    data.InplaceUnion(child_data);
  }

  (*data_)[ctx] = data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitBinding(rel_parser::PrunedCoreRelParser::BindingContext *ctx) {
  ExtendedData data;

  if (ctx->id) {
    std::string id = ctx->id->getText();
    data.variables.insert(id);
    data.free_variables.insert(id);
  }

  (*data_)[ctx] = data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitApplBase(rel_parser::PrunedCoreRelParser::ApplBaseContext *ctx) {
  ExtendedData data;

  if (ctx->relAbs()) {
    auto ast = std::any_cast<ExtendedAST>(visit(ctx->relAbs()));
    data = ast.RootExtendedData();
  }

  // Do nothing in the other case because the ID is not a variable

  (*data_)[ctx] = data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitApplParams(rel_parser::PrunedCoreRelParser::ApplParamsContext *ctx) {
  ExtendedData data;

  for (auto &child : ctx->applParam()) {
    auto child_ast = std::any_cast<ExtendedAST>(visit(child));
    auto child_data = child_ast.RootExtendedData();

    data.InplaceUnion(child_data);
  }

  (*data_)[ctx] = data;

  return ExtendedAST{ctx, data_};
}

std::any ExtendedASTVisitor::visitApplParam(rel_parser::PrunedCoreRelParser::ApplParamContext *ctx) {
  ExtendedData data;

  if (ctx->expr()) {
    auto ast = std::any_cast<ExtendedAST>(visit(ctx->expr()));
    data = ast.RootExtendedData();
  }

  (*data_)[ctx] = data;

  return ExtendedAST{ctx, data_};
}

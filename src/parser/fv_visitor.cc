#include "fv_visitor.h"

FreeVariablesVisitor::FreeVariablesVisitor(std::shared_ptr<ExtendedASTIndex> extended_ast)
    : extended_ast_index_(extended_ast) {}

std::any FreeVariablesVisitor::visitProgram(rel_parser::PrunedCoreRelParser::ProgramContext *ctx) {
  ExtendedNode data;

  for (auto &child : ctx->relDef()) {
    auto child_ast = std::any_cast<ExtendedAST>(visit(child));
    auto child_data = child_ast.Root();

    data.InplaceUnion(child_data);
  }

  (*extended_ast_index_)[ctx] = data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitRelDef(rel_parser::PrunedCoreRelParser::RelDefContext *ctx) {
  auto ast = std::any_cast<ExtendedAST>(visit(ctx->relAbs()));
  auto data = ast.Root();

  (*extended_ast_index_)[ctx] = data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitRelAbs(rel_parser::PrunedCoreRelParser::RelAbsContext *ctx) {
  ExtendedNode data;

  for (auto &child : ctx->expr()) {
    auto child_ast = std::any_cast<ExtendedAST>(visit(child));
    auto child_data = child_ast.Root();
    data.InplaceUnion(child_data);
  }

  (*extended_ast_index_)[ctx] = data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitLitExpr(rel_parser::PrunedCoreRelParser::LitExprContext *ctx) {
  ExtendedNode data;

  (*extended_ast_index_)[ctx] = data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitIDExpr(rel_parser::PrunedCoreRelParser::IDExprContext *ctx) {
  ExtendedNode data;

  std::string id = ctx->T_ID()->getText();

  data.free_variables.insert(id);
  data.variables.insert(id);

  (*extended_ast_index_)[ctx] = data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitProductExpr(rel_parser::PrunedCoreRelParser::ProductExprContext *ctx) {
  ExtendedNode data;

  for (auto &child : ctx->productInner()->expr()) {
    auto child_ast = std::any_cast<ExtendedAST>(visit(child));
    auto child_data = child_ast.Root();
    data.InplaceUnion(child_data);
  }

  (*extended_ast_index_)[ctx] = data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitConditionExpr(rel_parser::PrunedCoreRelParser::ConditionExprContext *ctx) {
  ExtendedNode data;

  auto lh_ast = std::any_cast<ExtendedAST>(visit(ctx->lhs));
  auto lh_data = lh_ast.Root();

  auto rh_ast = std::any_cast<ExtendedAST>(visit(ctx->rhs));
  auto rh_data = rh_ast.Root();

  data.InplaceUnion(lh_data);
  data.InplaceUnion(rh_data);

  (*extended_ast_index_)[ctx] = data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitRelAbsExpr(rel_parser::PrunedCoreRelParser::RelAbsExprContext *ctx) {
  auto ast = std::any_cast<ExtendedAST>(visit(ctx->relAbs()));
  auto data = ast.Root();

  (*extended_ast_index_)[ctx] = data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitFormulaExpr(rel_parser::PrunedCoreRelParser::FormulaExprContext *ctx) {
  auto ast = std::any_cast<ExtendedAST>(visit(ctx->formula()));
  auto data = ast.Root();

  (*extended_ast_index_)[ctx] = data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitBindingsExpr(rel_parser::PrunedCoreRelParser::BindingsExprContext *ctx) {
  auto bindings_ast = std::any_cast<ExtendedAST>(visit(ctx->bindingInner()));
  auto bindings_data = bindings_ast.Root();

  auto expr_ast = std::any_cast<ExtendedAST>(visit(ctx->expr()));
  auto expr_data = expr_ast.Root();

  expr_data.InplaceDifference(bindings_data);

  (*extended_ast_index_)[ctx] = expr_data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitBindingsFormula(rel_parser::PrunedCoreRelParser::BindingsFormulaContext *ctx) {
  auto bindings_ast = std::any_cast<ExtendedAST>(visit(ctx->bindingInner()));
  auto bindings_data = bindings_ast.Root();

  auto formula_ast = std::any_cast<ExtendedAST>(visit(ctx->formula()));
  auto formula_data = formula_ast.Root();

  formula_data.InplaceDifference(bindings_data);

  (*extended_ast_index_)[ctx] = formula_data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitPartialAppl(rel_parser::PrunedCoreRelParser::PartialApplContext *ctx) {
  auto base_ast = std::any_cast<ExtendedAST>(visit(ctx->applBase()));
  auto base_data = base_ast.Root();

  auto params_ast = std::any_cast<ExtendedAST>(visit(ctx->applParams()));
  auto params_data = params_ast.Root();

  base_data.InplaceUnion(params_data);

  (*extended_ast_index_)[ctx] = base_data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitFullAppl(rel_parser::PrunedCoreRelParser::FullApplContext *ctx) {
  auto base_ast = std::any_cast<ExtendedAST>(visit(ctx->applBase()));
  auto base_data = base_ast.Root();

  auto params_ast = std::any_cast<ExtendedAST>(visit(ctx->applParams()));
  auto params_data = params_ast.Root();

  base_data.InplaceUnion(params_data);

  (*extended_ast_index_)[ctx] = base_data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitBinOp(rel_parser::PrunedCoreRelParser::BinOpContext *ctx) {
  auto lhs_ast = std::any_cast<ExtendedAST>(visit(ctx->lhs));
  auto lhs_data = lhs_ast.Root();

  auto rhs_ast = std::any_cast<ExtendedAST>(visit(ctx->rhs));
  auto rhs_data = rhs_ast.Root();

  lhs_data.InplaceUnion(rhs_data);

  (*extended_ast_index_)[ctx] = lhs_data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitUnOp(rel_parser::PrunedCoreRelParser::UnOpContext *ctx) {
  auto ast = std::any_cast<ExtendedAST>(visit(ctx->formula()));
  auto data = ast.Root();

  (*extended_ast_index_)[ctx] = data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitQuantification(rel_parser::PrunedCoreRelParser::QuantificationContext *ctx) {
  auto formula_ast = std::any_cast<ExtendedAST>(visit(ctx->formula()));
  auto formula_data = formula_ast.Root();

  auto bindings_ast = std::any_cast<ExtendedAST>(visit(ctx->bindingInner()));
  auto bindings_data = bindings_ast.Root();

  formula_data.InplaceDifference(bindings_data);

  (*extended_ast_index_)[ctx] = formula_data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitParen(rel_parser::PrunedCoreRelParser::ParenContext *ctx) {
  auto ast = std::any_cast<ExtendedAST>(visit(ctx->formula()));
  auto data = ast.Root();

  (*extended_ast_index_)[ctx] = data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitBindingInner(rel_parser::PrunedCoreRelParser::BindingInnerContext *ctx) {
  ExtendedNode data;

  for (auto &child : ctx->binding()) {
    auto child_ast = std::any_cast<ExtendedAST>(visit(child));
    auto child_data = child_ast.Root();
    data.InplaceUnion(child_data);
  }

  (*extended_ast_index_)[ctx] = data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitBinding(rel_parser::PrunedCoreRelParser::BindingContext *ctx) {
  ExtendedNode data;

  if (ctx->id) {
    std::string id = ctx->id->getText();
    data.variables.insert(id);
    data.free_variables.insert(id);
  }

  (*extended_ast_index_)[ctx] = data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitApplBase(rel_parser::PrunedCoreRelParser::ApplBaseContext *ctx) {
  ExtendedNode data;

  if (ctx->relAbs()) {
    auto ast = std::any_cast<ExtendedAST>(visit(ctx->relAbs()));
    data = ast.Root();
  }

  // Do nothing in the other case because the ID is not a variable

  (*extended_ast_index_)[ctx] = data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitApplParams(rel_parser::PrunedCoreRelParser::ApplParamsContext *ctx) {
  ExtendedNode data;

  for (auto &child : ctx->applParam()) {
    auto child_ast = std::any_cast<ExtendedAST>(visit(child));
    auto child_data = child_ast.Root();

    data.InplaceUnion(child_data);
  }

  (*extended_ast_index_)[ctx] = data;

  return ExtendedAST{ctx, extended_ast_index_};
}

std::any FreeVariablesVisitor::visitApplParam(rel_parser::PrunedCoreRelParser::ApplParamContext *ctx) {
  ExtendedNode data;

  if (ctx->expr()) {
    auto ast = std::any_cast<ExtendedAST>(visit(ctx->expr()));
    data = ast.Root();
  }

  (*extended_ast_index_)[ctx] = data;

  return ExtendedAST{ctx, extended_ast_index_};
}
